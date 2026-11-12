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
#include "../bind/registrar.h"
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
    class ResourceManager;
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

        // Makes the class known to the runtime, for `entity.state` to give. No language gets a global: there is
        // nothing for a script to construct.
        static void Register(Bind::Registrar &registrar, Bind::Languages languages = Bind::Languages::All);

        // Drops the change subscriptions of a stopping resource of `manager`, mirroring
        // Events::CleanupResource. Without it a stopped resource's handler keeps running, and its
        // retained function keeps the resource's objects alive. The resource is told by its manager and
        // its name, whatever language it is written in, so two managers in one process do not drop
        // each other's. The manager calls it for every resource it stops.
        static void CleanupResource(const ResourceManager *manager, const std::string &resourceName);

        // The same for the manager `isolate` answers with (Bind::SetCallResourceManager).
        static void CleanupResource(v8::Isolate *isolate, const std::string &resourceName);

        // Drops every subscription made under `manager`, which is going: none is to reach it again.
        static void ForgetManager(const ResourceManager *manager);

        // Drops the subscriptions whose handler can no longer run because its runtime is gone. What hears of
        // a Lua runtime going calls it: the disposing listener of the Lua engine, added after
        // Bind::WatchRuntimes, which is what tells the handlers.
        static void DropDeadSubscriptions();

        // Diagnostics and tests: the live `onChange` registrations, of every runtime.
        static size_t SubscriptionCount();

        // Scalars map directly; anything else is serialized to JSON. Returns false with an exception
        // pending when the value cannot be represented — a cycle or a function: the TypeError `set`
        // throws, since this is the conversion `set` runs.
        static bool ToStateValue(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> input, Networking::Replication::StateValue &out);

        // The reverse, as `get` and a change handler are given a value. Unparseable Json comes back as
        // raw text rather than throwing, so a script never has to guard a read.
        static v8::Local<v8::Value> FromStateValue(v8::Isolate *isolate, v8::Local<v8::Context> context, const Networking::Replication::StateValue &value);

      private:
        Networking::Replication::StateBag *Resolve() const;

        // The manager subscriptions were made under, until it goes: what a failing handler is reported to.
        struct ManagerLink {
            ResourceManager *manager = nullptr;
        };

        // One live `onChange` registration: the script's function, of either language, the resource that
        // owns it and that resource's manager so a stop can drop it, and the replication-side handle to
        // cancel.
        struct Subscription {
            std::shared_ptr<Bind::ScriptCallable> callback;
            std::string resourceName;
            std::shared_ptr<ManagerLink> manager;
            Networking::Replication::StateChangeHandle handle = Networking::Replication::kInvalidStateChangeHandle;
        };

        static void Unsubscribe(uint32_t id);

        // Drops every subscription `drop` picks.
        static void DropSubscriptions(const std::function<bool(const Subscription &)> &drop);

        // Calls one live subscription's handler with a change the filter already accepted.
        static void Dispatch(uint32_t id, const Networking::Replication::StateChange &change);

        // The link of `manager`, shared by everything made under it. Never null; of no manager for nullptr.
        static std::shared_ptr<ManagerLink> LinkOf(ResourceManager *manager);

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
        inline static std::unordered_map<const ResourceManager *, std::shared_ptr<ManagerLink>> _managers;
        inline static uint32_t _nextSubscriptionId = 0;
    };
} // namespace Framework::Scripting::Builtins

FW_SCRIPT_CLASS(Framework::Scripting::Builtins::StateBag, "StateBag");
