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
#include <networking/rpc/nametag.h>
#include <networking/replication/nametag_state.h>

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

    std::string Player::GetClientKind() const {
        const auto *identity = ResolveIdentity();
        if (!identity) {
            return "unknown";
        }
        return identity->clientKind == Networking::RPC::ClientKind::Headless ? "headless" : "game";
    }

    uint16_t Player::GetCapabilityProtocolVersion() const {
        const auto *identity = ResolveIdentity();
        return identity ? identity->capabilityProtocolVersion : 0;
    }

    uint32_t Player::GetCapabilityFlags() const {
        const auto *identity = ResolveIdentity();
        return identity ? identity->capabilities : 0;
    }

    v8::Local<v8::Object> Player::GetCapabilities(v8::Isolate *isolate) const {
        auto context = isolate->GetCurrentContext();
        auto value   = v8::Object::New(isolate);
        const uint32_t flags = GetCapabilityFlags();
        const auto set = [&](const char *key, v8::Local<v8::Value> field) {
            (void)value->Set(context, v8pp::to_v8(isolate, key), field);
        };
        const auto boolean = [&](uint32_t flag) {
            return v8::Boolean::New(isolate, (flags & flag) == flag);
        };
        set("clientKind", v8pp::to_v8(isolate, GetClientKind()));
        set("protocolVersion", v8pp::to_v8(isolate, GetCapabilityProtocolVersion()));
        set("scriptEvents", boolean(Networking::RPC::ClientCapability::ScriptEvents));
        set("replication", boolean(Networking::RPC::ClientCapability::Replication));
        set("gameWorld", boolean(Networking::RPC::ClientCapability::GameWorld));
        set("nativePlayer", boolean(Networking::RPC::ClientCapability::NativePlayer));
        set("ui", boolean(Networking::RPC::ClientCapability::UI));
        set("input", boolean(Networking::RPC::ClientCapability::Input));
        return value;
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

    Networking::Replication::NametagState *Player::ResolveNametag() const {
        auto *entity = Resolve();
        return entity ? entity->GetNametag() : nullptr;
    }

    void Player::SendNametag(const Networking::Replication::NametagState &state) const {
        // Server-only, like Kick; the getters still read the local replica.
        if (CoreModules::GetClientInstance()) {
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
        Networking::RPC::SetNametagState msg;
        msg.networkId  = entity->GetNetworkID();
        msg.components = state.components;
        msg.color      = state.color;
        msg.text       = state.text;
        peer->SendRPC(msg, MafiaNet::ToGuid(entity->ownerGUID));
    }

    void Player::SetNametagComponent(Networking::Replication::NametagComponent component, bool enabled) {
        const auto *current = ResolveNametag();
        if (!current) {
            return;
        }
        Networking::Replication::NametagState next = *current;
        next.Set(component, enabled);
        SendNametag(next);
    }

    bool Player::HasNametagComponent(Networking::Replication::NametagComponent component) const {
        const auto *state = ResolveNametag();
        return state != nullptr && state->Has(component);
    }

    void Player::SetNametagVisible(bool visible) {
        SetNametagComponent(Networking::Replication::NametagComponent::Name, visible);
    }

    bool Player::IsNametagVisible() const {
        return HasNametagComponent(Networking::Replication::NametagComponent::Name);
    }

    void Player::SetNametagHealthVisible(bool visible) {
        SetNametagComponent(Networking::Replication::NametagComponent::Health, visible);
    }

    bool Player::IsNametagHealthVisible() const {
        return HasNametagComponent(Networking::Replication::NametagComponent::Health);
    }

    void Player::SetNametagText(const std::string &text) {
        const auto *current = ResolveNametag();
        if (!current) {
            return;
        }
        Networking::Replication::NametagState next = *current;
        next.text                                  = text;
        SendNametag(next);
    }

    std::string Player::GetNametagText() const {
        const auto *state = ResolveNametag();
        return state ? state->text : "";
    }

    void Player::SetNametagColor(uint32_t color) {
        const auto *current = ResolveNametag();
        if (!current) {
            return;
        }
        Networking::Replication::NametagState next = *current;
        next.color                                 = color;
        SendNametag(next);
    }

    uint32_t Player::GetNametagColor() const {
        const auto *state = ResolveNametag();
        return state ? state->color : 0xFFFFFFFF;
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
            .function("setNametagVisible", &Player::SetNametagVisible,
                v8pp::metadata::docs("void", {v8pp::metadata::param("visible", "boolean", false, "True to show this player's nametag to everyone, false to hide it.")},
                    "Shows or hides the name over this player's head for every other player. The health bar has its own switch, and each player can still hide all nametags locally."))
            .function("isNametagVisible", &Player::IsNametagVisible,
                v8pp::metadata::docs("boolean", {}, "Checks whether this player's nametag is shown to other players.", "True unless the nametag was hidden; false also when this game has no nametags."))
            .function("setNametagHealthVisible", &Player::SetNametagHealthVisible,
                v8pp::metadata::docs("void", {v8pp::metadata::param("visible", "boolean", false, "True to show the health bar under this player's name, false to hide it.")},
                    "Shows or hides the health bar under this player's nametag, leaving the name itself alone."))
            .function("isNametagHealthVisible", &Player::IsNametagHealthVisible,
                v8pp::metadata::docs("boolean", {}, "Checks whether the health bar under this player's nametag is shown.", "True unless the health bar was hidden; false also when this game has no nametags."))
            .function("setNametagText", &Player::SetNametagText,
                v8pp::metadata::docs("void", {v8pp::metadata::param("text", "string", true, "Text to show instead of the player's name; empty or omitted restores the name.")},
                    "Overrides the text drawn on this player's nametag."))
            .function("getNametagText", &Player::GetNametagText,
                v8pp::metadata::docs("string", {}, "Reads this player's nametag text override.", "The override, or an empty string when the player's own name is drawn."))
            .function("setNametagColor", &Player::SetNametagColor,
                v8pp::metadata::docs("void", {v8pp::metadata::param("color", "number", false, "Packed 0xAARRGGBB color.")},
                    "Tints the text on this player's nametag."))
            .function("getNametagColor", &Player::GetNametagColor,
                v8pp::metadata::docs("number", {}, "Reads this player's nametag color.", "Packed 0xAARRGGBB color; opaque white when untinted."))
            .property("steamId", &Player::GetSteamId, v8pp::metadata::property_docs("string", "Authenticated Steam identifier, or an empty string when unavailable."))
            .property("discordId", &Player::GetDiscordId, v8pp::metadata::property_docs("string", "Authenticated Discord identifier, or an empty string when unavailable."))
            .property("hardwareId", &Player::GetHardwareId, v8pp::metadata::property_docs("string", "Framework hardware identifier, or an empty string when unavailable."))
            .property("clientKind", &Player::GetClientKind, v8pp::metadata::property_docs("\"game\" | \"headless\" | \"unknown\"", "Client implementation announced during the connection handshake; informational, not authorization."))
            .property("capabilities", &Player::GetCapabilities, v8pp::metadata::property_options{"Negotiated client features; client-announced and informational, never an authorization source.", "PlayerCapabilities", true})
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

    void Player::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<Player> &cls = GetClass(isolate);
        auto ctx                  = isolate->GetCurrentContext();
        global->Set(ctx, v8pp::to_v8(isolate, "Player"), cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
    }

    void Player::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }
} // namespace Framework::Scripting::Builtins
