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

namespace Framework::Networking::Replication {
    enum class NametagComponent : uint8_t;
    struct NametagState;
} // namespace Framework::Networking::Replication

namespace Framework::Scripting::Builtins {
    // A connection's player entity. Connection-level ops (kick, ...) live here, not on Entity, so
    // they stay off non-player entities. Mods derive via inherit<Player>().
    class Player: public Entity {
      public:
        Player(uint64_t networkId): Entity(networkId) {}

        // Server-only; a no-op on the client (see NetworkPeer::KickPlayer).
        void Kick(const std::string &reason);

        // Send a named event to this player's client, received there as Events.on(name, data).
        // Server-only; payloadJson is JSON.parsed on the client (pass JSON text, empty = no data).
        void Emit(const std::string &eventName, const std::string &payloadJson);

        // Client-announced identity (RPC::ClientIdentity). Server-only, unverified; empty when absent.
        std::string GetSteamId() const;
        std::string GetEpicId() const;
        std::string GetDiscordId() const;
        std::string GetDiscordName() const;
        std::string GetHardwareId() const;

        // Connection's average round-trip time in ms. Server-only; -1 when unavailable.
        int GetPing() const;

        // Connection's remote IP address (no port). Server-only; empty when unavailable.
        std::string GetAddress() const;

        // Nametag over this player's avatar (games whose entity carries a NametagState). The setters
        // are server-only and write the server's copy, which every other client replicates.
        void SetNametagVisible(bool visible);
        bool IsNametagVisible() const;
        void SetNametagHealthVisible(bool visible);
        bool IsNametagHealthVisible() const;

        // Empty restores the game's own label.
        void SetNametagText(const std::string &text);
        std::string GetNametagText() const;

        // Packed 0xAARRGGBB.
        void SetNametagColor(uint32_t color);
        uint32_t GetNametagColor() const;

        std::string ToString() const override;

        // The class as scripts see it, declared once for both languages.
        static const Bind::Class<Player> &Definition();

        static v8pp::class_<Player> &GetClass(v8::Isolate *isolate);

        // Publishes the Player constructor in the languages of `languages`, like Entity::Register /
        // TextLabel::Register. In JavaScript games opt in; the framework itself exposes Player lazily
        // through GetClass (see integrations/server/instance.cpp). In Lua Entity, which it inherits, is
        // published with it.
        static void Register(Bind::Registrar &registrar, Bind::Languages languages = Bind::Languages::All);

        static void UnregisterIsolate(v8::Isolate *isolate);

      protected:
        const Networking::RPC::ClientIdentity *ResolveIdentity() const;

        Networking::Replication::NametagState *ResolveNametag() const;
        Networking::Replication::NametagState *WritableNametag() const;
        void SetNametagComponent(Networking::Replication::NametagComponent component, bool enabled);
        bool HasNametagComponent(Networking::Replication::NametagComponent component) const;

        inline static ClassCache<Player> _classes;
    };
} // namespace Framework::Scripting::Builtins

FW_SCRIPT_CLASS(Framework::Scripting::Builtins::Player, "Player");
