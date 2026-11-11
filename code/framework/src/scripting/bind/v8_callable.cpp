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

#include <atomic>
#include <map>
#include <mutex>
#include <optional>
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
// of another runtime carries an id that leads back to that callable. No id is given twice in the process, so one
// that outlived what it named names nothing.
//
// A runtime ends in two steps. Stopped, its isolate is alive and may be running the script that stopped it, but
// everything kept for it is dead and stays so: the record remains, and what is made in the isolate from then on is
// dead from the start. Disposed, the record goes.
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

        struct Runtime;

        // A function of an isolate that stands for a callable of another runtime. The function keeps the callable
        // alive, and this goes when the function is collected.
        struct StandIn final {
            Runtime *runtime = nullptr;
            uint64_t id      = 0;
            // What Runtime::standInOf knows it by: the address of the callable, which is good while the function
            // holds the callable and is not read once it does not.
            const ScriptCallable *key = nullptr;
            std::weak_ptr<ScriptCallable> callable;
            v8::Global<v8::Function> function; // weak
        };

        struct Runtime final {
            // nullptr once the isolate is disposed of.
            v8::Isolate *isolate = nullptr;
            // The runtime runs no more, though its isolate may still be there.
            bool stopped = false;
            // By the id the function carries. A callable takes itself out as it goes.
            std::unordered_map<uint64_t, std::weak_ptr<Callable>> callables;
            // By the id the function carries, and by the callable: a callable has one stand-in in an isolate
            // while that function lives, so it arrives as the same function each time.
            std::unordered_map<uint64_t, std::unique_ptr<StandIn>> standIns;
            std::unordered_map<const ScriptCallable *, StandIn *> standInOf;
            std::map<uint64_t, Waiting> waiting;
            std::unordered_set<DeferredResult *> deferred;

            // Whether script may still be run in it on the host's behalf.
            bool Alive() const {
                return isolate != nullptr && !stopped;
            }
        };

        // Of a callable, of a stand-in or of a call that waits: never 0, and never given twice.
        uint64_t NextId() {
            static std::atomic<uint64_t> last {0};
            return last.fetch_add(1, std::memory_order_relaxed) + 1;
        }

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

        // What a step into V8 that gave nothing tells the caller of an Invoke: `what`, then what the step threw, or
        // `otherwise` when it threw nothing. In an isolate that is being terminated, that the runtime has stopped.
        CallOutcome Unfinished(v8::Isolate *isolate, v8::TryCatch &tryCatch, const std::string &what, const char *otherwise) {
            if (tryCatch.HasTerminated() || isolate->IsExecutionTerminating()) {
                return Failed(kRuntimeStopped);
            }
            return Failed(what + (tryCatch.HasCaught() ? FormatV8Exception(isolate, tryCatch, otherwise) : std::string(otherwise)));
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
            // In a runtime that has stopped it is dead from the start, and holds nothing.
            Callable(std::shared_ptr<Runtime> runtime, uint64_t id, v8::Local<v8::Context> context, v8::Local<v8::Function> function, std::string resource): _runtime(std::move(runtime)), _id(id), _resource(std::move(resource)) {
                if (_runtime->Alive()) {
                    _context.Reset(_runtime->isolate, context);
                    _function.Reset(_runtime->isolate, function);
                }
            }

            // The handles go with it while the isolate lives; Release took them otherwise.
            ~Callable() override {
                _runtime->callables.erase(_id);
            }

            // The runtime has stopped: nothing is left to call.
            void Release() {
                _function.Reset();
                _context.Reset();
            }

            // The function, when `isolate` is the one that owns it and it is still held.
            v8::Local<v8::Function> FunctionIn(v8::Isolate *isolate) const {
                return _runtime->isolate == isolate ? Function() : v8::Local<v8::Function>();
            }

            Language GetLanguage() const override {
                return Language::JavaScript;
            }

            std::string ResourceName() const override {
                return _resource;
            }

            bool IsAlive() const override {
                return _runtime->Alive();
            }

            bool Equals(const ScriptCallable &other) const override {
                if (this == &other) {
                    return true;
                }
                const auto *that = dynamic_cast<const Callable *>(&other);
                return that != nullptr && that->_runtime == _runtime && !_function.IsEmpty() && _function == that->_function;
            }

            v8::Isolate *Isolate() const override {
                return _runtime->Alive() ? _runtime->isolate : nullptr;
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
                if (!runtime->Alive()) {
                    done(Failed(kRuntimeStopped));
                    return;
                }
                v8::Isolate *isolate = runtime->isolate;

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
                    if (arguments.size() != args.size()) {
                        outcome = Unfinished(isolate, tryCatch, "Argument " + std::to_string(arguments.size()) + ": ", "the value could not be built");
                    }
                    else if (!_function.Get(isolate)->Call(entered.context, v8::Undefined(isolate), static_cast<int>(arguments.size()), arguments.data()).ToLocal(&value)) {
                        outcome = Unfinished(isolate, tryCatch, {}, "The call failed");
                    }
                    else if (!value->IsPromise()) {
                        outcome = CopyResult(isolate, entered.context, value, mode);
                    }
                    else if (!runtime->Alive()) {
                        // The function stopped its own runtime: nothing would hear of the promise.
                        outcome = Failed(kRuntimeStopped);
                    }
                    else {
                        // Waited on before the reactions exist, so that no reaction can come to find nobody
                        // waiting, however the isolate runs its microtasks.
                        const uint64_t key = NextId();
                        runtime->waiting.emplace(key, Waiting {std::move(done), mode});
                        waits = true;

                        v8::Local<v8::Value> data = v8::BigInt::NewFromUnsigned(isolate, key);
                        v8::Local<v8::Function> onFulfilled;
                        v8::Local<v8::Function> onRejected;
                        if (!v8::Function::New(entered.context, OnFulfilled, data).ToLocal(&onFulfilled) || !v8::Function::New(entered.context, OnRejected, data).ToLocal(&onRejected) || value.As<v8::Promise>()->Then(entered.context, onFulfilled, onRejected).IsEmpty()) {
                            // Nothing will come for it, unless the runtime stopped meanwhile and said so.
                            const auto unheard = runtime->waiting.find(key);
                            if (unheard != runtime->waiting.end()) {
                                done = std::move(unheard->second.done);
                                runtime->waiting.erase(unheard);
                                waits   = false;
                                outcome = Unfinished(isolate, tryCatch, {}, "The promise of the function could not be waited on");
                            }
                        }
                    }
                    // The queues of a Node runtime drain as it is left: a promise that is settled by then tells
                    // `done` here. A bare isolate has none: its promise is told when it next runs its microtasks.
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
                // In a runtime that has stopped nothing will settle it later.
                if (!_runtime->Alive()) {
                    Drop();
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
                if (!_runtime->Alive() || _resolver.IsEmpty()) {
                    return;
                }
                v8::Isolate *isolate = _runtime->isolate;
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

        // The function a stand-in was is being collected. A first-pass weak callback: V8 allows nothing of its own
        // in it but resetting the handle, and nothing else here touches V8. The function held the callable, which
        // goes with it.
        void OnStandInCollected(const v8::WeakCallbackInfo<StandIn> &info) {
            StandIn *standIn = info.GetParameter();
            standIn->function.Reset();
            Runtime &runtime = *standIn->runtime;
            if (const auto known = runtime.standInOf.find(standIn->key); known != runtime.standInOf.end() && known->second == standIn) {
                runtime.standInOf.erase(known);
            }
            runtime.standIns.erase(standIn->id);
        }

        // Everything kept for the runtime is dead from here on. The callables stay known, so that a function is
        // still found to have the one it had.
        void Stop(const std::shared_ptr<Runtime> &runtime) {
            // Dead before anything is told: what `done` invokes in this runtime is answered at once.
            runtime->stopped = true;

            // Held while they let go: one that goes meanwhile takes itself out of the table.
            std::vector<std::shared_ptr<Callable>> callables;
            callables.reserve(runtime->callables.size());
            for (const auto &[id, weak] : runtime->callables) {
                if (std::shared_ptr<Callable> callable = weak.lock()) {
                    callables.push_back(std::move(callable));
                }
            }
            for (const std::shared_ptr<Callable> &callable : callables) {
                callable->Release();
            }
            runtime->standInOf.clear();
            runtime->standIns.clear();

            const std::vector<DeferredResult *> deferred(runtime->deferred.begin(), runtime->deferred.end());
            for (DeferredResult *result : deferred) {
                result->Drop();
            }

            auto waiting = std::move(runtime->waiting);
            runtime->waiting.clear();
            for (auto &[key, call] : waiting) {
                call.done(Failed(kRuntimeStopped));
            }
        }
    } // namespace

    std::shared_ptr<ScriptCallable> V8ToCallable(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Function> function) {
        const std::shared_ptr<Runtime> runtime = RuntimeOf(isolate);

        // A function that stands for a callable is that callable to whoever keeps it or sends it on, so that it
        // comes home as itself and is not wrapped again on the way.
        if (const uint64_t standIn = IdOf(context, function, StandInKey(isolate))) {
            const auto found = runtime->standIns.find(standIn);
            if (found != runtime->standIns.end()) {
                if (std::shared_ptr<ScriptCallable> original = found->second->callable.lock()) {
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

        const uint64_t id = NextId();
        auto callable     = std::make_shared<Callable>(runtime, id, context, function, ResourceOf(isolate, function));
        // A function that takes no private property gets a callable each time, which Equals still tells are one.
        (void)function->SetPrivate(context, key, v8::Number::New(isolate, static_cast<double>(id))).FromMaybe(false);
        runtime->callables[id] = callable;
        return callable;
    }

    v8::MaybeLocal<v8::Function> V8FunctionOf(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::shared_ptr<ScriptCallable> &callable) {
        // A function coming home is itself.
        if (const auto *own = dynamic_cast<const Callable *>(callable.get())) {
            const v8::Local<v8::Function> original = own->FunctionIn(isolate);
            if (!original.IsEmpty()) {
                return original;
            }
        }

        // The one that stands for it here already. Nothing of the tables is held across a call into V8, which may
        // collect, and so take, any of the others.
        const std::shared_ptr<Runtime> runtime = RuntimeOf(isolate);
        if (const auto known = runtime->standInOf.find(callable.get()); known != runtime->standInOf.end() && !known->second->function.IsEmpty()) {
            return known->second->function.Get(isolate);
        }

        v8::Local<v8::Function> function;
        if (!Detail::V8NativeFunction(isolate, context, std::make_shared<NativeFunction>(Detail::CallableForwarder(callable))).ToLocal(&function)) {
            return {};
        }
        // A function that takes no private property stands for the callable all the same, each time anew.
        const uint64_t id = NextId();
        if (!function->SetPrivate(context, StandInKey(isolate), v8::Number::New(isolate, static_cast<double>(id))).FromMaybe(false)) {
            return function;
        }
        auto standIn      = std::make_unique<StandIn>();
        standIn->runtime  = runtime.get();
        standIn->id       = id;
        standIn->key      = callable.get();
        standIn->callable = callable;
        standIn->function.Reset(isolate, function);
        standIn->function.SetWeak(standIn.get(), OnStandInCollected, v8::WeakCallbackType::kParameter);
        runtime->standInOf[callable.get()] = standIn.get();
        runtime->standIns.emplace(id, std::move(standIn));
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

    void V8RuntimeStopped(v8::Isolate *isolate) {
        // Known from here on even if nothing was kept for it yet: what is made in it later is dead too.
        const std::shared_ptr<Runtime> runtime = RuntimeOf(isolate);
        if (!runtime->stopped) {
            Stop(runtime);
        }
    }

    void V8RuntimeDisposing(v8::Isolate *isolate) {
        const std::shared_ptr<Runtime> runtime = TakeRuntime(isolate);
        if (runtime == nullptr) {
            return;
        }
        // Again for one that had stopped: for what was made in the isolate since.
        Stop(runtime);
        runtime->callables.clear();
        runtime->isolate = nullptr;
    }

    size_t V8StandInCount(v8::Isolate *isolate) {
        const std::shared_ptr<Runtime> runtime = FindRuntime(isolate);
        return runtime != nullptr ? runtime->standIns.size() : 0;
    }

    void WatchRuntimes(Engine &engine) {
        engine.AddRuntimeStoppedListener([](v8::Isolate *isolate) {
            V8RuntimeStopped(isolate);
        });
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
