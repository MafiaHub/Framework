/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "function_references.h"

#include <map>
#include <optional>
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
        struct Export {
            uint32_t runtime = 0;
            v8::Global<v8::Function> function;
            uint32_t holds = 0;
        };

        // A callable in one runtime that forwards to an export of another. Collected with its JS function.
        struct StandIn {
            State *state = nullptr;
            uint64_t id = 0;
            uint32_t runtime = 0;
            v8::Global<v8::Function> handle;
        };

        // A promise returned to a caller in another runtime, waiting on the owner's promise. Owned here until the
        // owner's promise settles or the owner goes away; the owner's reaction functions point at it.
        struct Settlement {
            State *state = nullptr;
            uint32_t owner = 0;
            uint32_t caller = 0;
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
        std::map<uint64_t, Export> exports;
        std::map<StandIn *, std::unique_ptr<StandIn>> standIns;
        std::map<Settlement *, std::unique_ptr<Settlement>> settlements;
        uint32_t nextRuntime = 1;
        uint64_t nextExport  = 1;

        static v8::Local<v8::String> Text(v8::Isolate *isolate, const std::string &text) {
            return v8::String::NewFromUtf8(isolate, text.data(), v8::NewStringType::kNormal, static_cast<int>(text.size())).ToLocalChecked();
        }

        static void Throw(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::Error(Text(isolate, message)));
        }

        static std::string ToUtf8(v8::Isolate *isolate, v8::Local<v8::Value> value) {
            v8::String::Utf8Value text(isolate, value);
            return *text ? std::string(*text, text.length()) : std::string();
        }

        std::string NameOf(uint32_t runtime) const {
            const auto it = runtimes.find(runtime);
            return it != runtimes.end() ? it->second.name : std::string("a stopped resource");
        }

        TransferFunctions Hooks(uint32_t runtime) {
            TransferFunctions functions;
            std::weak_ptr<State> weak = weak_from_this();
            functions.exportFunction  = [weak, runtime](v8::Isolate *isolate, v8::Local<v8::Function> function, TransferredValue &out) {
                if (auto state = weak.lock()) {
                    state->ExportFunction(runtime, isolate, function, out);
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

        void ExportFunction(uint32_t runtime, v8::Isolate *isolate, v8::Local<v8::Function> function, TransferredValue &out) {
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

            ++exports[id].holds;
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

            auto standIn     = std::make_unique<StandIn>();
            standIn->state   = this;
            standIn->id      = id;
            standIn->runtime = runtime;

            v8::Local<v8::Function> function;
            if (!v8::Function::New(context, CallStandIn, v8::External::New(isolate, standIn.get())).ToLocal(&function)) {
                return {};
            }
            ++it->second.holds;
            v8::Local<v8::Private> key = v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.functionReference"));
            function->SetPrivate(context, key, v8::Number::New(isolate, static_cast<double>(id))).Check();
            standIn->handle.Reset(isolate, function);
            standIn->handle.SetWeak(standIn.get(), OnStandInCollected, v8::WeakCallbackType::kParameter);
            standIns.emplace(standIn.get(), std::move(standIn));
            return function;
        }

        static void OnStandInCollected(const v8::WeakCallbackInfo<StandIn> &info) {
            StandIn *standIn = info.GetParameter();
            State *state     = standIn->state;
            standIn->handle.Reset();
            state->Release(standIn->id);
            state->standIns.erase(standIn);
        }

        // Runs in the caller's runtime: copy the arguments out, call the export inside its owner, and bring back what it
        // returned or threw.
        static void CallStandIn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *standIn          = static_cast<StandIn *>(info.Data().As<v8::External>()->Value());
            State &state           = *standIn->state;
            v8::Isolate *isolate   = info.GetIsolate();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            const auto exported = state.exports.find(standIn->id);
            if (exported == state.exports.end() || !state.runtimes.contains(exported->second.runtime)) {
                Throw(isolate, "The function belongs to a resource that has stopped");
                return;
            }
            const uint32_t ownerId = exported->second.runtime;
            Runtime &owner         = state.runtimes.at(ownerId);

            // Arguments leave the caller first; whatever cannot cross fails the call on the caller's side.
            std::vector<TransferredValue> arguments(info.Length());
            const TransferFunctions callerHooks = state.Hooks(standIn->runtime);
            for (int i = 0; i < info.Length(); ++i) {
                auto copied = ValueTransfer::Copy(isolate, context, info[i], callerHooks);
                if (!copied) {
                    Throw(isolate, "Argument " + std::to_string(i) + ": " + copied.GetError());
                    return;
                }
                arguments[i] = copied.GetValue();
            }

            std::optional<TransferredValue> result;
            std::optional<TransferredValue> thrown;
            Settlement *settlement = nullptr;
            {
                Entered entered(owner);
                v8::TryCatch tryCatch(owner.isolate);
                const TransferFunctions ownerHooks = state.Hooks(ownerId);

                std::vector<v8::Local<v8::Value>> argv;
                for (const auto &argument : arguments) {
                    v8::Local<v8::Value> rebuilt;
                    if (!ValueTransfer::Rebuild(owner.isolate, entered.context, argument, ownerHooks).ToLocal(&rebuilt)) {
                        break;
                    }
                    argv.push_back(rebuilt);
                }

                v8::Local<v8::Value> returned;
                const bool called = argv.size() == arguments.size() && exported->second.function.Get(owner.isolate)->Call(entered.context, v8::Undefined(owner.isolate), static_cast<int>(argv.size()), argv.data()).ToLocal(&returned);
                if (!called) {
                    thrown = state.CopyOut(owner.isolate, entered.context, tryCatch.HasCaught() ? tryCatch.Exception() : v8::Local<v8::Value>(), ownerHooks, "The call failed");
                }
                else if (returned->IsPromise()) {
                    settlement = state.Await(ownerId, standIn->runtime, entered.context, returned.As<v8::Promise>());
                }
                else {
                    auto copied = ValueTransfer::Copy(owner.isolate, entered.context, returned, ownerHooks);
                    if (copied) {
                        result = copied.GetValue();
                    }
                    else {
                        thrown = state.ErrorValue("Return value: " + copied.GetError());
                    }
                }
            }

            const TransferFunctions hooks = state.Hooks(standIn->runtime);
            if (thrown) {
                v8::Local<v8::Value> error;
                if (ValueTransfer::Rebuild(isolate, context, *thrown, hooks).ToLocal(&error)) {
                    isolate->ThrowException(error);
                }
                return;
            }
            if (settlement != nullptr) {
                v8::Local<v8::Promise::Resolver> resolver;
                if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
                    return;
                }
                settlement->resolver.Reset(isolate, resolver);
                info.GetReturnValue().Set(resolver->GetPromise());
                return;
            }
            v8::Local<v8::Value> rebuilt;
            if (ValueTransfer::Rebuild(isolate, context, *result, hooks).ToLocal(&rebuilt)) {
                info.GetReturnValue().Set(rebuilt);
            }
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
                return copied.GetValue();
            }
            return ErrorValue(ToUtf8(isolate, value) + " (" + copied.GetError() + ")");
        }

        // Called inside the owner: react to its promise, and settle the caller's when it does.
        Settlement *Await(uint32_t owner, uint32_t caller, v8::Local<v8::Context> context, v8::Local<v8::Promise> promise) {
            v8::Isolate *isolate      = context->GetIsolate();
            auto settlement           = std::make_unique<Settlement>();
            settlement->state         = this;
            settlement->owner         = owner;
            settlement->caller        = caller;
            v8::Local<v8::External> data = v8::External::New(isolate, settlement.get());

            v8::Local<v8::Function> onFulfilled;
            v8::Local<v8::Function> onRejected;
            if (!v8::Function::New(context, OnFulfilled, data).ToLocal(&onFulfilled) || !v8::Function::New(context, OnRejected, data).ToLocal(&onRejected) || promise->Then(context, onFulfilled, onRejected).IsEmpty()) {
                return nullptr;
            }
            Settlement *raw = settlement.get();
            settlements.emplace(raw, std::move(settlement));
            return raw;
        }

        static void OnFulfilled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Settle(info, true);
        }

        static void OnRejected(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Settle(info, false);
        }

        // Runs inside the owner, from its microtask queue.
        static void Settle(const v8::FunctionCallbackInfo<v8::Value> &info, bool fulfilled) {
            auto *settlement = static_cast<Settlement *>(info.Data().As<v8::External>()->Value());
            State &state     = *settlement->state;
            const auto it    = state.settlements.find(settlement);
            if (it == state.settlements.end()) {
                return;
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
            if (fulfilled) {
                auto result = ValueTransfer::Copy(isolate, isolate->GetCurrentContext(), value, state.Hooks(owned->owner));
                if (result) {
                    copied = result.GetValue();
                }
                else {
                    copied    = ErrorValue("Resolved value: " + result.GetError());
                    fulfilled = false;
                }
            }
            else {
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
        for (auto &[raw, standIn] : _state->standIns) {
            standIn->handle.Reset();
        }
        _state->standIns.clear();
        _state->settlements.clear();
        _state->exports.clear();
        _state->runtimes.clear();
    }

    uint32_t FunctionReferences::AddRuntime(v8::Isolate *isolate, v8::Local<v8::Context> context, std::string name) {
        const uint32_t id      = _state->nextRuntime++;
        State::Runtime &entry  = _state->runtimes[id];
        entry.isolate          = isolate;
        entry.context.Reset(isolate, context);
        entry.name = std::move(name);
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
        state.runtimes.erase(it);

        for (auto &settlement : owed) {
            const auto caller = state.runtimes.find(settlement->caller);
            if (settlement->resolver.IsEmpty() || caller == state.runtimes.end()) {
                continue;
            }
            State::Entered entered(caller->second);
            v8::Local<v8::Value> error = v8::Exception::Error(State::Text(caller->second.isolate, name + " stopped before the call finished"));
            settlement->resolver.Get(caller->second.isolate)->Reject(entered.context, error).Check();
        }
    }

    TransferFunctions FunctionReferences::For(uint32_t runtime) const {
        return _state->Hooks(runtime);
    }

    size_t FunctionReferences::GetExportCount() const {
        return _state->exports.size();
    }

    size_t FunctionReferences::GetStandInCount() const {
        return _state->standIns.size();
    }

} // namespace Framework::Scripting
