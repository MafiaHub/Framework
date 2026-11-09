/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "player.h"
#include "../api/binding.h"
#include "../api/native_value_types.h"
#include "../scripting_catalog.h"

#include <core_modules.h>
#include <integrations/shared/rpc/emit_script_event.h>
#include <networking/network_peer.h>
#include <networking/replication/nametag_state.h>
#include <networking/rpc/client_identity.h>
#include <utils/string_utils.h>

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

    std::string Player::GetEpicId() const {
        const auto *identity = ResolveIdentity();
        return identity ? identity->epicId : "";
    }

    std::string Player::GetDiscordId() const {
        const auto *identity = ResolveIdentity();
        return identity ? identity->discordId : "";
    }

    std::string Player::GetDiscordName() const {
        const auto *identity = ResolveIdentity();
        return identity ? identity->discordName : "";
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

    Networking::Replication::NametagState *Player::ResolveNametag() const {
        auto *entity = Resolve();
        return entity ? entity->GetNametag() : nullptr;
    }

    Networking::Replication::NametagState *Player::WritableNametag() const {
        // Server-only, like Kick: the state is a ServerField, so a client write would never leave it.
        return CoreModules::GetClientInstance() ? nullptr : ResolveNametag();
    }

    void Player::SetNametagComponent(Networking::Replication::NametagComponent component, bool enabled) {
        if (auto *state = WritableNametag()) {
            state->Set(component, enabled);
        }
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
        if (auto *state = WritableNametag()) {
            state->text = text;
            Utils::StringUtils::TruncateUtf8(state->text, Networking::Replication::NametagState::kMaxTextBytes);
        }
    }

    std::string Player::GetNametagText() const {
        const auto *state = ResolveNametag();
        return state ? state->text : "";
    }

    void Player::SetNametagColor(uint32_t color) {
        if (auto *state = WritableNametag()) {
            state->color = color;
        }
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

    const Api::ClassBinding<Player> &Player::Binding() {
        using Api::Side;
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;

        static const Api::ClassBinding<Player> &binding = *[] {
            auto *b = new Api::ClassBinding<Player>("Player", "Framework-owned base handle for a connected player, extended by each game or mod.");
            b->Inherits<Entity>()
                .Constructor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a wrapper for an existing connected player with this ID; it does not connect or spawn a player."))
                .Method<&Player::ToString>("toString", docs("string", {}, "Formats this player handle for logging and debugging.", "Text containing the player's network entity ID."))
                .Method<&Player::IsNametagVisible>("isNametagVisible", docs("boolean", {}, "Checks whether this player's nametag is shown to other players.", "True unless the nametag was hidden; false also when this game has no nametags."))
                .Method<&Player::IsNametagHealthVisible>("isNametagHealthVisible", docs("boolean", {}, "Checks whether the health bar under this player's nametag is shown.", "True unless the health bar was hidden; false also when this game has no nametags."))
                .Method<&Player::GetNametagText>("getNametagText", docs("string", {}, "Reads this player's nametag text override.", "The override, or an empty string when the player's own name is drawn."))
                .Method<&Player::GetNametagColor>("getNametagColor", docs("number", {}, "Reads this player's nametag color.", "Packed 0xAARRGGBB color; opaque white when untinted."));

            // Connection-level surface; NetworkPeer implements it only in NetworkServer. The getters above
            // stay on both sides, they read the replica.
            b->Method<&Player::Kick>("kick", docs("void", {param("reason", "string", true, "Optional reason shown to the disconnected player; omitting it uses the generic kicked reason.")}, "Disconnects this player from the server."), Side::Server)
                .Method<&Player::Emit>("emit",
                    docs("void", {param("eventName", "string", false, "Client event name."), param("payloadJson", "string", true, "Optional JSON payload forwarded verbatim to the owning client.")}, "Emits a named script event to this player's client connection."), Side::Server)
                .Method<&Player::GetAddress>("getIP", docs("string", {}, "Returns this player's current remote network address.", "Address string, or an empty string when the player or peer is unavailable."), Side::Server)
                .Method<&Player::SetNametagVisible>("setNametagVisible",
                    docs("void", {param("visible", "boolean", false, "True to show this player's nametag to everyone, false to hide it.")},
                        "Shows or hides the name over this player's head for every other player. The health bar has its own switch, and each player can still hide all nametags locally."),
                    Side::Server)
                .Method<&Player::SetNametagHealthVisible>("setNametagHealthVisible",
                    docs("void", {param("visible", "boolean", false, "True to show the health bar under this player's name, false to hide it.")}, "Shows or hides the health bar under this player's nametag, leaving the name itself alone."), Side::Server)
                .Method<&Player::SetNametagText>("setNametagText",
                    docs("void", {param("text", "string", true, "Text to show instead of the player's name, cut to 64 bytes; empty or omitted restores the name.")}, "Overrides the text drawn on this player's nametag."), Side::Server)
                .Method<&Player::SetNametagColor>("setNametagColor", docs("void", {param("color", "number", false, "Packed 0xAARRGGBB color.")}, "Tints the text on this player's nametag."), Side::Server)
                .Property<&Player::GetSteamId>("steamId", property_docs("string", "Authenticated Steam identifier, or an empty string when unavailable."), Side::Server)
                .Property<&Player::GetEpicId>("epicId", property_docs("string", "Epic Games account identifier the client reported, not verified by the server, or an empty string when unavailable."), Side::Server)
                .Property<&Player::GetDiscordId>("discordId", property_docs("string", "Authenticated Discord identifier, or an empty string when unavailable."), Side::Server)
                .Property<&Player::GetDiscordName>("discordName", property_docs("string", "Discord username the client reported, not verified by the server, or an empty string when unavailable."), Side::Server)
                .Property<&Player::GetHardwareId>("hardwareId", property_docs("string", "Framework hardware identifier, or an empty string when unavailable."), Side::Server)
                .Property<&Player::GetPing>("ping", property_docs("number", "Current round-trip latency in milliseconds, or -1 when unavailable."), Side::Server)
                .Property<&Player::GetAddress>("ip", property_docs("string", "Current remote network address, or an empty string when unavailable."), Side::Server);
            return b;
        }();
        return binding;
    }

    v8pp::class_<Player> &Player::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            return Binding().Build(isolate);
        });
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
        _classes.Unregister(isolate);
    }
} // namespace Framework::Scripting::Builtins
