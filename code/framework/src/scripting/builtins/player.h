/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "entity.h"

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Framework::Networking::RPC {
    struct ClientIdentity;
} // namespace Framework::Networking::RPC

namespace Framework::Scripting::Builtins {
    // A connection's player entity. Connection-level ops (kick, ...) live here, not on Entity, so
    // they stay off non-player entities. Mods derive via inherit<Player>().
    class Player: public Entity {
      public:
        Player(uint64_t networkId): Entity(networkId) {}

        // Server-only; a no-op on the client (see NetworkPeer::KickPlayer).
        void Kick(const std::string &reason);

        // Send a named event to this player's client, received there as Core.Events.on(name, data).
        // Server-only; payloadJson is JSON.parsed on the client (pass JSON text, empty = no data).
        void Emit(const std::string &eventName, const std::string &payloadJson);

        // Client-announced identity (RPC::ClientIdentity). Server-only, unverified; empty when absent.
        std::string GetSteamId() const;
        std::string GetDiscordId() const;
        std::string GetHardwareId() const;

        // Connection's average round-trip time in ms. Server-only; -1 when unavailable.
        int GetPing() const;

        // Connection's remote IP address (no port). Server-only; empty when unavailable.
        std::string GetAddress() const;

        std::string ToString() const override;

        static v8pp::class_<Player> &GetClass(v8::Isolate *isolate);

      protected:
        const Networking::RPC::ClientIdentity *ResolveIdentity() const;

        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Player>>> _classes;
    };
} // namespace Framework::Scripting::Builtins
