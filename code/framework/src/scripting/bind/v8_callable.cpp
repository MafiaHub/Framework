/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "v8_callable.h"

#include "call.h"
#include "v8_backend.h"

#include "../callback_scope.h"
#include "../engine.h"
#include "../engine_helpers.h"
#include "../resource/resource_manager.h"
#include "../resource_monitor.h"

#include <v8pp/convert.hpp>

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// The JavaScript side of ScriptCallable and Deferred.
//
// What is kept for an isolate is one Runtime, found by the isolate. A callable and a deferred result share it, so
// they can tell that the isolate is gone without asking it. A function carries the id of its callable as a private
// property, which is how the same function is found to have one already; a function made to stand for a callable
// of another runtime carries an id that leads back to that callable.
//
// A V8 callback is handed a key and looks its target up, never a pointer: an isolate can still run code after what
// was kept for it is gone, and the callback then finds nothing.

namespace Framework::Scripting::Bind {
    namespace {
        class Callable;
        class DeferredResult;

        // A call that waits on the promise its function returned.
        struct Waiting final {
            Completion done;
            ReturnMode mode = ReturnMode::Value;
        };

        struct Runtime final {
            // nullptr once the isolate is going.
            v8::Isolate *isolate = nullptr;
            // By the id the function carries. A callable takes itself out as it goes.
            std::unordered_map<uint64_t, std::weak_ptr<Callable>> callables;
            // The callables the functions made by V8FunctionOf stand for, by the id such a function carries. The
            // function keeps its callable, so an entry that has expired is of a function that was collected.
            std::unordered_map<uint64_t, std::weak_ptr<ScriptCallable>> standIns;
            size_t standInsAtSweep = 0;
            std::map<uint64_t, Waiting> waiting;
            std::unordered_set<DeferredResult *> deferred;
            uint64_t lastId = 0;
        };

        // Isolates may be set up on different threads.
        std::mutex &RuntimesMutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<v8::Isolate *, std::shared_ptr<Runtime>> &Runtimes() {
            static std::unordered_map<v8::Isolate *, std::shared_ptr<Runtime>> runtimes;
            return runtimes;
        }

        std::shared_ptr<Runtime> FindRuntime(v8::Isolate *isolate) {
            std::scoped_lock lock(RuntimesMutex());
            const auto found = Runtimes().find(isolate);
            return found != Runtimes().end() ? found->second : nullptr;
        }

        std::shared_ptr<Runtime> RuntimeOf(v8::Isolate *isolate) {
            std::scoped_lock lock(RuntimesMutex());
            std::shared_ptr<Runtime> &runtime = Runtimes()[isolate];
            if (runtime == nullptr) {
                runtime          = std::make_shared<Runtime>();
                runtime->isolate = isolate;
            }
            return runtime;
        }

        std::shared_ptr<Runtime> TakeRuntime(v8::Isolate *isolate) {
            std::scoped_lock lock(RuntimesMutex());
            const auto node = Runtimes().extract(isolate);
            return node.empty() ? nullptr : node.mapped();
        }

        v8::Local<v8::Private> CallableKey(v8::Isolate *isolate) {
            return v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.callable"));
        }

        v8::Local<v8::Private> StandInKey(v8::Isolate *isolate) {
            return v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.callableStandIn"));
        }

        // The id `function` carries under `key`, or 0.
        uint64_t IdOf(v8::Local<v8::Context> context, v8::Local<v8::Function> function, v8::Local<v8::Private> key) {
            v8::Local<v8::Value> id;
            return function->GetPrivate(context, key).ToLocal(&id) && id->IsNumber() ? static_cast<uint64_t>(id.As<v8::Number>()->Value()) : 0;
        }

        // Enters an isolate from outside its JavaScript for the lifetime of the object, as EngineScope does: the
        // scopes in the order V8 requires, then the callback scope of the runtime, so that what the stay queued
        // there runs as the object goes.
        struct Entered final {
            Entered(v8::Isolate *isolate, const v8::Global<v8::Context> &held): locker(isolate), isolateScope(isolate), handleScope(isolate), context(held.Get(isolate)), contextScope(context), callbackScope(OpenCallbackScope(isolate)) {}

