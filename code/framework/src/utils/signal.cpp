#include "signal.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <map>
#include <set>
#include <signal.h>
#include <stdexcept>
#include <sys/select.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace Framework::Utils {
    namespace {
        using Signals = std::set<int>;
        using Sink    = SignalHandler::Sink;
    } // namespace

    struct SignalHandler::Handle {
        Signals sigs;
    };

    namespace {
        using Handle = SignalHandler::Handle;

        static constexpr const std::size_t MaxErrors = 5;

        class ReadError: public std::runtime_error {
          public:
            using std::runtime_error::runtime_error;
        };

        void catch_signal(int);

        static constexpr const std::uint8_t ShutdownSignal = std::numeric_limits<std::uint8_t>::max();

        struct Dispatcher {
            struct sigaction ancestor;
            const Handle *handle;

            Dispatcher(const Handle *);
        };

        Dispatcher::Dispatcher(const Handle *h): ancestor {}, handle {h} {}

        class Registry {
            using Dispatchers = std::vector<Dispatcher>;
            using SignalHandlers    = std::map<int, Dispatchers>;
            using Callables   = std::map<const Handle *, Sink>;

          public:
            static Registry &instance();
            ~Registry();

            template <typename Func>
            Registry &addSignalHandler(const Handle *, Func &&);

            Registry &removeSignalHandler(const Handle *);

            Registry &push(int, const Handle *);
            Registry &pop(int, const Handle *);

            void handle(int);

          private:
            Registry();

            void readSignals();

            void closePipe();

          private:
            static Registry instance_;

          private:
            SignalHandlers SignalHandlers_;
            Callables callables_;
            int readFd_;
            int writeFd_;
            std::thread thread_;
            std::atomic_bool running_;
        };

        Registry Registry::instance_ {};

        inline Registry &Registry::instance() {
            return instance_;
        }

        Registry::Registry(): SignalHandlers_ {}, callables_ {}, readFd_ {}, writeFd_ {}, thread_ {}, running_ {true} {
            int fds[2];
            if (::pipe(fds) != 0) {
                throw std::runtime_error {"Cannot create a pipe for communicating signals."};
            }

            readFd_  = fds[0];
            writeFd_ = fds[1];

            if (fcntl(writeFd_, F_SETFL, fcntl(writeFd_, F_GETFL) | O_NONBLOCK) < 0) {
                closePipe();
                throw std::runtime_error {"Cannot set the write filedescriptor of the signal pipe to non-blocking."};
            }

            if (fcntl(writeFd_, F_SETFD, fcntl(writeFd_, F_GETFD) | FD_CLOEXEC) < 0) {
                closePipe();
                throw std::runtime_error {"Cannot set the write filedescriptor of the signal pipe to close on exec."};
            }

            if (fcntl(readFd_, F_SETFD, fcntl(readFd_, F_GETFD) | FD_CLOEXEC) < 0) {
                closePipe();
                throw std::runtime_error {"Cannot set the read filedescriptor of the signal pipe to close on exec."};
            }

            thread_ = std::thread {&Registry::readSignals, this};
        }

        Registry::~Registry() {
            running_.exchange(false);
            std::size_t errorCounter = 0;

            while (errorCounter < MaxErrors && ::write(writeFd_, &ShutdownSignal, sizeof ShutdownSignal) != sizeof ShutdownSignal) { ++errorCounter; }

            thread_.join();
            closePipe();
        }

        template <typename Func>
        inline Registry &Registry::addSignalHandler(const Handle *handle, Func &&func) {
            callables_.emplace(handle, std::forward<Func>(func));
            return *this;
        }

        inline Registry &Registry::removeSignalHandler(const Handle *handle) {
            callables_.erase(handle);
            return *this;
        }

        Registry &Registry::push(int signum, const Handle *handle) {
            struct sigaction action;

            std::memset(&action, 0, sizeof action);

            action.sa_handler = &catch_signal;
            action.sa_flags   = 0;
            sigfillset(&action.sa_mask);

            Dispatcher dispatcher {handle};

            if (::sigaction(signum, &action, &dispatcher.ancestor) < 0) {
                throw std::runtime_error {"Cannot register a signal SignalHandler for the given signal."};
            }

            SignalHandlers::iterator atSignal = SignalHandlers_.find(signum);
            if (atSignal == SignalHandlers_.end()) {
                atSignal = SignalHandlers_.emplace(signum, Dispatchers {}).first;
            }

            Dispatchers &dispatchers = atSignal->second;
            dispatchers.push_back(std::move(dispatcher));

            return *this;
        }

        void Registry::handle(int signum) {
            std::uint8_t reduced     = static_cast<std::uint8_t>(signum);
            std::size_t errorCounter = 0;
            while (errorCounter < MaxErrors && ::write(writeFd_, &reduced, sizeof reduced) != sizeof reduced) { ++errorCounter; }
        }

        Registry &Registry::pop(int signum, const Handle *handle) {
            SignalHandlers::iterator atSignalHandler = SignalHandlers_.find(signum);
            if (atSignalHandler != SignalHandlers_.end()) {
                Dispatchers &dispatchers                       = atSignalHandler->second;
                Dispatchers::reverse_iterator atDispatcher     = dispatchers.rbegin();
                Dispatchers::reverse_iterator endOfDispatchers = dispatchers.rend();

                bool onTop = true;
                while (onTop && atDispatcher != endOfDispatchers) {
                    if (atDispatcher->handle == handle) {
                        ::sigaction(signum, &atDispatcher->ancestor, nullptr);

                        atDispatcher = Dispatchers::reverse_iterator {dispatchers.erase(std::next(atDispatcher).base())};
                    }
                    else {
                        onTop = false;
                        ++atDispatcher;
                    }
                }
                while (atDispatcher != endOfDispatchers) {
                    if (atDispatcher->handle == handle) {
                        auto previous      = std::prev(atDispatcher);
                        previous->ancestor = std::move(atDispatcher->ancestor);

                        atDispatcher = Dispatchers::reverse_iterator {dispatchers.erase(std::next(atDispatcher).base())};
                    }
                    else {
                        ++atDispatcher;
                    }
                }

                if (atSignalHandler->second.empty()) {
                    SignalHandlers_.erase(atSignalHandler);
                }
            }

            return *this;
        }

        void Registry::readSignals() {
            std::size_t readErrors = 0;

            while (readErrors < MaxErrors && running_.load()) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(readFd_, &fds);

                timeval timeout {.tv_sec = 60 * 60, .tv_usec = 0};

                try {
                    int nrFds = ::select(readFd_ + 1, &fds, nullptr, nullptr, &timeout);
                    if (nrFds < 0) {
                        throw ReadError {"Cannot select on the signal pipes read file descriptor."};
                    }
                    else if (FD_ISSET(readFd_, &fds)) {
                        std::uint8_t signum;
                        ssize_t bytes = ::read(readFd_, &signum, sizeof signum);

                        if (bytes != sizeof signum) {
                            throw ReadError {"Cannot read from the signal pipe."};
                        }
                        else if (signum != ShutdownSignal && running_.load()) {
                            readErrors                   = 0;
                            SignalHandlers::iterator atSignalHandler = SignalHandlers_.find(signum);

                            if (atSignalHandler != SignalHandlers_.end()) {
                                Dispatchers &dispatchers = atSignalHandler->second;

                                if (!dispatchers.empty()) {
                                    const Handle *handle = dispatchers.back().handle;

                                    Callables::const_iterator atCallable = callables_.find(handle);
                                    if (atCallable != callables_.cend() && atCallable->second) {
                                        (atCallable->second)(signum);
                                    }
                                }
                            }
                        }
                    }
                }
                catch (const ReadError &) {
                    ++readErrors;
                    std::this_thread::sleep_for(std::chrono::milliseconds {20});
                }
                catch (...) {
                    // "The execution of the signal SignalHandler raised an exception."
                }
            }
        }

        inline void Registry::closePipe() {
            ::close(readFd_);
            ::close(writeFd_);
        }

        void catch_signal(int signum) {
            Registry::instance().handle(signum);
        }
    } // namespace

    /**
     * Creates a signal SignalHandler.
     * Registers the SignalHandler in the internal registry.
     * @param callback the callback that takes the signal number as single argument
     */
    SignalHandler::SignalHandler(Sink &&callback): handle_ {new Handle} {
        Registry::instance().addSignalHandler(handle_.get(), std::move(callback));
    }

    SignalHandler::SignalHandler(SignalHandler &&) = default;
    SignalHandler &SignalHandler::operator=(SignalHandler &&) = default;

    /**
     * Unregisters the SignalHandler from the internal registry.
     */
    SignalHandler::~SignalHandler() {
        Registry &registry = Registry::instance();

        for (const auto s : handle_->sigs) { registry.pop(s, handle_.get()); }

        registry.removeSignalHandler(handle_.get());
    }

    /**
     * Adds the given signal to the set of signals listened to.
     * If the signal is already listened to, this function does nothing.
     * After this function returns the given signal will be delivered to the stored
     * callback.
     * @param signum the signal to additionally listen on
     * @return this SignalHandler
     */
    SignalHandler &SignalHandler::addSignal(int signum) {
        Signals::const_iterator atSignal = handle_->sigs.find(signum);

        if (atSignal == handle_->sigs.cend()) {
            Registry::instance().push(signum, handle_.get());
            handle_->sigs.insert(signum);
        }

        return *this;
    }

    /**
     * Removes the given signal from the set of signals listened to.
     * If the signal is not listened to, this function does nothing.
     * After this function returns the given signal will not be delivered to the stored
     * callback any more.
     * @param signum the signal to remove from the set of signals listened to
     * @return this SignalHandler
     */
    SignalHandler &SignalHandler::removeSignal(int signum) {
        Signals::const_iterator atSignal = handle_->sigs.find(signum);

        if (atSignal != handle_->sigs.cend()) {
            Registry::instance().pop(signum, handle_.get());
            handle_->sigs.erase(atSignal);
        }

        return *this;
    }

    /**
     * Returns true iff the given signal signum is listened to, iff the signal with
     * the given number is delivered to the callback.
     * @param signum the signal in question
     * @return true iff the given signal signum is listened to
     */
    bool SignalHandler::listensOn(int signum) const {
        return handle_->sigs.find(signum) != handle_->sigs.cend();
    }
} // namespace Framework::Utils