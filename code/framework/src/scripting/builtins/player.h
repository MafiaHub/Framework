/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "entity.h"

#include <core_modules.h>
#include <networking/network_peer.h>

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

namespace Framework::Scripting::Builtins {
    // A connection's player entity. Connection-level ops (kick, ...) live here, not on Entity, so
    // they stay off non-player entities. Mods derive via inherit<Player>().
    class Player: public Entity {
      public:
        Player(uint64_t networkId): Entity(networkId) {}

        // Server-only; a no-op on the client (see NetworkPeer::KickPlayer).
        void Kick(const std::string &reason) {
            auto *entity = Resolve();
            if (!entity) {
                return;
            }
            auto *peer = CoreModules::GetNetworkPeer();
            if (!peer) {
                return;
            }
            peer->KickPlayer(MafiaNet::ToGuid(entity->ownerGUID),
                reason.empty() ? Networking::DisconnectionReason::KICKED : Networking::DisconnectionReason::KICKED_CUSTOM,
                reason);
        }

        std::string ToString() const override {
            std::ostringstream ss;
            ss << "Player{ id: " << _id << " }";
            return ss.str();
        }

        static v8pp::class_<Player> &GetClass(v8::Isolate *isolate) {
            auto it = _classes.find(isolate);
            if (it != _classes.end()) {
                return *it->second;
            }

            // v8pp inherit<Entity> requires Entity registered first.
            Entity::GetClass(isolate);

            auto &cls = _classes[isolate];
            cls = std::make_unique<v8pp::class_<Player>>(isolate);
            cls->auto_wrap_objects(true);
            cls->inherit<Entity>()
                .ctor<uint64_t>()
                .function("toString", &Player::ToString)
                .function("kick", &Player::Kick);
            return *cls;
        }

      protected:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Player>>> _classes;
    };
} // namespace Framework::Scripting::Builtins
