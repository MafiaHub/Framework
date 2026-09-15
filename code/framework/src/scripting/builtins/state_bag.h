/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <networking/replication/replication_manager.h>
#include <networking/replication/state_bag.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

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

        static v8pp::class_<StateBag> &GetClass(v8::Isolate *isolate);
        static v8::Local<v8::Object> NewInstance(v8::Isolate *isolate, uint64_t networkId);
        static void UnregisterIsolate(v8::Isolate *isolate);

        // Drops the change subscriptions a stopping resource registered, mirroring
        // Events::CleanupResource. Without it a stopped resource's handler keeps running, and its
        // retained function keeps the resource's objects alive.
        static void CleanupResource(v8::Isolate *isolate, const std::string &resourceName);

        // Scalars map directly; anything else is serialized to JSON. Returns false with an exception
        // pending when the value cannot be represented — a cycle, chiefly.
        static bool ToStateValue(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> input, Networking::Replication::StateValue &out);

        // The reverse. Unparseable Json comes back as raw text rather than throwing, so a script
        // never has to guard a read.
        static v8::Local<v8::Value> FromStateValue(v8::Isolate *isolate, v8::Local<v8::Context> context, const Networking::Replication::StateValue &value);

      private:
        Networking::Replication::StateBag *Resolve() const;

        // One live `onChange` registration: the script's function, the resource it belongs to so a
        // stop can drop it, and the replication-side handle to cancel.
        struct Subscription {
            v8::Global<v8::Function> callback;
            std::string resourceName;
            Networking::Replication::StateChangeHandle handle = Networking::Replication::kInvalidStateChangeHandle;
        };

        static void Unsubscribe(v8::Isolate *isolate, uint32_t id);

        // The live bag behind a call's receiver, or nullptr when the handle is stale or not a
        // StateBag at all. Both cases are a silent no-op, so callers need not tell them apart —
        // argument checks still run first, because a bad argument throws even on a dead receiver.
        static Networking::Replication::StateBag *ResolveFrom(const v8::FunctionCallbackInfo<v8::Value> &info);

        uint64_t _id = 0;
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<StateBag>>> _classes;
        inline static std::unordered_map<v8::Isolate *, std::unordered_map<uint32_t, Subscription>> _subscriptions;
        inline static uint32_t _nextSubscriptionId = 0;
    };
} // namespace Framework::Scripting::Builtins
