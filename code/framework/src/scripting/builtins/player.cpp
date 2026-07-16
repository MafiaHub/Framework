/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "player.h"

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
        peer->KickPlayer(MafiaNet::ToGuid(entity->ownerGUID),
            reason.empty() ? Networking::DisconnectionReason::KICKED : Networking::DisconnectionReason::KICKED_CUSTOM,
            reason);
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
        cls = std::make_unique<v8pp::class_<Player>>(isolate);
        cls->auto_wrap_objects(true);
        cls->inherit<Entity>()
            .ctor<uint64_t>()
            .function("toString", &Player::ToString)
            .function("kick", &Player::Kick)
            .function("emit", &Player::Emit)
            .function("getIP", &Player::GetAddress)
            .property("steamId", &Player::GetSteamId)
            .property("discordId", &Player::GetDiscordId)
            .property("hardwareId", &Player::GetHardwareId)
            .property("ping", &Player::GetPing)
            .property("ip", &Player::GetAddress);
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
} // namespace Framework::Scripting::Builtins
