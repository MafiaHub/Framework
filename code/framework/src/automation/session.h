#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace httplib {
    class Server;
}

namespace Framework::Automation {
    struct Options {
        std::filesystem::path directory;
        std::filesystem::path workspace;
        std::string token;
        std::string role;
        bool enabled = false;

        // QA is opt-in in debug builds. Both directories must already exist;
        // the instance directory must be strictly inside the workspace.
        static Options FromEnvironment();
    };

    class Session final {
      public:
        struct Command {
            std::string id;
            std::string operation;
            nlohmann::json arguments;
        };

        Session();
        ~Session();
        Session(const Session &)            = delete;
        Session &operator=(const Session &) = delete;
        void Start(const Options &options, const nlohmann::json &capabilities);
        void Stop();
        bool IsEnabled() const {
            return _options.enabled;
        }
        const Options &GetOptions() const {
            return _options;
        }
        std::vector<Command> TakeCommands();
        void Complete(const std::string &id, const nlohmann::json &result, bool success = true);
        void Publish(nlohmann::json snapshot);
        void Emit(const std::string &name, nlohmann::json payload = nlohmann::json::object());
        void AdvanceTick() {
            _tick.fetch_add(1, std::memory_order_relaxed);
        }
        std::uint64_t Tick() const {
            return _tick.load(std::memory_order_relaxed);
        }
        bool ContactExpired() const;

      private:
        void WriteJournal(std::stop_token stop);
        Options _options;
        std::unique_ptr<httplib::Server> _server;
        std::jthread _listener;
        std::jthread _writer;
        mutable std::mutex _mutex;
        nlohmann::json _snapshot = nlohmann::json::object();
        struct Operation {
            std::string fingerprint;
            nlohmann::json result;
        };
        std::unordered_map<std::string, Operation> _operations;
        std::deque<Command> _commands;
        std::deque<nlohmann::json> _events;
        std::deque<nlohmann::json> _journal;
        std::atomic<std::uint64_t> _tick {0};
        std::atomic<std::int64_t> _lastContact {0};
        std::uint64_t _sequence = 0;
        bool _journalOverflow   = false;
        std::atomic_bool _journalFailed {false};
    };

    // Path policy shared by the launcher, adapter and runner. Canonicalizing
    // existing ancestors also rejects escapes through symlinks/junctions.
    bool IsInside(const std::filesystem::path &path, const std::filesystem::path &root);
} // namespace Framework::Automation