            v8::Locker locker;
            v8::Isolate::Scope isolateScope;
            v8::HandleScope handleScope;
            v8::Local<v8::Context> context;
            v8::Context::Scope contextScope;
            std::unique_ptr<CallbackScope> callbackScope;
        };

        CallOutcome Failed(std::string error) {
            CallOutcome outcome;
            outcome.threw = true;
            outcome.error = std::move(error);
            return outcome;
        }

        // What a call that returned `value` tells its caller.
        CallOutcome CopyResult(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value, ReturnMode mode) {
            CallOutcome outcome;
            if (mode == ReturnMode::Outcome) {
                if (value->IsBoolean()) {
                    outcome.value.kind    = TransferredValue::Kind::Boolean;
                    outcome.value.boolean = value->IsTrue();
                }
                return outcome;
            }
            auto copied = ValueTransfer::Copy(isolate, context, value, V8CallableTransfer());
            if (!copied) {
                return Failed(copied.GetError());
            }
            outcome.value = std::move(copied).GetValue();
            return outcome;
        }

        // What a promise was rejected with, as text: the stack of an error, which starts with its message, and
        // anything else as String() spells it.
        std::string DescribeRejection(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> reason) {
            // Reading the stack may run a getter of the script.
            v8::TryCatch tryCatch(isolate);
            if (reason->IsNativeError()) {
                v8::Local<v8::Value> stack;
                if (reason.As<v8::Object>()->Get(context, v8::String::NewFromUtf8Literal(isolate, "stack")).ToLocal(&stack) && stack->IsString() && stack.As<v8::String>()->Length() > 0) {
                    return ToUtf8(isolate, stack);
                }
            }
            std::string text = ToUtf8(isolate, reason);
            return text.empty() ? std::string("The promise was rejected") : text;
        }

        // Runs inside the isolate of the function, from its microtask queue.
        void Settle(const v8::FunctionCallbackInfo<v8::Value> &info, bool fulfilled) {
            v8::Isolate *isolate                   = info.GetIsolate();
            const std::shared_ptr<Runtime> runtime = FindRuntime(isolate);
            if (runtime == nullptr) {
                return;
            }
            const auto found = runtime->waiting.find(info.Data().As<v8::BigInt>()->Uint64Value());
            if (found == runtime->waiting.end()) {
                return;
            }
            // Taken out first: `done` may invoke callables and destroy runtimes.
            Waiting waiting = std::move(found->second);
            runtime->waiting.erase(found);

            v8::HandleScope scope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Value> value     = info.Length() > 0 ? info[0] : v8::Undefined(isolate).As<v8::Value>();
            waiting.done(fulfilled ? CopyResult(isolate, context, value, waiting.mode) : Failed(DescribeRejection(isolate, context, value)));
        }

        void OnFulfilled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Settle(info, true);
        }

        void OnRejected(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Settle(info, false);
        }

        class Callable final: public V8Callable {
          public:
            Callable(std::shared_ptr<Runtime> runtime, uint64_t id, v8::Local<v8::Context> context, v8::Local<v8::Function> function, std::string resource): _runtime(std::move(runtime)), _id(id), _resource(std::move(resource)) {
                _context.Reset(_runtime->isolate, context);
                _function.Reset(_runtime->isolate, function);
            }

            // The handles go with it while the isolate lives; Release took them otherwise.
            ~Callable() override {
                _runtime->callables.erase(_id);
            }

            // The isolate is going: nothing is left to call.
            void Release() {
                _function.Reset();
                _context.Reset();
            }

            Language GetLanguage() const override {
                return Language::JavaScript;
            }

            std::string ResourceName() const override {
                return _resource;
            }

            bool IsAlive() const override {
                return _runtime->isolate != nullptr;
            }

