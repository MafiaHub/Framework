/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../bind/callable.h"
#include "../bind/class.h"
#include "../class_cache.h"

#include <networking/replication/replication_manager.h>
#include <networking/replication/state_bag.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Framework::Scripting {
    class LuaRuntime;
} // namespace Framework::Scripting

namespace Framework::Scripting::Builtins {
    // Scripting handle for one entity's state bag, reached as `entity.state`.
    //
    // Holds the entity's NetworkID rather than a pointer to the bag, for the same reason Entity does:
    // a handle kept across a destruction resolves to nothing instead of dangling. A method called on
    // a stale handle is a silent no-op, per the conventions in README.md.
    //
    // Not constructible from script: there is no bag without an entity to hang it on.
    class StateBag final {
      public:
        explicit StateBag(uint64_t networkId): _id(networkId) {}

        uint64_t GetId() const {
            return _id;
        }

        // The class as scripts see it, declared once for both languages.
        static const Bind::Class<StateBag> &Definition();

        static v8pp::class_<StateBag> &GetClass(v8::Isolate *isolate);
        static v8::Local<v8::Object> NewInstance(v8::Isolate *isolate, uint64_t networkId);

        // Drops the class of the isolate, the subscriptions its scripts hold and those whose runtime is gone.
        static void UnregisterIsolate(v8::Isolate *isolate);

        // Makes the class exist in the state of a Lua runtime, for `entity.state` to give. Like JavaScript, Lua
        // gets no global: there is nothing for a script to construct. A failure, such as the state running out
        // of memory, is a Lua error: call it protected, or it is a panic.
        static void RegisterLua(LuaRuntime &runtime);

        // Drops the change subscriptions a stopping resource registered, mirroring
        // Events::CleanupResource. Without it a stopped resource's handler keeps running, and its
        // retained function keeps the resource's objects alive. The resource is told by its name alone,
        // whatever language it is written in: `isolate` is whichever the caller has entered.
        static void CleanupResource(v8::Isolate *isolate, const std::string &resourceName);

        // Drops the subscriptions whose handler can no longer run because its runtime is gone. What hears of
        // a Lua runtime going calls it: the disposing listener of the Lua engine, added after
        // Bind::WatchRuntimes, which is what tells the handlers.
        static void DropDeadSubscriptions();

        // Diagnostics and tests: the live `onChange` registrations, of every runtime.
        static size_t SubscriptionCount();

        // Scalars map directly; anything else is serialized to JSON. Returns false with an exception
        // pending when the value cannot be represented — a cycle, chiefly.
        static bool ToStateValue(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> input, Networking::Replication::StateValue &out);

        // The reverse. Unparseable Json comes back as raw text rather than throwing, so a script
        // never has to guard a read.
        static v8::Local<v8::Value> FromStateValue(v8::Isolate *isolate, v8::Local<v8::Context> context, const Networking::Replication::StateValue &value);

      private:
        Networking::Replication::StateBag *Resolve() const;

        // One live `onChange` registration: the script's function, of either language, the resource that
        // registered it so a stop can drop it, and the replication-side handle to cancel.
        struct Subscription {
            std::shared_ptr<Bind::ScriptCallable> callback;
            std::string resourceName;
            Networking::Replication::StateChangeHandle handle = Networking::Replication::kInvalidStateChangeHandle;
        };

        static void Unsubscribe(uint32_t id);

        // Drops every subscription `drop` picks.
        static void DropSubscriptions(const std::function<bool(const Subscription &)> &drop);

        // Calls one live subscription's handler with a change the filter already accepted.
        static void Dispatch(uint32_t id, const Networking::Replication::StateChange &change);

        // The live bag behind a call's receiver, or nullptr when the handle is stale or not a
        // StateBag at all. Both cases are a silent no-op, so callers need not tell them apart —
        // argument checks still run first, because a bad argument throws even on a dead receiver.
        static Networking::Replication::StateBag *ResolveFrom(const Bind::Call &call);

        // What a script calls.
        static void Get(Bind::Call &call);
        static void Has(Bind::Call &call);
        static void Keys(Bind::Call &call);
        static void ToObject(Bind::Call &call);
        static void OnChange(Bind::Call &call);
        static void Set(Bind::Call &call);
        static void Remove(Bind::Call &call);

        uint64_t _id = 0;
        inline static ClassCache<StateBag> _classes;
        inline static std::unordered_map<uint32_t, Subscription> _subscriptions;
        inline static uint32_t _nextSubscriptionId = 0;
    };
} // namespace Framework::Scripting::Builtins

FW_SCRIPT_CLASS(Framework::Scripting::Builtins::StateBag, "StateBag");
