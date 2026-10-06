/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "function_references.h"
#include "engine_helpers.h"

#include <v8pp/convert.hpp>

#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Framework::Scripting {

    struct FunctionReferences::State : std::enable_shared_from_this<FunctionReferences::State> {
        struct Runtime {
            v8::Isolate *isolate = nullptr;
            v8::Global<v8::Context> context;
            std::string name;
        };

        // A function kept alive for other runtimes to call. `holds` counts the copies and stand-ins that refer to it.
        // `receiver` is the object it was sent as a method of, which it is called on: a method exported on an object
        // keeps its `this`.
        struct Export {
            uint32_t runtime = 0;
            v8::Global<v8::Function> function;
            v8::Global<v8::Object> receiver;
            uint32_t holds = 0;
        };

        // A callable in one runtime that forwards to an export of another. Collected with its JS function.
        struct StandIn {
            State *state = nullptr;
            uint64_t key = 0;
            uint64_t id  = 0;
            uint32_t runtime = 0;
            v8::Global<v8::Function> handle;
        };

        // A promise returned to a caller in another runtime, waiting on the owner's promise. Owned here until the
        // owner's promise settles or the owner goes away; the owner's reaction functions name it by key.
        struct Settlement {
            uint64_t key   = 0;
            uint32_t owner = 0;
            uint32_t caller = 0;
            bool keepValue  = true;
            v8::Global<v8::Promise::Resolver> resolver;
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

        // Enters a runtime for the lifetime of the object, in the order V8 requires.
        struct Entered {
            explicit Entered(Runtime &runtime): locker(runtime.isolate), isolateScope(runtime.isolate), handleScope(runtime.isolate), context(runtime.context.Get(runtime.isolate)), contextScope(context) {}

            v8::Locker locker;
            v8::Isolate::Scope isolateScope;
            v8::HandleScope handleScope;
            v8::Local<v8::Context> context;
            v8::Context::Scope contextScope;
        };

        std::map<uint32_t, Runtime> runtimes;
        std::unordered_map<v8::Isolate *, uint32_t> runtimeByIsolate;
        std::map<uint64_t, Export> exports;
        std::unordered_map<uint64_t, std::unique_ptr<StandIn>> standIns;
        // The stand-in each runtime holds for an export, keyed by (runtime, export id).
        std::map<std::pair<uint32_t, uint64_t>, uint64_t> standInByExport;
        // Stand-ins whose functions were collected, waiting for ReleaseCollected().
        std::vector<uint64_t> collected;
        std::map<uint64_t, std::unique_ptr<Settlement>> settlements;
        uint32_t nextRuntime    = 1;
        uint64_t nextExport     = 1;
        uint64_t nextStandIn    = 1;
        uint64_t nextSettlement = 1;

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
            const auto it = runtimes.find(runtime);
            return it != runtimes.end() ? it->second.name : std::string("a stopped resource");
        }

        uint32_t FindRuntime(v8::Isolate *isolate) const {
            const auto it = runtimeByIsolate.find(isolate);
            return it != runtimeByIsolate.end() ? it->second : 0;
        }

        TransferFunctions Hooks(uint32_t runtime) {
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

        void ExportFunction(uint32_t runtime, v8::Isolate *isolate, v8::Local<v8::Function> function, v8::Local<v8::Object> holder, TransferredValue &out) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Private> key     = v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.functionReference"));

            // A function exported before carries its id: exporting it again reuses the entry, so a handler sent on
            // every event costs one, and a stand-in carries the id of the export it forwards to, so sending one on
            // refers to the original instead of stacking a stand-in on a stand-in.
            uint64_t id = 0;
            v8::Local<v8::Value> previous;
            if (function->GetPrivate(context, key).ToLocal(&previous) && previous->IsNumber()) {
                const auto existing = static_cast<uint64_t>(previous.As<v8::Number>()->Value());
                if (exports.contains(existing)) {
                    id = existing;
                }
            }
            if (id == 0) {
                id                   = nextExport++;
                Export &entry        = exports[id];
                entry.runtime        = runtime;
                entry.function.Reset(isolate, function);
                function->SetPrivate(context, key, v8::Number::New(isolate, static_cast<double>(id))).Check();
            }

            Export &exported = exports[id];
            if (exported.receiver.IsEmpty() && !holder.IsEmpty() && exported.runtime == runtime) {
                exported.receiver.Reset(isolate, holder);
            }
            ++exported.holds;
            // Built in place: a temporary Hold would release the export as it went out of scope.
            auto hold     = std::make_shared<Hold>();
            hold->state   = weak_from_this();
            hold->id      = id;
            out.reference = id;
            out.retainer  = std::move(hold);
        }

        void Release(uint64_t id) {
            const auto it = exports.find(id);
            if (it == exports.end()) {
                return;
            }
            if (--it->second.holds == 0) {
                exports.erase(it);
            }
        }

        v8::MaybeLocal<v8::Function> ImportFunction(uint32_t runtime, v8::Isolate *isolate, v8::Local<v8::Context> context, uint64_t id) {
            const auto it = exports.find(id);
            if (it == exports.end()) {
                Throw(isolate, "The function belongs to a resource that has stopped");
                return {};
            }

            // A function coming home is itself, not a stand-in for itself.
            if (it->second.runtime == runtime) {
                return it->second.function.Get(isolate);
            }

            // The runtime already holds a stand-in for it: the same function arrives as the same stand-in.
            const auto cached = standInByExport.find({runtime, id});
            if (cached != standInByExport.end()) {
                const auto existing = standIns.find(cached->second);
                if (existing != standIns.end() && !existing->second->handle.IsEmpty()) {
                    return existing->second->handle.Get(isolate);
                }
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
            ++it->second.holds;
            v8::Local<v8::Private> key = v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.functionReference"));
            function->SetPrivate(context, key, v8::Number::New(isolate, static_cast<double>(id))).Check();
            standIn->handle.Reset(isolate, function);
            standIn->handle.SetWeak(standIn.get(), OnStandInCollected, v8::WeakCallbackType::kParameter);
            standInByExport[{runtime, id}] = standIn->key;
            standIns.emplace(standIn->key, std::move(standIn));
            return function;
        }

        // A first-pass weak callback: V8 allows nothing in it but resetting the handle. Releasing the export destroys a
        // handle of another isolate, so it waits for ReleaseCollected().
        static void OnStandInCollected(const v8::WeakCallbackInfo<StandIn> &info) {
            StandIn *standIn = info.GetParameter();
            standIn->handle.Reset();
            standIn->state->collected.push_back(standIn->key);
        }

        void ReleaseCollected() {
            std::vector<uint64_t> keys;
            keys.swap(collected);
            for (const uint64_t key : keys) {
                const auto it = standIns.find(key);
                if (it == standIns.end()) {
                    continue; // Its runtime left first and released it then.
                }
                const auto cached = standInByExport.find({it->second->runtime, it->second->id});
                if (cached != standInByExport.end() && cached->second == key) {
                    standInByExport.erase(cached);
                }
                Release(it->second->id);
                standIns.erase(it);
            }
        }

        // Runs in the caller's runtime: forward the call to the export's owner.
        static void CallStandIn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto [statePointer, key] = ReadCallbackData(info);
            State &state             = *statePointer;
            v8::Isolate *isolate     = info.GetIsolate();

            // Gone when the runtime holding it has left; its isolate can still run code until it is disposed.
            const auto standIn = state.standIns.find(key);
            if (standIn == state.standIns.end()) {
                Throw(isolate, "This resource can no longer call other resources");
                return;
            }
            const uint64_t id     = standIn->second->id;
            const uint32_t caller = standIn->second->runtime;

            if (state.OwnerOf(id) == 0) {
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
            const uint32_t owner = state.OwnerOf(id);
            if (owner == 0) {
                Throw(isolate, "The function belongs to a resource that has stopped");
                return;
            }
            // Held by value: the call may export or release entries, and the map may move under an iterator.
            v8::Isolate *ownerIsolate = state.runtimes.at(owner).isolate;
            const Export &exported    = state.exports.at(id);
            v8::Global<v8::Function> function(ownerIsolate, exported.function);
            v8::Global<v8::Object> receiver(ownerIsolate, exported.receiver);
            v8::Local<v8::Value> result;
            if (state.Invoke(caller, isolate, isolate->GetCurrentContext(), owner, function, copied, Returned::Value, receiver).ToLocal(&result)) {
                info.GetReturnValue().Set(result);
            }
        }

        // The runtime that owns a live export, or 0 when the export or its runtime is gone.
        uint32_t OwnerOf(uint64_t id) const {
            const auto exported = exports.find(id);
            if (exported == exports.end() || !runtimes.contains(exported->second.runtime)) {
                return 0;
            }
            return exported->second.runtime;
        }

        // Copies arguments out of the caller. On failure an exception is pending there, naming the argument.
        bool CopyArguments(uint32_t caller, v8::Isolate *isolate, v8::Local<v8::Context> context, const std::vector<v8::Local<v8::Value>> &arguments, std::vector<TransferredValue> &out) {
            const TransferFunctions hooks = Hooks(caller);
            out.resize(arguments.size());
            for (size_t i = 0; i < arguments.size(); ++i) {
                auto copied = ValueTransfer::Copy(isolate, context, arguments[i], hooks);
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
        // `receiver`, when set, is the `this` the function runs with.
        v8::MaybeLocal<v8::Value> Invoke(uint32_t callerId, v8::Isolate *isolate, v8::Local<v8::Context> context, uint32_t ownerId, const v8::Global<v8::Function> &function, const std::vector<TransferredValue> &arguments, Returned returned, const v8::Global<v8::Object> &receiver = {}) {
            const auto ownerIt = runtimes.find(ownerId);
            if (ownerIt == runtimes.end()) {
                Throw(isolate, "The function belongs to a resource that has stopped");
                return {};
            }
            Runtime &owner = ownerIt->second;

            std::optional<TransferredValue> result;
            std::optional<TransferredValue> thrown;
            uint64_t settlement = 0;
            {
                Entered entered(owner);
                v8::TryCatch tryCatch(owner.isolate);
                const TransferFunctions ownerHooks = Hooks(ownerId);

                std::vector<v8::Local<v8::Value>> argv;
                argv.reserve(arguments.size());
                for (const auto &argument : arguments) {
                    v8::Local<v8::Value> rebuilt;
                    if (!ValueTransfer::Rebuild(owner.isolate, entered.context, argument, ownerHooks).ToLocal(&rebuilt)) {
                        break;
                    }
                    argv.push_back(rebuilt);
                }

                v8::Local<v8::Value> self = receiver.IsEmpty() ? v8::Undefined(owner.isolate).As<v8::Value>() : receiver.Get(owner.isolate).As<v8::Value>();
                v8::Local<v8::Value> value;
                const bool called = argv.size() == arguments.size() && function.Get(owner.isolate)->Call(entered.context, self, static_cast<int>(argv.size()), argv.data()).ToLocal(&value);
                if (!called) {
                    // Terminated (the owner called process.exit(), say): there is no exception value to copy.
                    thrown = tryCatch.HasTerminated() ? ErrorValue(owner.name + " stopped during the call") : CopyOut(owner.isolate, entered.context, tryCatch.HasCaught() ? tryCatch.Exception() : v8::Local<v8::Value>(), ownerHooks, "The call failed");
                }
                else if (value->IsPromise()) {
                    settlement = Await(ownerId, callerId, entered.context, value.As<v8::Promise>(), returned == Returned::Value);
                }
                else if (returned == Returned::Outcome && !value->IsBoolean()) {
                    result.emplace(); // Undefined: only a boolean (a veto) is an outcome, so nothing else is copied.
                }
                else {
                    auto copied = ValueTransfer::Copy(owner.isolate, entered.context, value, ownerHooks);
                    if (copied) {
                        result = std::move(copied).GetValue();
                    }
                    else {
                        thrown = ErrorValue("Return value: " + copied.GetError());
                    }
                }
            }

            const TransferFunctions hooks = Hooks(callerId);
            if (thrown) {
                v8::Local<v8::Value> error;
                if (ValueTransfer::Rebuild(isolate, context, *thrown, hooks).ToLocal(&error)) {
                    isolate->ThrowException(error);
                }
                return {};
            }
            if (settlement != 0) {
                const auto waiting = settlements.find(settlement);
                v8::Local<v8::Promise::Resolver> resolver;
                if (waiting == settlements.end() || !v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
                    return {};
                }
                waiting->second->resolver.Reset(isolate, resolver);
                return resolver->GetPromise();
            }
            if (!result) {
                Throw(isolate, "The call failed");
                return {};
            }
            return ValueTransfer::Rebuild(isolate, context, *result, hooks);
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
            v8::Isolate *isolate  = context->GetIsolate();
            auto settlement       = std::make_unique<Settlement>();
            settlement->key       = nextSettlement++;
            settlement->owner     = owner;
            settlement->caller    = caller;
            settlement->keepValue = keepValue;
            v8::Local<v8::Value> data = CallbackData(isolate, settlement->key);

            v8::Local<v8::Function> onFulfilled;
            v8::Local<v8::Function> onRejected;
            if (!v8::Function::New(context, OnFulfilled, data).ToLocal(&onFulfilled) || !v8::Function::New(context, OnRejected, data).ToLocal(&onRejected) || promise->Then(context, onFulfilled, onRejected).IsEmpty()) {
                return 0;
            }
            const uint64_t key = settlement->key;
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
            const auto it            = state.settlements.find(key);
            if (it == state.settlements.end()) {
                return; // Already settled, or its owner or caller has left.
            }
            std::unique_ptr<Settlement> owned = std::move(it->second);
            state.settlements.erase(it);

            const auto caller = state.runtimes.find(owned->caller);
            if (owned->resolver.IsEmpty() || caller == state.runtimes.end()) {
                return;
            }

            v8::Isolate *isolate = info.GetIsolate();
            v8::Local<v8::Value> value = info.Length() > 0 ? info[0] : v8::Undefined(isolate).As<v8::Value>();
            TransferredValue copied;
            if (fulfilled && owned->keepValue) {
                auto result = ValueTransfer::Copy(isolate, isolate->GetCurrentContext(), value, state.Hooks(owned->owner));
                if (result) {
                    copied = std::move(result).GetValue();
                }
                else {
                    copied    = ErrorValue("Resolved value: " + result.GetError());
                    fulfilled = false;
                }
            }
            else if (!fulfilled) {
                copied = state.CopyOut(isolate, isolate->GetCurrentContext(), value, state.Hooks(owned->owner), "The promise was rejected");
            }

            Entered entered(caller->second);
            v8::Local<v8::Promise::Resolver> resolver = owned->resolver.Get(caller->second.isolate);
            v8::TryCatch tryCatch(caller->second.isolate);
            v8::Local<v8::Value> rebuilt;
            if (!ValueTransfer::Rebuild(caller->second.isolate, entered.context, copied, state.Hooks(owned->caller)).ToLocal(&rebuilt)) {
                rebuilt   = tryCatch.Exception();
                fulfilled = false;
            }
            if (fulfilled) {
                resolver->Resolve(entered.context, rebuilt).Check();
            }
            else {
                resolver->Reject(entered.context, rebuilt).Check();
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
        const uint32_t id      = _state->nextRuntime++;
        State::Runtime &entry  = _state->runtimes[id];
        entry.isolate          = isolate;
        entry.context.Reset(isolate, context);
        entry.name = std::move(name);
        _state->runtimeByIsolate[isolate] = id;
        return id;
    }

    void FunctionReferences::RemoveRuntime(uint32_t runtime) {
        State &state  = *_state;
        const auto it = state.runtimes.find(runtime);
        if (it == state.runtimes.end()) {
            return;
        }
        const std::string name = it->second.name;

        // What it exported can no longer be called; stand-ins elsewhere now throw.
        std::erase_if(state.exports, [&](const auto &entry) { return entry.second.runtime == runtime; });

        // What its stand-ins held elsewhere is released: they die with the isolate and their weak callbacks never fire.
        for (auto standIn = state.standIns.begin(); standIn != state.standIns.end();) {
            if (standIn->second->runtime == runtime) {
                standIn->second->handle.Reset();
                state.Release(standIn->second->id);
                standIn = state.standIns.erase(standIn);
            }
            else {
                ++standIn;
            }
        }
        std::erase_if(state.standInByExport, [&](const auto &entry) { return entry.first.first == runtime; });

        // Promises it owed to callers will never settle: reject them. Promises owed to it have nobody to settle.
        std::vector<std::unique_ptr<State::Settlement>> owed;
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

        for (auto &settlement : owed) {
            const auto caller = state.runtimes.find(settlement->caller);
            if (settlement->resolver.IsEmpty() || caller == state.runtimes.end()) {
                continue;
            }
            State::Entered entered(caller->second);
            v8::Local<v8::Value> error = v8::Exception::Error(v8pp::to_v8(caller->second.isolate, name + " stopped before the call finished"));
            settlement->resolver.Get(caller->second.isolate)->Reject(entered.context, error).Check();
        }
    }

    TransferFunctions FunctionReferences::For(uint32_t runtime) const {
        return _state->Hooks(runtime);
    }

    uint32_t FunctionReferences::FindRuntime(v8::Isolate *isolate) const {
        return _state->FindRuntime(isolate);
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
        State::Runtime &ownerRuntime   = state.runtimes.at(ownerId);

        TransferredValue copied;
        std::string error;
        {
            State::Entered entered(ownerRuntime);
            v8::TryCatch tryCatch(owner);
            v8::Local<v8::Value> value;
            if (!produce(owner, entered.context).ToLocal(&value)) {
                if (tryCatch.HasTerminated()) {
                    error = ownerRuntime.name + " stopped while the value was read";
                }
                else {
                    error = tryCatch.HasCaught() ? ToUtf8(owner, tryCatch.Exception()) : std::string("The value could not be read");
                }
            }
            else {
                auto result = ValueTransfer::Copy(owner, entered.context, value, state.Hooks(ownerId));
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
        return ValueTransfer::Rebuild(caller, context, copied, state.Hooks(callerId));
    }

    void FunctionReferences::ReleaseCollected() {
        _state->ReleaseCollected();
    }

    size_t FunctionReferences::GetExportCount() const {
        return _state->exports.size();
    }

    size_t FunctionReferences::GetStandInCount() const {
        return _state->standIns.size();
    }

} // namespace Framework::Scripting