            bool Equals(const ScriptCallable &other) const override {
                if (this == &other) {
                    return true;
                }
                const auto *that = dynamic_cast<const Callable *>(&other);
                return that != nullptr && that->_runtime == _runtime && !_function.IsEmpty() && _function == that->_function;
            }

            v8::Isolate *Isolate() const override {
                return _runtime->isolate;
            }

            v8::Local<v8::Function> Function() const override {
                return _function.IsEmpty() ? v8::Local<v8::Function>() : _function.Get(_runtime->isolate);
            }

            void Invoke(const std::vector<TransferredValue> &args, ReturnMode mode, Completion done) override {
                if (!done) {
                    done = [](CallOutcome &&) {};
                }
                // Held: the function, and `done`, may drop the callable.
                const std::shared_ptr<Runtime> runtime = _runtime;
                v8::Isolate *isolate                   = runtime->isolate;
                if (isolate == nullptr) {
                    done(Failed(kRuntimeStopped));
                    return;
                }

                CallOutcome outcome;
                bool waits = false;
                {
                    ResourceMonitor::Scope charged(isolate);
                    Entered entered(isolate, _context);
                    v8::TryCatch tryCatch(isolate);

                    std::vector<v8::Local<v8::Value>> arguments;
                    arguments.reserve(args.size());
                    for (const TransferredValue &argument : args) {
                        v8::Local<v8::Value> rebuilt;
                        if (!ValueTransfer::Rebuild(isolate, entered.context, argument).ToLocal(&rebuilt)) {
                            break;
                        }
                        arguments.push_back(rebuilt);
                    }

                    v8::Local<v8::Value> value;
                    if (arguments.size() != args.size() || !_function.Get(isolate)->Call(entered.context, v8::Undefined(isolate), static_cast<int>(arguments.size()), arguments.data()).ToLocal(&value)) {
                        // Terminated (the script called process.exit(), say): there is no exception to spell.
                        outcome = Failed(tryCatch.HasCaught() && !tryCatch.HasTerminated() ? FormatV8Exception(isolate, tryCatch, "The call failed") : std::string(kRuntimeStopped));
                    }
                    else if (!value->IsPromise()) {
                        outcome = CopyResult(isolate, entered.context, value, mode);
                    }
                    else if (runtime->isolate == nullptr) {
                        // The function had what is kept for its isolate dropped: nothing would hear of the promise.
                        outcome = Failed(kRuntimeStopped);
                    }
                    else {
                        const uint64_t key        = ++runtime->lastId;
                        v8::Local<v8::Value> data = v8::BigInt::NewFromUnsigned(isolate, key);
                        v8::Local<v8::Function> onFulfilled;
                        v8::Local<v8::Function> onRejected;
                        if (v8::Function::New(entered.context, OnFulfilled, data).ToLocal(&onFulfilled) && v8::Function::New(entered.context, OnRejected, data).ToLocal(&onRejected) && !value.As<v8::Promise>()->Then(entered.context, onFulfilled, onRejected).IsEmpty()) {
                            runtime->waiting.emplace(key, Waiting {std::move(done), mode});
                            waits = true;
                        }
                        else {
                            outcome = Failed(kRuntimeStopped);
                        }
                    }
                    // The queues of the runtime drain as it is left: a promise that is settled by then tells
                    // `done` here.
                }
                if (!waits) {
                    done(std::move(outcome));
                }
            }

          private:
            const std::shared_ptr<Runtime> _runtime;
            const uint64_t _id;
            const std::string _resource;
            v8::Global<v8::Context> _context;
            v8::Global<v8::Function> _function;
        };

        class DeferredResult final: public Detail::V8Deferred {
          public:
            DeferredResult(std::shared_ptr<Runtime> runtime, v8::Local<v8::Context> context, v8::Local<v8::Promise::Resolver> resolver): _runtime(std::move(runtime)) {
                _context.Reset(_runtime->isolate, context);
                _resolver.Reset(_runtime->isolate, resolver);
                _runtime->deferred.insert(this);
            }

