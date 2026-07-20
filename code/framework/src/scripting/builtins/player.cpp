/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "player.h"
#include "../scripting_catalog.h"

#include <core_modules.h>
#include <integrations/shared/rpc/emit_script_event.h>
#include <networking/network_peer.h>
#include <networking/rpc/client_identity.h>

#include <sstream>

namespace Framework::Scripting::Builtins {
    void Player::Kick(const std::string &reason) {
        auto *entity = Resolve();
        if (!entity) {
            return;
        }
        auto *peer = CoreModules::GetNetworkPeer();
        if (!peer) {
            return;
        }
        peer->KickPlayer(MafiaNet::ToGuid(entity->ownerGUID), reason.empty() ? Networking::DisconnectionReason::KICKED : Networking::DisconnectionReason::KICKED_CUSTOM, reason);
    }

    void Player::Emit(const std::string &eventName, const std::string &payloadJson) {
        if (eventName.empty()) {
            return;
        }
        auto *entity = Resolve();
        if (!entity) {
            return;
        }
        auto *peer = CoreModules::GetNetworkPeer();
        if (!peer) {
            return;
        }
        Framework::Integrations::Shared::RPC::EmitScriptEvent ev;
        ev.FromParameters(eventName, payloadJson);
        peer->SendRPC(ev, MafiaNet::ToGuid(entity->ownerGUID));
    }

    std::string Player::GetSteamId() const {
        const auto *identity = ResolveIdentity();
        return identity ? identity->steamId : "";
    }

    std::string Player::GetDiscordId() const {
        const auto *identity = ResolveIdentity();
        return identity ? identity->discordId : "";
    }

    std::string Player::GetHardwareId() const {
        const auto *identity = ResolveIdentity();
        return identity ? identity->hardwareId : "";
    }

    int Player::GetPing() const {
        const auto *entity = Resolve();
        if (!entity) {
            return -1;
        }
        auto *peer = CoreModules::GetNetworkPeer();
        return peer ? peer->GetPing(MafiaNet::ToGuid(entity->ownerGUID)) : -1;
    }

    std::string Player::GetAddress() const {
        const auto *entity = Resolve();
        if (!entity) {
            return "";
        }
        auto *peer = CoreModules::GetNetworkPeer();
        return peer ? peer->GetAddress(MafiaNet::ToGuid(entity->ownerGUID)) : "";
    }

    std::string Player::ToString() const {
        std::ostringstream ss;
        ss << "Player{ id: " << _id << " }";
        return ss.str();
    }

    v8pp::class_<Player> &Player::GetClass(v8::Isolate *isolate) {
        auto it = _classes.find(isolate);
        if (it != _classes.end()) {
            return *it->second;
        }

        // v8pp inherit<Entity> requires Entity registered first.
        Entity::GetClass(isolate);

        auto &cls = _classes[isolate];
        cls       = std::make_unique<v8pp::class_<Player>>(isolate, GetScriptingCatalog(isolate), "Player", "Framework-owned base handle for a connected player, extended by each game or mod.");
        cls->auto_wrap_objects(true);
        cls->document_base("Entity");
        cls->inherit<Entity>()
            .ctor<uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a wrapper for an existing connected player with this ID; it does not connect or spawn a player."))
            .function("toString", &Player::ToString, v8pp::metadata::docs("string", {}, "Formats this player handle for logging and debugging.", "Text containing the player's network entity ID."))
            .function("kick", &Player::Kick, v8pp::metadata::docs("void", {v8pp::metadata::param("reason", "string", true, "Optional reason shown to the disconnected player; omitting it uses the generic kicked reason.")}, "Disconnects this player from the server."))
            .function("emit", &Player::Emit,
                v8pp::metadata::docs("void", {v8pp::metadata::param("eventName", "string", false, "Client event name."), v8pp::metadata::param("payloadJson", "string", true, "Optional JSON payload forwarded verbatim to the owning client.")},
                    "Emits a named script event to this player's client connection."))
            .function("getIP", &Player::GetAddress, v8pp::metadata::docs("string", {}, "Returns this player's current remote network address.", "Address string, or an empty string when the player or peer is unavailable."))
            .property("steamId", &Player::GetSteamId, v8pp::metadata::property_docs("string", "Authenticated Steam identifier, or an empty string when unavailable."))
            .property("discordId", &Player::GetDiscordId, v8pp::metadata::property_docs("string", "Authenticated Discord identifier, or an empty string when unavailable."))
            .property("hardwareId", &Player::GetHardwareId, v8pp::metadata::property_docs("string", "Framework hardware identifier, or an empty string when unavailable."))
            .property("ping", &Player::GetPing, v8pp::metadata::property_docs("number", "Current round-trip latency in milliseconds, or -1 when unavailable."))
            .property("ip", &Player::GetAddress, v8pp::metadata::property_docs("string", "Current remote network address, or an empty string when unavailable."));
        return *cls;
    }

    const Networking::RPC::ClientIdentity *Player::ResolveIdentity() const {
        const auto *entity = Resolve();
        if (!entity) {
            return nullptr;
        }
        auto *peer = CoreModules::GetNetworkPeer();
        return peer ? peer->GetPeerIdentity(MafiaNet::ToGuid(entity->ownerGUID)) : nullptr;
    }

    void Player::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }
} // namespace Framework::Scripting::Builtins
