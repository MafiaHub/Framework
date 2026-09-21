/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <core_modules.h>
#include <networking/replication/network_entity.h>
#include <networking/replication/replication_manager.h>
#include <networking/replication/state_bag.h>
#include <scripting/builtins/entity.h>
#include <scripting/builtins/state_bag.h>
#include <scripting/engine.h>
#include <scripting/module.h>
#include <scripting/resource/resource_manager.h>

#include <function2/function2.hpp>
#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <cstdint>
#include <vector>

// The `entityStateChange` event, raised by both integration instances so a mod gets it by existing
// rather than by reimplementing it. The one part a game has an opinion about is which handle its
// scripts should see, which is Instance::WrapScriptEntity.
namespace Framework::Integrations::Shared::Scripting {
    // What WrapScriptEntity answers unless a game overrides it.
    inline v8::Local<v8::Value> WrapEntityDefault(v8::Isolate *isolate, uint64_t networkId) {
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        return v8pp::class_<Framework::Scripting::Builtins::Entity>::create_object(isolate, networkId);
    }

    // The arguments an entityStateChange handler receives: (entity, key, value, previous).
    //
    // Undefined rather than null for a removed key and for one that held nothing before, because null
    // is a value a script may store and the two must stay distinguishable.
    inline std::vector<v8::Local<v8::Value>> StateChangeArgs(v8::Isolate *isolate, v8::Local<v8::Context> context, const Framework::Networking::Replication::StateChange &change, v8::Local<v8::Value> entity) {
        using Bag = Framework::Scripting::Builtins::StateBag;

        std::vector<v8::Local<v8::Value>> args;
        args.push_back(entity);
        args.push_back(v8pp::to_v8(isolate, change.key));
        args.push_back(change.removed ? v8::Local<v8::Value>(v8::Undefined(isolate)) : Bag::FromStateValue(isolate, context, change.value));
        args.push_back(change.hadPrevious ? Bag::FromStateValue(isolate, context, change.previous) : v8::Local<v8::Value>(v8::Undefined(isolate)));
        return args;
    }

    // Raises the event for every state-bag change on this peer: the server when a script writes one,
    // a client when one arrives, including the keys a construction seed carried.
    //
    // Returns the subscription handle, for ReleaseStateBagEvents on shutdown.
    inline Framework::Networking::Replication::StateChangeHandle InstallStateBagEvents(fu2::function<v8::Local<v8::Value>(v8::Isolate *, uint64_t) const> wrap) {
        auto *replication = CoreModules::GetReplication();
        if (replication == nullptr) {
            return Framework::Networking::Replication::kInvalidStateChangeHandle;
        }

        // An empty filter: this is the global bus. A script that wants one entity or one key
        // subscribes through entity.state.onChange rather than being woken here.
        return replication->AddStateChangeHandler({}, [wrap = std::move(wrap)](const Framework::Networking::Replication::StateChange &change) {
            if (change.entity == nullptr) {
                return;
            }

            auto *module    = CoreModules::GetScriptingModule();
            auto *engine    = module != nullptr ? module->GetScriptingEngine() : nullptr;
            auto *resources = module != nullptr ? module->GetResourceManager() : nullptr;
            if (engine == nullptr || resources == nullptr) {
                return;
            }

            v8::Isolate *isolate = engine->GetIsolate();
            if (isolate == nullptr) {
                return;
            }

            // A write can come from off the script thread, so the isolate is entered rather than
            // assumed. Locker nests safely when a script's own write is what got us here.
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine->GetContext();
            if (context.IsEmpty()) {
                return;
            }
            v8::Context::Scope contextScope(context);

            std::vector<v8::Local<v8::Value>> args = StateChangeArgs(isolate, context, change, wrap(isolate, change.entity->GetNetworkID()));
            resources->GetEvents().EmitReserved(isolate, context, "entityStateChange", args);
        });
    }

    // Drops the subscription and clears the handle. A handle that stayed registered past its
    // instance would have the next session's changes dispatched through it.
    inline void ReleaseStateBagEvents(Framework::Networking::Replication::StateChangeHandle &handle) {
        if (handle == Framework::Networking::Replication::kInvalidStateChangeHandle) {
            return;
        }
        if (auto *replication = CoreModules::GetReplication()) {
            replication->RemoveStateChangeHandler(handle);
        }
        handle = Framework::Networking::Replication::kInvalidStateChangeHandle;
    }
} // namespace Framework::Integrations::Shared::Scripting