            ~DeferredResult() override {
                _runtime->deferred.erase(this);
            }

            void Returned(v8::Isolate *isolate, v8::Local<v8::Context> context) override {
                _returned = true;
                if (Settled() && !_resolver.IsEmpty()) {
                    SettleIn(isolate, context, _rejected ? nullptr : &_value, _message);
                    _value = {};
                }
            }

            void Drop() override {
                _resolver.Reset();
                _context.Reset();
            }

          protected:
            void OnResolve(TransferredValue &&value) override {
                if (!_returned) {
                    _value = std::move(value);
                    return;
                }
                Settle(&value, {});
            }

            void OnReject(std::string &&message) override {
                if (!_returned) {
                    _rejected = true;
                    _message  = std::move(message);
                    return;
                }
                Settle(nullptr, message);
            }

          private:
            // Settles the promise from outside the JavaScript of its runtime, which is entered for it.
            void Settle(const TransferredValue *value, const std::string &message) {
                v8::Isolate *isolate = _runtime->isolate;
                if (isolate == nullptr || _resolver.IsEmpty()) {
                    return;
                }
                ResourceMonitor::Scope charged(isolate);
                Entered entered(isolate, _context);
                SettleIn(isolate, entered.context, value, message);
            }

            // Fulfils the promise with `value`, or rejects it with `message` when there is none. The caller holds
            // the scopes of the isolate.
            void SettleIn(v8::Isolate *isolate, v8::Local<v8::Context> context, const TransferredValue *value, const std::string &message) {
                v8::Local<v8::Promise::Resolver> resolver = _resolver.Get(isolate);
                Drop();

                v8::TryCatch tryCatch(isolate);
                v8::Local<v8::Value> settled;
                if (value == nullptr) {
                    settled = v8::Exception::Error(v8pp::to_v8(isolate, message));
                }
                else if (!ValueTransfer::Rebuild(isolate, context, *value).ToLocal(&settled)) {
                    // What the runtime cannot be given is what the promise is rejected with.
                    if (!tryCatch.HasCaught() || tryCatch.HasTerminated()) {
                        return;
                    }
                    settled = tryCatch.Exception();
                    value   = nullptr;
                }
                if (value != nullptr) {
                    (void)resolver->Resolve(context, settled).FromMaybe(false);
                }
                else {
                    (void)resolver->Reject(context, settled).FromMaybe(false);
                }
            }

            const std::shared_ptr<Runtime> _runtime;
            v8::Global<v8::Context> _context;
            v8::Global<v8::Promise::Resolver> _resolver;

            // Whether the function has returned. Until then a settlement is kept, for the call to make as it
            // returns the promise: the script has it before anything it queued runs.
            bool _returned = false;
            bool _rejected = false;
            TransferredValue _value;
            std::string _message;
        };

        // The resource `function` belongs to, as Call::ResourceName resolves the one of a caller.
        std::string ResourceOf(v8::Isolate *isolate, v8::Local<v8::Function> function) {
            const ResourceManager *manager = Detail::CallResourceManager(isolate);
            return manager != nullptr ? manager->ResolveResourceContext(isolate, function) : std::string();
        }

