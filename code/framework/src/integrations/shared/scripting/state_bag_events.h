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
#include <scripting/builtins/state_bag.h>
#include <scripting/engine.h>
#include <scripting/module.h>
#include <scripting/resource/resource_manager.h>

#include <function2.hpp>
#include <v8.h>
#include <v8pp/convert.hpp>

#include <cstdint>
#include <vector>

namespace Framework::Integrations::Shared::Scripting {
    // Raises the `entityStateChange` event for every state-bag change on this peer: the server when a
    // script writes one, a client when one arrives, including the keys a construction seed carried.
    //
    // Both integration instances call this, so a mod gets the event by existing rather than by
    // reimplementing it. `wrap` is the one part a game has an opinion about -- which handle its
    // scripts should see for an entity -- and is the Instance::WrapScriptEntity override.
    //
    // Returns the subscription handle; hand it back to RemoveStateChangeHandler on shutdown.
    inline Framework::Networking::Replication::StateChangeHandle InstallStateBagEvents(fu2::function<v8::Local<v8::Value>(v8::Isolate *, uint64_t) const> wrap) {
        auto *replication = CoreModules::GetReplication();
        if (replication == nullptr) {
            return Framework::Networking::Replication::kInvalidStateChangeHandle;
        }

        // An empty filter: this is the global bus, and a script that wants one entity or one key
        // subscribes through entity.state.onChange instead of being woken here.
        return replication->AddStateChangeHandler({}, [wrap = std::move(wrap)](const Framework::Networking::Replication::StateChange &change) {
            if (change.entity == nullptr) {
                return;
            }

            auto *module = CoreModules::GetScriptingModule();
            auto *engine = module != nullptr ? module->GetScriptingEngine() : nullptr;
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

            using Bag = Framework::Scripting::Builtins::StateBag;

            // Undefined rather than null for a removal and for a key that held nothing, so a stored
            // null stays distinguishable from an absent one.
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(wrap(isolate, change.entity->GetNetworkID()));
            args.push_back(v8pp::to_v8(isolate, change.key));
            args.push_back(change.removed ? v8::Local<v8::Value>(v8::Undefined(isolate)) : Bag::FromStateValue(isolate, context, change.value));
            args.push_back(change.hadPrevious ? Bag::FromStateValue(isolate, context, change.previous) : v8::Local<v8::Value>(v8::Undefined(isolate)));

            resources->GetEvents().EmitReserved(isolate, context, "entityStateChange", args);
        });
    }
} // namespace Framework::Integrations::Shared::Scripting
