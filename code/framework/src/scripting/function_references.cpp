/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "function_references.h"

#include "callback_scope.h"
#include "engine_helpers.h"
#include "resource_monitor.h"
#include "script_gate.h"

#include <utils/profiler.h>

#include <v8pp/convert.hpp>

#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Framework::Scripting {

    struct FunctionReferences::State: std::enable_shared_from_this<FunctionReferences::State> {
        // An export's handles, in its owner's isolate. Touched only by a thread that holds that isolate: a call that
        // is on its way to the owner shares them instead of making handles of its own from outside, and whoever
        // releases the export from outside leaves them to the owner's next visit.
        struct Handles {
            v8::Global<v8::Function> function;
            v8::Global<v8::Object> receiver;

            void Reset() {
                function.Reset();
                receiver.Reset();
            }
        };

        struct Runtime {
            v8::Isolate *isolate = nullptr;
            v8::Global<v8::Context> context;
            std::string name;
            std::shared_ptr<const TransferFunctions> hooks; // Built once: every call copies through them.
            // Handles of exports released by a thread that did not hold this isolate, for one that does to drop.
            std::vector<std::shared_ptr<Handles>> retired;
        };

        // A function kept alive for other runtimes to call. `holds` counts the copies and stand-ins that refer to it.
        // `receiver` is the object it was sent as a method of, which it is called on: a method exported on an object
        // keeps its `this`.
        struct Export {
            uint32_t runtime = 0;
            std::shared_ptr<Handles> handles;
            std::string name; // the function's own: a function crosses as a value, and the monitor's slow reports need one
            uint32_t holds = 0;
        };

        // A callable in one runtime that forwards to an export of another. Collected with its JS function.
        struct StandIn {
            State *state     = nullptr;
            uint64_t key     = 0;
            uint64_t id      = 0;
            uint32_t runtime = 0;
            v8::Global<v8::Function> handle;
        };

        // A promise returned to a caller in another runtime, waiting on the owner's promise. Owned here until the
        // owner's promise settles or the owner goes away; the owner's reaction functions name it by key.
        struct Settlement {
            uint64_t key    = 0;
            uint32_t owner  = 0;
            uint32_t caller = 0;
            bool keepValue  = true;
            v8::Global<v8::Promise::Resolver> resolver;

            // Set once the owner's promise has settled: how, and with what, copied out of the owner. The owner's queue
            // drains as the call leaves it, so a promise already resolved by then settles before the caller's promise
            // exists; the outcome waits here for Invoke to make one.
            bool settled   = false;
            bool fulfilled = false;
            TransferredValue outcome;
        };

        // Keeps an export alive for as long as a copy holding it exists.
        struct Hold {
            std::weak_ptr<State> state;
            uint64_t id = 0;

            ~Hold() {
                if (auto locked = state.lock()) {
                    locked->Release(id);
                }
            }
        };

        // Enters a runtime for the lifetime of the object, in the order V8 requires, inside its callback scope: what
        // the stay queues there runs as the object goes, before the code that entered carries on.
        struct Entered {
            explicit Entered(Runtime &runtime): locker(runtime.isolate), isolateScope(runtime.isolate), handleScope(runtime.isolate), context(runtime.context.Get(runtime.isolate)), contextScope(context), callbackScope(OpenCallbackScope(runtime.isolate)) {}

            // First in, last out: a call from a script into another runtime holds safe points off for its length.
            RuntimeEntry entry;
            v8::Locker locker;
            v8::Isolate::Scope isolateScope;
            v8::HandleScope handleScope;
            v8::Local<v8::Context> context;
            v8::Context::Scope contextScope;
            std::unique_ptr<CallbackScope> callbackScope;
        };

        // Guards every table below, and what their entries hold, against the threads that copy arguments at the
        // same time: the script thread and each thread with a runtime of its own. Never held while script runs
        // (a copy runs getters, a call runs anything) nor while an isolate is waited for: what a step needs is
        // read out under it, and the step is taken without it. Whoever enters a runtime found here relies on it
        // staying: runtimes come and go only on the script thread or with the world, which is where calls are made.
        mutable std::mutex mutex;
        // Weak callbacks run inside a collection, which any V8 call under `mutex` could start: a lock of their own.
        std::mutex collectedMutex;

        std::map<uint32_t, Runtime> runtimes;
        std::unordered_map<v8::Isolate *, uint32_t> runtimeByIsolate;
        std::map<uint64_t, Export> exports;
        std::unordered_map<uint64_t, std::unique_ptr<StandIn>> standIns;
        // The stand-in each runtime holds for an export, keyed by (runtime, export id).
        std::map<std::pair<uint32_t, uint64_t>, uint64_t> standInByExport;
        // Stand-ins whose functions were collected, waiting for ReleaseCollected().
        std::vector<uint64_t> collected;
        std::map<uint64_t, std::unique_ptr<Settlement>> settlements;
        uint32_t nextRuntime = 1;
        uint64_t nextExport  = 1;
        std::atomic<uint64_t> nextStandIn {1};
        std::atomic<uint64_t> nextSettlement {1};

        // What a V8 callback (a stand-in, a promise reaction) is handed: this state and the key of its target. A key, not
        // a pointer: the target can go while the function still exists, such as when its runtime has left but a
        // disposing listener still runs code in the isolate, so the callback looks it up and finds it gone.
        // The state itself outlives every runtime that takes part, so the pointer to it stays valid.
        v8::Local<v8::Value> CallbackData(v8::Isolate *isolate, uint64_t key) {
            v8::Local<v8::Value> parts[] = {v8::External::New(isolate, this), v8::BigInt::NewFromUnsigned(isolate, key)};
            return v8::Array::New(isolate, parts, 2);
        }

        static std::pair<State *, uint64_t> ReadCallbackData(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
            v8::Local<v8::Array> parts     = info.Data().As<v8::Array>();
            auto *state                    = static_cast<State *>(parts->Get(context, 0).ToLocalChecked().As<v8::External>()->Value());
            const uint64_t key             = parts->Get(context, 1).ToLocalChecked().As<v8::BigInt>()->Uint64Value();
            return {state, key};
        }

        static void Throw(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, message)));
        }

        std::string NameOf(uint32_t runtime) const {
            const std::lock_guard lock(mutex);
            const auto it = runtimes.find(runtime);
            return it != runtimes.end() ? it->second.name : std::string("a stopped resource");
        }

        uint32_t FindRuntime(v8::Isolate *isolate) const {
            const std::lock_guard lock(mutex);
            const auto it = runtimeByIsolate.find(isolate);
            return it != runtimeByIsolate.end() ? it->second : 0;
        }

        // The runtime's hooks, shared: a caller keeps them even if the runtime leaves during the copy they serve.
        std::shared_ptr<const TransferFunctions> Hooks(uint32_t runtime) {
            {
                const std::lock_guard lock(mutex);
                const auto it = runtimes.find(runtime);
                if (it != runtimes.end() && it->second.hooks) {
                    return it->second.hooks;
                }
            }
            return std::make_shared<const TransferFunctions>(MakeHooks(runtime));
        }

        TransferFunctions MakeHooks(uint32_t runtime) {
            TransferFunctions functions;
            std::weak_ptr<State> weak = weak_from_this();
            functions.exportFunction  = [weak, runtime](v8::Isolate *isolate, v8::Local<v8::Function> function, v8::Local<v8::Object> holder, TransferredValue &out) {
                if (auto state = weak.lock()) {
                    state->ExportFunction(runtime, isolate, function, holder, out);
                }
            };
            functions.importFunction = [weak, runtime](v8::Isolate *isolate, v8::Local<v8::Context> context, uint64_t id) -> v8::MaybeLocal<v8::Function> {
                if (auto state = weak.lock()) {
                    return state->ImportFunction(runtime, isolate, context, id);
                }
                Throw(isolate, "Scripting is shutting down");
                return {};
            };
            return functions;
        }

        // Drops the handles other threads left for `runtime`, whose isolate the caller holds.
        void DropRetired(uint32_t runtime) {
            std::vector<std::shared_ptr<Handles>> retired;
            {
                const std::lock_guard lock(mutex);
                const auto it = runtimes.find(runtime);
                if (it == runtimes.end() || it->second.retired.empty()) {
                    return;
                }
                retired.swap(it->second.retired);
            }
            for (const auto &handles : retired) {
                handles->Reset();
            }
        }

        // One more hold on a live export, or false. Looking an export up and holding it are one step: another
        // thread may be releasing it.
        bool HoldExport(uint64_t id) {
            const std::lock_guard lock(mutex);
            const auto exported = exports.find(id);
            if (exported == exports.end()) {
                return false;
            }
            ++exported->second.holds;
            return true;
        }

        void ExportFunction(uint32_t runtime, v8::Isolate *isolate, v8::Local<v8::Function> function, v8::Local<v8::Object> holder, TransferredValue &out) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Private> key     = v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.functionReference"));
            DropRetired(runtime);

            // A stand-in carries the id of the export it forwards to: sending it on refers to the original instead of
            // stacking a stand-in on a stand-in.
            uint64_t id = 0;
            v8::Local<v8::Value> previous;
            if (function->GetPrivate(context, key).ToLocal(&previous) && previous->IsNumber()) {
                const auto existing = static_cast<uint64_t>(previous.As<v8::Number>()->Value());
                const std::lock_guard lock(mutex);
                const auto exported = exports.find(existing);
                if (exported != exports.end() && (exported->second.runtime != runtime || holder.IsEmpty())) {
                    id = existing;
                    ++exported->second.holds;
                }
            }

            // A method is exported once per object it is a method of, so each copy calls it on its own object. Sent
            // again, the same pair reuses its entry, so a handler sent on every event costs one.
            v8::Local<v8::Map> methods;
            if (id == 0 && !holder.IsEmpty()) {
                methods               = MethodsOf(isolate, context, holder);
                const uint64_t method = MethodExport(context, methods, function);
                if (method != 0 && HoldExport(method)) {
                    id = method;
                }
            }

            if (id == 0) {
                // Everything that asks V8 comes first; the table only takes the result.
                std::string name = DebugName(isolate, context, function);
                auto handles     = std::make_shared<Handles>();
                handles->function.Reset(isolate, function);
                if (!methods.IsEmpty()) {
                    // Weak: the method needs its object only while the owner keeps it.
                    handles->receiver.Reset(isolate, holder);
                    handles->receiver.SetWeak();
                }
                {
                    const std::lock_guard lock(mutex);
                    id            = nextExport++;
                    Export &entry = exports[id];
                    entry.runtime = runtime;
                    entry.handles = std::move(handles);
                    entry.name    = std::move(name);
                    entry.holds   = 1;
                }
                v8::Local<v8::Value> idValue = v8::Number::New(isolate, static_cast<double>(id));
                if (!methods.IsEmpty()) {
                    (void)methods->Set(context, function, idValue);
                }
                else {
                    function->SetPrivate(context, key, idValue).Check();
                }
            }

            // Built in place: a temporary Hold would release the export as it went out of scope.
            auto hold     = std::make_shared<Hold>();
            hold->state   = weak_from_this();
            hold->id      = id;
            out.reference = id;
            out.retainer  = std::move(hold);
        }

        // The name a slow call reports the function by: its own, or the one Exports.register() gave it when it has none
        // of its own, as an arrow function passed straight to register() has not.
        static std::string DebugName(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Function> function) {
            std::string name = ToUtf8(isolate, function->GetDebugName());
            v8::Local<v8::Value> registered;
            if (name.empty() && function->GetPrivate(context, ExportNameKey(isolate)).ToLocal(&registered) && registered->IsString()) {
                name = ToUtf8(isolate, registered);
            }
            return name;
        }

        static v8::Local<v8::Private> ExportNameKey(v8::Isolate *isolate) {
            return v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.exportName"));
        }

        // The table of methods an object has had exported, function to export id, kept on the object itself. Empty
        // when the object takes no private properties.
        static v8::Local<v8::Map> MethodsOf(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> holder) {
            v8::Local<v8::Private> key = v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.methodReferences"));
            v8::Local<v8::Value> existing;
            if (holder->GetPrivate(context, key).ToLocal(&existing) && existing->IsMap()) {
                return existing.As<v8::Map>();
            }
            v8::Local<v8::Map> methods = v8::Map::New(isolate);
            if (!holder->SetPrivate(context, key, methods).FromMaybe(false)) {
                return {};
            }
            return methods;
        }

        // The export `function` was given as a method in `methods`, which may have been released since, or 0.
        static uint64_t MethodExport(v8::Local<v8::Context> context, v8::Local<v8::Map> methods, v8::Local<v8::Function> function) {
            v8::Local<v8::Value> existing;
            if (methods.IsEmpty() || !methods->Get(context, function).ToLocal(&existing) || !existing->IsNumber()) {
                return 0;
            }
            return static_cast<uint64_t>(existing.As<v8::Number>()->Value());
        }

        void Release(uint64_t id) {
            const std::lock_guard lock(mutex);
            ReleaseLocked(id);
        }

        // With the mutex held. The last hold frees the export; its handles go now when this thread holds the owner's
        // isolate, and wait for a thread that does otherwise.
        void ReleaseLocked(uint64_t id) {
            const auto it = exports.find(id);
            if (it == exports.end() || --it->second.holds != 0) {
                return;
            }
            std::shared_ptr<Handles> handles = std::move(it->second.handles);
            const auto owner                 = runtimes.find(it->second.runtime);
            exports.erase(it);
            if (owner == runtimes.end()) {
                return;
            }
            if (v8::Locker::IsLocked(owner->second.isolate)) {
                handles->Reset();
            }
            else {
                owner->second.retired.push_back(std::move(handles));
            }
        }

        v8::MaybeLocal<v8::Function> ImportFunction(uint32_t runtime, v8::Isolate *isolate, v8::Local<v8::Context> context, uint64_t id) {
            DropRetired(runtime);

            v8::Local<v8::Function> known;
            bool live = false;
            {
                const std::lock_guard lock(mutex);
                const auto it = exports.find(id);
                if (it != exports.end()) {
                    live = true;
                    // A function coming home is itself, not a stand-in for itself.
                    if (it->second.runtime == runtime) {
                        known = it->second.handles->function.Get(isolate);
                    }
                    else {
                        // The runtime already holds a stand-in for it: the same function arrives as the same stand-in.
                        const auto cached = standInByExport.find({runtime, id});
                        if (cached != standInByExport.end()) {
                            const auto existing = standIns.find(cached->second);
                            if (existing != standIns.end() && !existing->second->handle.IsEmpty()) {
                                known = existing->second->handle.Get(isolate);
                            }
                        }
                    }
                }
            }
            if (!live) {
                Throw(isolate, "The function belongs to a resource that has stopped");
                return {};
            }
            if (!known.IsEmpty()) {
                return known;
            }

            auto standIn     = std::make_unique<StandIn>();
            standIn->state   = this;
            standIn->key     = nextStandIn++;
            standIn->id      = id;
            standIn->runtime = runtime;

            v8::Local<v8::Function> function;
            if (!v8::Function::New(context, CallStandIn, CallbackData(isolate, standIn->key)).ToLocal(&function)) {
                return {};
            }
            v8::Local<v8::Private> key = v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.functionReference"));
            function->SetPrivate(context, key, v8::Number::New(isolate, static_cast<double>(id))).Check();
            standIn->handle.Reset(isolate, function);
            standIn->handle.SetWeak(standIn.get(), OnStandInCollected, v8::WeakCallbackType::kParameter);
            {
                const std::lock_guard lock(mutex);
                const auto it = exports.find(id);
                if (it != exports.end()) {
                    ++it->second.holds;
                    standInByExport[{runtime, id}] = standIn->key;
                    const uint64_t standInKey      = standIn->key;
                    standIns.emplace(standInKey, std::move(standIn));
                }
            }
            if (standIn) {
                // Released while the stand-in was being made.
                standIn->handle.Reset();
                Throw(isolate, "The function belongs to a resource that has stopped");
                return {};
            }
            return function;
        }

        // A first-pass weak callback: V8 allows nothing in it but resetting the handle. Releasing the export destroys a
        // handle of another isolate, so it waits for ReleaseCollected().
        static void OnStandInCollected(const v8::WeakCallbackInfo<StandIn> &info) {
            StandIn *standIn = info.GetParameter();
            standIn->handle.Reset();
            const std::lock_guard lock(standIn->state->collectedMutex);
            standIn->state->collected.push_back(standIn->key);
        }

        void ReleaseCollected() {
            std::vector<uint64_t> keys;
            {
                const std::lock_guard lock(collectedMutex);
                keys.swap(collected);
            }
            const std::lock_guard lock(mutex);
            for (const uint64_t key : keys) {
                const auto it = standIns.find(key);
                if (it == standIns.end()) {
                    continue; // Its runtime left first and released it then.
                }
                const auto cached = standInByExport.find({it->second->runtime, it->second->id});
                if (cached != standInByExport.end() && cached->second == key) {
                    standInByExport.erase(cached);
                }
                ReleaseLocked(it->second->id);
                standIns.erase(it);
            }
        }

        // Runs in the caller's runtime: forward the call to the export's owner.
        static void CallStandIn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto [statePointer, key] = ReadCallbackData(info);
            State &state             = *statePointer;
            v8::Isolate *isolate     = info.GetIsolate();

            uint64_t id     = 0;
            uint32_t caller = 0;
            uint32_t owner  = 0;
            {
                const std::lock_guard lock(state.mutex);
                const auto standIn = state.standIns.find(key);
                if (standIn != state.standIns.end()) {
                    id     = standIn->second->id;
                    caller = standIn->second->runtime;
                    owner  = state.OwnerOf(id);
                }
            }
            // Gone when the runtime holding it has left; its isolate can still run code until it is disposed.
            if (caller == 0) {
                Throw(isolate, "This resource can no longer call other resources");
                return;
            }
            if (owner == 0) {
                Throw(isolate, "The function belongs to a resource that has stopped");
                return;
            }

            std::vector<v8::Local<v8::Value>> arguments;
            arguments.reserve(static_cast<size_t>(info.Length()));
            for (int i = 0; i < info.Length(); ++i) {
                arguments.push_back(info[i]);
            }
            std::vector<TransferredValue> copied;
            if (!state.CopyArguments(caller, isolate, isolate->GetCurrentContext(), arguments, copied)) {
                return;
            }

            // Copying ran the arguments' getters, which may have stopped the owner: look it up again.
            // Shared, not referred to: the call may export or release entries, this one among them.
            std::shared_ptr<Handles> handles;
            {
                const std::lock_guard lock(state.mutex);
                owner = state.OwnerOf(id);
                if (owner != 0) {
                    handles = state.exports.at(id).handles;
                }
            }
            if (owner == 0) {
                Throw(isolate, "The function belongs to a resource that has stopped");
                return;
            }
            v8::Local<v8::Value> result;
            if (state.Invoke(caller, isolate, isolate->GetCurrentContext(), owner, handles->function, copied, Returned::Value, handles->receiver, id).ToLocal(&result)) {
                info.GetReturnValue().Set(result);
            }
        }

        // The runtime that owns a live export, or 0 when the export or its runtime is gone. With the mutex held.
        uint32_t OwnerOf(uint64_t id) const {
            const auto exported = exports.find(id);
            if (exported == exports.end() || !runtimes.contains(exported->second.runtime)) {
                return 0;
            }
            return exported->second.runtime;
        }

        // Copies arguments out of the caller. On failure an exception is pending there, naming the argument.
        bool CopyArguments(uint32_t caller, v8::Isolate *isolate, v8::Local<v8::Context> context, const std::vector<v8::Local<v8::Value>> &arguments, std::vector<TransferredValue> &out) {
            const auto hooks = Hooks(caller);
            out.resize(arguments.size());
            for (size_t i = 0; i < arguments.size(); ++i) {
                auto copied = ValueTransfer::Copy(isolate, context, arguments[i], *hooks);
                if (!copied) {
                    Throw(isolate, "Argument " + std::to_string(i) + ": " + copied.GetError());
                    return false;
                }
                out[i] = std::move(copied).GetValue();
            }
            return true;
        }

        // Calls `function` inside `ownerId` with copied arguments and brings back what it returned or threw. The
        // caller holds its own scopes; on a throw the exception is pending there and the result is empty.
        // `receiver`, when set, is the `this` the function runs with. `exportId` names an export call in the
        // monitor's slow reports.
        v8::MaybeLocal<v8::Value> Invoke(uint32_t callerId, v8::Isolate *isolate, v8::Local<v8::Context> context, uint32_t ownerId, const v8::Global<v8::Function> &function, const std::vector<TransferredValue> &arguments, Returned returned,
            const v8::Global<v8::Object> &receiver = {}, uint64_t exportId = 0) {
            Runtime *owner            = nullptr;
            v8::Isolate *ownerIsolate = nullptr;
            std::string ownerName;
            {
                const std::lock_guard lock(mutex);
                const auto ownerIt = runtimes.find(ownerId);
                if (ownerIt != runtimes.end()) {
                    owner        = &ownerIt->second;
                    ownerIsolate = owner->isolate;
                    ownerName    = owner->name;
                }
            }
            if (owner == nullptr) {
                Throw(isolate, "The function belongs to a resource that has stopped");
                return {};
            }
            FW_PROFILE_SCOPE_N("Scripting::CrossCall");
            FW_PROFILE_TEXT(ownerName.c_str(), ownerName.size());

            std::optional<TransferredValue> result;
            std::optional<TransferredValue> thrown;
            uint64_t settlement  = 0;
            auto *monitor        = ResourceMonitor::Current();
            const double charged = monitor ? monitor->ChargedMs(ownerIsolate, ResourceMonitor::Clock::now()) : 0.0;
            {
                ResourceMonitor::Scope scope(ownerIsolate);
                Entered entered(*owner);
                v8::TryCatch tryCatch(ownerIsolate);
                const auto ownerHooks = Hooks(ownerId);

                std::vector<v8::Local<v8::Value>> argv;
                argv.reserve(arguments.size());
                for (const auto &argument : arguments) {
                    v8::Local<v8::Value> rebuilt;
                    if (!ValueTransfer::Rebuild(ownerIsolate, entered.context, argument, *ownerHooks).ToLocal(&rebuilt)) {
                        break;
                    }
                    argv.push_back(rebuilt);
                }

                v8::Local<v8::Value> self = receiver.IsEmpty() ? v8::Undefined(ownerIsolate).As<v8::Value>() : receiver.Get(ownerIsolate).As<v8::Value>();
                v8::Local<v8::Value> value;
                // An export's handles are read here, with its owner held; empty once the export was released on the way.
                const bool released = function.IsEmpty();
                const bool called   = !released && argv.size() == arguments.size() && function.Get(ownerIsolate)->Call(entered.context, self, static_cast<int>(argv.size()), argv.data()).ToLocal(&value);
                if (released) {
                    thrown = ErrorValue("The function belongs to a resource that has stopped");
                }
                else if (!called) {
                    // Terminated (the owner called process.exit(), say): there is no exception value to copy.
                    thrown = tryCatch.HasTerminated() ? ErrorValue(ownerName + " stopped during the call") : CopyOut(ownerIsolate, entered.context, tryCatch.HasCaught() ? tryCatch.Exception() : v8::Local<v8::Value>(), *ownerHooks, "The call failed");
                }
                else if (value->IsPromise()) {
                    settlement = Await(ownerId, callerId, entered.context, value.As<v8::Promise>(), returned == Returned::Value);
                }
                else if (returned == Returned::Outcome && !value->IsBoolean()) {
                    result.emplace(); // Undefined: only a boolean (a veto) is an outcome, so nothing else is copied.
                }
                else {
                    auto copied = ValueTransfer::Copy(ownerIsolate, entered.context, value, *ownerHooks);
                    if (copied) {
                        result = std::move(copied).GetValue();
                    }
                    else {
                        thrown = ErrorValue("Return value: " + copied.GetError());
                    }
                }
            }
            // Event and message dispatch ask for an outcome and report their own handlers; an export call is reported
            // here, under the export's name so one slow export does not hide the next one of the same owner and caller.
            // The name is looked up only for a call that was slow: every export call passes here, and the export may
            // have been released during the call.
            if (monitor && returned == Returned::Value) {
                const auto now     = ResourceMonitor::Clock::now();
                const double ownMs = monitor->ChargedMs(ownerIsolate, now) - charged;
                if (monitor->IsSlow(ownMs)) {
                    std::string what = "export call from";
                    if (exportId != 0) {
                        std::string exportName = "(since released)";
                        {
                            const std::lock_guard lock(mutex);
                            const auto exported = exports.find(exportId);
                            if (exported != exports.end()) {
                                exportName = exported->second.name;
                            }
                        }
                        what = "export '" + exportName + "' called from";
                    }
                    monitor->ReportIfSlow(ownerIsolate, what, NameOf(callerId), ownMs, now);
                }
            }

            const auto hooks = Hooks(callerId);
            if (thrown) {
                v8::Local<v8::Value> error;
                if (ValueTransfer::Rebuild(isolate, context, *thrown, *hooks).ToLocal(&error)) {
                    isolate->ThrowException(error);
                }
                return {};
            }
            if (settlement != 0) {
                v8::Local<v8::Promise::Resolver> resolver;
                const bool made = v8::Promise::Resolver::New(context).ToLocal(&resolver);
                // Taken out of the table under the lock and dealt with after it: settling runs the hooks, and a
                // settlement that goes takes its outcome's holds with it, both of which lock.
                std::unique_ptr<Settlement> taken;
                bool waited = false;
                {
                    const std::lock_guard lock(mutex);
                    const auto waiting = settlements.find(settlement);
                    if (waiting != settlements.end()) {
                        waited = true;
                        if (made) {
                            waiting->second->resolver.Reset(isolate, resolver);
                        }
                        // Settled already, as the owner's queue drained on the way out of the call: nothing left to
                        // wait for. Without a promise nothing could be settled; an exception is pending already.
                        if (!made || waiting->second->settled) {
                            taken = std::move(waiting->second);
                            settlements.erase(waiting);
                        }
                    }
                }
                if (!made) {
                    return {};
                }
                if (!waited) {
                    Throw(isolate, "The call failed");
                    return {};
                }
                if (taken) {
                    SettleIn(isolate, context, *taken);
                }
                return resolver->GetPromise();
            }
            if (!result) {
                Throw(isolate, "The call failed");
                return {};
            }
            return ValueTransfer::Rebuild(isolate, context, *result, *hooks);
        }

        static TransferredValue ErrorValue(const std::string &message) {
            TransferredValue error;
            error.kind = TransferredValue::Kind::Error;
            TransferredValue text;
            text.kind = TransferredValue::Kind::String;
            text.text = message;
            error.properties.emplace_back("message", std::move(text));
            return error;
        }

        // A thrown or rejected value, copied for the other side; one that cannot cross becomes an error saying so.
        TransferredValue CopyOut(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value, const TransferFunctions &hooks, const char *fallback) {
            if (value.IsEmpty()) {
                return ErrorValue(fallback);
            }
            auto copied = ValueTransfer::Copy(isolate, context, value, hooks);
            if (copied) {
                return std::move(copied).GetValue();
            }
            return ErrorValue(ToUtf8(isolate, value) + " (" + copied.GetError() + ")");
        }

        // Called inside the owner: react to its promise, and settle the caller's when it does. Returns the
        // settlement's key, or 0 when the reaction could not be attached.
        uint64_t Await(uint32_t owner, uint32_t caller, v8::Local<v8::Context> context, v8::Local<v8::Promise> promise, bool keepValue) {
            v8::Isolate *isolate      = v8::Isolate::GetCurrent();
            auto settlement           = std::make_unique<Settlement>();
            settlement->key           = nextSettlement++;
            settlement->owner         = owner;
            settlement->caller        = caller;
            settlement->keepValue     = keepValue;
            v8::Local<v8::Value> data = CallbackData(isolate, settlement->key);

            v8::Local<v8::Function> onFulfilled;
            v8::Local<v8::Function> onRejected;
            if (!v8::Function::New(context, OnFulfilled, data).ToLocal(&onFulfilled) || !v8::Function::New(context, OnRejected, data).ToLocal(&onRejected) || promise->Then(context, onFulfilled, onRejected).IsEmpty()) {
                return 0;
            }
            const uint64_t key = settlement->key;
            const std::lock_guard lock(mutex);
            settlements.emplace(key, std::move(settlement));
            return key;
        }

        static void OnFulfilled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Settle(info, true);
        }

        static void OnRejected(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Settle(info, false);
        }

        // Runs inside the owner, from its microtask queue.
        static void Settle(const v8::FunctionCallbackInfo<v8::Value> &info, bool fulfilled) {
            auto [statePointer, key] = ReadCallbackData(info);
            State &state             = *statePointer;
            // Held here, out of the table, while the value is copied: copying runs the owner's getters.
            std::unique_ptr<Settlement> owned;
            bool callerLeft = true;
            std::string callerName;
            {
                const std::lock_guard lock(state.mutex);
                const auto it = state.settlements.find(key);
                if (it == state.settlements.end()) {
                    return; // Already settled, or its owner has left.
                }
                owned = std::move(it->second);
                state.settlements.erase(it);
                const auto caller = state.runtimes.find(owned->caller);
                if (caller != state.runtimes.end()) {
                    callerLeft = false;
                    callerName = caller->second.name;
                }
            }
            if (callerLeft) {
                return; // Nobody to settle.
            }
            FW_PROFILE_SCOPE_N("Scripting::CrossSettle");
            FW_PROFILE_TEXT(callerName.c_str(), callerName.size());

            v8::Isolate *isolate       = info.GetIsolate();
            v8::Local<v8::Value> value = info.Length() > 0 ? info[0] : v8::Undefined(isolate).As<v8::Value>();
            owned->settled             = true;
            owned->fulfilled           = fulfilled;
            if (fulfilled && owned->keepValue) {
                auto result = ValueTransfer::Copy(isolate, isolate->GetCurrentContext(), value, *state.Hooks(owned->owner));
                if (result) {
                    owned->outcome = std::move(result).GetValue();
                }
                else {
                    owned->outcome   = ErrorValue("Resolved value: " + result.GetError());
                    owned->fulfilled = false;
                }
            }
            else if (!fulfilled) {
                owned->outcome = state.CopyOut(isolate, isolate->GetCurrentContext(), value, *state.Hooks(owned->owner), "The promise was rejected");
            }

            // Settled as the call that awaits it leaves the owner: the caller's promise does not exist yet. Left here
            // with its outcome for Invoke, which settles the promise as soon as it has made it.
            if (owned->resolver.IsEmpty()) {
                const std::lock_guard lock(state.mutex);
                const uint64_t ownedKey = owned->key;
                state.settlements.emplace(ownedKey, std::move(owned));
                return;
            }
            state.SettleCaller(*owned);
        }

        // Settles the caller's promise with the outcome the settlement carries, from outside the caller.
        void SettleCaller(Settlement &settlement) {
            Runtime *caller = nullptr;
            {
                const std::lock_guard lock(mutex);
                const auto it = runtimes.find(settlement.caller);
                if (it != runtimes.end()) {
                    caller = &it->second;
                }
            }
            if (settlement.resolver.IsEmpty() || caller == nullptr) {
                return;
            }
            // The copy ran in the owner; rebuilding, resolving and the continuations that run as the caller is left
            // are the caller's.
            v8::Isolate *const callerIsolate = caller->isolate;
            ResourceMonitor::Scope charged(callerIsolate);
            Entered entered(*caller);
            SettleIn(callerIsolate, entered.context, settlement);
        }

        // Settles the caller's promise with the outcome the settlement carries, rebuilt in the caller, which is entered.
        void SettleIn(v8::Isolate *isolate, v8::Local<v8::Context> context, Settlement &settlement) {
            v8::Local<v8::Promise::Resolver> resolver = settlement.resolver.Get(isolate);
            bool fulfilled                            = settlement.fulfilled;
            v8::TryCatch tryCatch(isolate);
            v8::Local<v8::Value> rebuilt;
            if (!ValueTransfer::Rebuild(isolate, context, settlement.outcome, *Hooks(settlement.caller)).ToLocal(&rebuilt)) {
                rebuilt   = tryCatch.Exception();
                fulfilled = false;
            }
            if (fulfilled) {
                resolver->Resolve(context, rebuilt).Check();
            }
            else {
                resolver->Reject(context, rebuilt).Check();
            }
        }
    };

    FunctionReferences::FunctionReferences(): _state(std::make_shared<State>()) {}

    FunctionReferences::~FunctionReferences() {
        // Runtimes leave before their isolates die; anything still here would fire a weak callback into freed state.
        for (auto &[key, standIn] : _state->standIns) {
            standIn->handle.Reset();
        }
        _state->standIns.clear();
        _state->standInByExport.clear();
        _state->collected.clear();
        _state->settlements.clear();
        _state->exports.clear();
        _state->runtimes.clear();
        _state->runtimeByIsolate.clear();
    }

    uint32_t FunctionReferences::AddRuntime(v8::Isolate *isolate, v8::Local<v8::Context> context, std::string name) {
        const std::lock_guard lock(_state->mutex);
        const uint32_t id     = _state->nextRuntime++;
        State::Runtime &entry = _state->runtimes[id];
        entry.isolate         = isolate;
        entry.context.Reset(isolate, context);
        entry.name                        = std::move(name);
        entry.hooks                       = std::make_shared<const TransferFunctions>(_state->MakeHooks(id));
        _state->runtimeByIsolate[isolate] = id;
        return id;
    }

    void FunctionReferences::RemoveRuntime(uint32_t runtime) {
        State &state = *_state;
        std::string name;
        // Promises it owed to callers will never settle: they are rejected below, once the tables are in order.
        std::vector<std::unique_ptr<State::Settlement>> owed;
        {
            const std::lock_guard lock(state.mutex);
            const auto it = state.runtimes.find(runtime);
            if (it == state.runtimes.end()) {
                return;
            }
            name = it->second.name;

            // What it exported can no longer be called; stand-ins elsewhere now throw. Its handles go here, in its
            // isolate, whoever still shares them.
            for (auto exported = state.exports.begin(); exported != state.exports.end();) {
                if (exported->second.runtime == runtime) {
                    exported->second.handles->Reset();
                    exported = state.exports.erase(exported);
                }
                else {
                    ++exported;
                }
            }
            for (const auto &handles : it->second.retired) {
                handles->Reset();
            }

            // What its stand-ins held elsewhere is released: they die with the isolate and their weak callbacks never
            // fire.
            for (auto standIn = state.standIns.begin(); standIn != state.standIns.end();) {
                if (standIn->second->runtime == runtime) {
                    standIn->second->handle.Reset();
                    state.ReleaseLocked(standIn->second->id);
                    standIn = state.standIns.erase(standIn);
                }
                else {
                    ++standIn;
                }
            }
            std::erase_if(state.standInByExport, [&](const auto &entry) {
                return entry.first.first == runtime;
            });

            // Promises owed to it have nobody to settle.
            for (auto settlement = state.settlements.begin(); settlement != state.settlements.end();) {
                if (settlement->second->owner == runtime) {
                    owed.push_back(std::move(settlement->second));
                    settlement = state.settlements.erase(settlement);
                    continue;
                }
                if (settlement->second->caller == runtime) {
                    settlement->second->resolver.Reset();
                }
                ++settlement;
            }
            state.runtimeByIsolate.erase(it->second.isolate);
            state.runtimes.erase(it);
        }

        for (auto &settlement : owed) {
            State::Runtime *caller = nullptr;
            {
                const std::lock_guard lock(state.mutex);
                const auto found = state.runtimes.find(settlement->caller);
                if (found != state.runtimes.end()) {
                    caller = &found->second;
                }
            }
            if (settlement->resolver.IsEmpty() || caller == nullptr) {
                continue;
            }
            v8::Isolate *const callerIsolate = caller->isolate;
            State::Entered entered(*caller);
            v8::Local<v8::Value> error = v8::Exception::Error(v8pp::to_v8(callerIsolate, name + " stopped before the call finished"));
            settlement->resolver.Get(callerIsolate)->Reject(entered.context, error).Check();
        }
    }

    TransferFunctions FunctionReferences::For(uint32_t runtime) const {
        return *_state->Hooks(runtime);
    }

    uint32_t FunctionReferences::FindRuntime(v8::Isolate *isolate) const {
        return _state->FindRuntime(isolate);
    }

    std::optional<std::vector<v8::Local<v8::Value>>> FunctionReferences::RebuildArguments(v8::Isolate *target, const std::vector<TransferredValue> &arguments) {
        const uint32_t targetId = _state->FindRuntime(target);
        if (targetId == 0) {
            State::Throw(target, "The resource is not reachable from here");
            return std::nullopt;
        }
        const auto hooks                     = _state->Hooks(targetId);
        const v8::Local<v8::Context> context = target->GetCurrentContext();
        std::vector<v8::Local<v8::Value>> rebuilt(arguments.size());
        for (size_t i = 0; i < arguments.size(); ++i) {
            if (!ValueTransfer::Rebuild(target, context, arguments[i], *hooks).ToLocal(&rebuilt[i])) {
                return std::nullopt;
            }
        }
        return rebuilt;
    }

    std::optional<std::vector<TransferredValue>> FunctionReferences::CopyArguments(v8::Isolate *caller, const std::vector<v8::Local<v8::Value>> &arguments) {
        const uint32_t callerId = _state->FindRuntime(caller);
        if (callerId == 0) {
            State::Throw(caller, "The resource is not reachable from here");
            return std::nullopt;
        }
        std::vector<TransferredValue> copied;
        if (!_state->CopyArguments(callerId, caller, caller->GetCurrentContext(), arguments, copied)) {
            return std::nullopt;
        }
        return copied;
    }

    v8::MaybeLocal<v8::Value> FunctionReferences::Call(v8::Isolate *caller, v8::Isolate *owner, const v8::Global<v8::Function> &function, const std::vector<v8::Local<v8::Value>> &arguments, Returned returned) {
        auto copied = CopyArguments(caller, arguments);
        if (!copied) {
            return {};
        }
        return Call(caller, owner, function, *copied, returned);
    }

    v8::MaybeLocal<v8::Value> FunctionReferences::Call(v8::Isolate *caller, v8::Isolate *owner, const v8::Global<v8::Function> &function, const std::vector<TransferredValue> &arguments, Returned returned) {
        const uint32_t callerId = _state->FindRuntime(caller);
        const uint32_t ownerId  = _state->FindRuntime(owner);
        if (callerId == 0 || ownerId == 0) {
            State::Throw(caller, "The resource is not reachable from here");
            return {};
        }
        return _state->Invoke(callerId, caller, caller->GetCurrentContext(), ownerId, function, arguments, returned);
    }

    v8::MaybeLocal<v8::Value> FunctionReferences::Fetch(v8::Isolate *caller, v8::Isolate *owner, fu2::function_view<v8::MaybeLocal<v8::Value>(v8::Isolate *, v8::Local<v8::Context>)> produce) {
        State &state            = *_state;
        const uint32_t callerId = state.FindRuntime(caller);
        const uint32_t ownerId  = state.FindRuntime(owner);
        if (callerId == 0 || ownerId == 0) {
            State::Throw(caller, "The resource is not reachable from here");
            return {};
        }
        v8::Local<v8::Context> context = caller->GetCurrentContext();
        State::Runtime *ownerRuntime   = nullptr;
        std::string ownerName;
        {
            const std::lock_guard lock(state.mutex);
            const auto found = state.runtimes.find(ownerId);
            if (found != state.runtimes.end()) {
                ownerRuntime = &found->second;
                ownerName    = found->second.name;
            }
        }
        if (ownerRuntime == nullptr) {
            State::Throw(caller, "The resource is not reachable from here");
            return {};
        }
        FW_PROFILE_SCOPE_N("Scripting::CrossFetch");
        FW_PROFILE_TEXT(ownerName.c_str(), ownerName.size());

        TransferredValue copied;
        std::string error;
        {
            ResourceMonitor::Scope charged(owner);
            State::Entered entered(*ownerRuntime);
            v8::TryCatch tryCatch(owner);
            v8::Local<v8::Value> value;
            if (!produce(owner, entered.context).ToLocal(&value)) {
                if (tryCatch.HasTerminated()) {
                    error = ownerName + " stopped while the value was read";
                }
                else {
                    error = tryCatch.HasCaught() ? ToUtf8(owner, tryCatch.Exception()) : std::string("The value could not be read");
                }
            }
            else {
                auto result = ValueTransfer::Copy(owner, entered.context, value, *state.Hooks(ownerId));
                if (result) {
                    copied = std::move(result).GetValue();
                }
                else {
                    error = result.GetError();
                }
            }
        }
        if (!error.empty()) {
            State::Throw(caller, error);
            return {};
        }
        return ValueTransfer::Rebuild(caller, context, copied, *state.Hooks(callerId));
    }

    void FunctionReferences::ReleaseCollected() {
        _state->ReleaseCollected();
    }

    void FunctionReferences::NameExport(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Function> function, const std::string &name) {
        (void)function->SetPrivate(context, State::ExportNameKey(isolate), v8pp::to_v8(isolate, name));
    }

    size_t FunctionReferences::GetExportCount() const {
        const std::lock_guard lock(_state->mutex);
        return _state->exports.size();
    }

    size_t FunctionReferences::GetStandInCount() const {
        const std::lock_guard lock(_state->mutex);
        return _state->standIns.size();
    }

} // namespace Framework::Scripting