        // Drops the stand-ins whose functions were collected, once their number has doubled since the last time.
        void SweepStandIns(Runtime &runtime) {
            if (runtime.standIns.size() < 2 * runtime.standInsAtSweep + 16) {
                return;
            }
            std::erase_if(runtime.standIns, [](const auto &standIn) {
                return standIn.second.expired();
            });
            runtime.standInsAtSweep = runtime.standIns.size();
        }
    } // namespace

    std::shared_ptr<ScriptCallable> V8ToCallable(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Function> function) {
        const std::shared_ptr<Runtime> runtime = RuntimeOf(isolate);

        // A function that stands for a callable is that callable to whoever keeps it or sends it on, so that it
        // comes home as itself and is not wrapped again on the way.
        if (const uint64_t standIn = IdOf(context, function, StandInKey(isolate))) {
            const auto found = runtime->standIns.find(standIn);
            if (found != runtime->standIns.end()) {
                if (std::shared_ptr<ScriptCallable> original = found->second.lock()) {
                    return original;
                }
            }
        }

        const v8::Local<v8::Private> key = CallableKey(isolate);
        if (const uint64_t id = IdOf(context, function, key)) {
            const auto found = runtime->callables.find(id);
            if (found != runtime->callables.end()) {
                if (std::shared_ptr<Callable> existing = found->second.lock()) {
                    return existing;
                }
            }
        }

        const uint64_t id = ++runtime->lastId;
        auto callable     = std::make_shared<Callable>(runtime, id, context, function, ResourceOf(isolate, function));
        // A function that takes no private property gets a callable each time, which Equals still tells are one.
        (void)function->SetPrivate(context, key, v8::Number::New(isolate, static_cast<double>(id))).FromMaybe(false);
        runtime->callables[id] = callable;
        return callable;
    }

    v8::MaybeLocal<v8::Function> V8FunctionOf(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::shared_ptr<ScriptCallable> &callable) {
        // A function coming home is itself.
        if (const auto *own = dynamic_cast<const Callable *>(callable.get()); own != nullptr && own->Isolate() == isolate) {
            return own->Function();
        }

        v8::Local<v8::Function> function;
        if (!Detail::V8NativeFunction(isolate, context, std::make_shared<NativeFunction>(Detail::CallableForwarder(callable))).ToLocal(&function)) {
            return {};
        }
        const std::shared_ptr<Runtime> runtime = RuntimeOf(isolate);
        const uint64_t id                      = ++runtime->lastId;
        if (function->SetPrivate(context, StandInKey(isolate), v8::Number::New(isolate, static_cast<double>(id))).FromMaybe(false)) {
            SweepStandIns(*runtime);
            runtime->standIns[id] = callable;
        }
        return function;
    }

    const TransferFunctions &V8CallableTransfer() {
        static const TransferFunctions functions = [] {
            TransferFunctions made;
            made.exportFunction = [](v8::Isolate *isolate, v8::Local<v8::Function> function, v8::Local<v8::Object>, TransferredValue &out) {
                out.callable = V8ToCallable(isolate, isolate->GetCurrentContext(), function);
            };
            return made;
        }();
        return functions;
    }

    void V8RuntimeDisposing(v8::Isolate *isolate) {
        const std::shared_ptr<Runtime> runtime = TakeRuntime(isolate);
        if (runtime == nullptr) {
            return;
        }
        // Dead before anything is told: what `done` invokes in this runtime is answered at once.
        runtime->isolate = nullptr;

        // Taken out first, each of them: a callable or a deferred result that goes meanwhile takes itself out.
        const auto callables = std::move(runtime->callables);
        runtime->callables.clear();
        for (const auto &[id, weak] : callables) {
            if (const std::shared_ptr<Callable> callable = weak.lock()) {
                callable->Release();
            }
        }
        runtime->standIns.clear();

        const auto deferred = std::move(runtime->deferred);
        runtime->deferred.clear();
        for (DeferredResult *result : deferred) {
            result->Drop();
        }

        auto waiting = std::move(runtime->waiting);
        runtime->waiting.clear();
        for (auto &[key, call] : waiting) {
            call.done(Failed(kRuntimeStopped));
        }
    }

    void WatchRuntimes(Engine &engine) {
        engine.AddRuntimeDisposingListener([](v8::Isolate *isolate) {
            V8RuntimeDisposing(isolate);
        });
    }

    namespace Detail {
        std::shared_ptr<V8Deferred> V8Defer(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Promise> &promise) {
            v8::Local<v8::Promise::Resolver> resolver;
            if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
                return nullptr;
            }
            promise = resolver->GetPromise();
            return std::make_shared<DeferredResult>(RuntimeOf(isolate), context, resolver);
        }
    } // namespace Detail
} // namespace Framework::Scripting::Bind
