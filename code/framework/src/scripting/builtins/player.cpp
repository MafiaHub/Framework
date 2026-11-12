/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "player.h"
#include "../lua/lua_runtime.h"
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

    const Bind::Class<Player> &Player::Definition() {
        // The connection-level surface is the server's: NetworkPeer implements it only in NetworkServer. The
        // nametag getters stay on both sides, they read the replica.
        static const auto definition = [] {
            // Inherit looks the definition of the base up.
            Entity::Definition();
            return Bind::Class<Player>("Player", "Framework-owned base handle for a connected player, extended by each game or mod.")
                .Inherit<Entity>()
                .Constructor<uint64_t>(Framework::Scripting::Bind::Doc("Creates a wrapper for an existing connected player with this ID; it does not connect or spawn a player.").Param("id", "Network entity identifier."))
                .Method("toString", &Player::ToString, Framework::Scripting::Bind::Doc("Formats this player handle for logging and debugging.").Returns("Text containing the player's network entity ID."))
                .Method("isNametagVisible", &Player::IsNametagVisible, Framework::Scripting::Bind::Doc("Checks whether this player's nametag is shown to other players.").Returns("True unless the nametag was hidden; false also when this game has no nametags."))
                .Method("isNametagHealthVisible", &Player::IsNametagHealthVisible, Framework::Scripting::Bind::Doc("Checks whether the health bar under this player's nametag is shown.").Returns("True unless the health bar was hidden; false also when this game has no nametags."))
                .Method("getNametagText", &Player::GetNametagText, Framework::Scripting::Bind::Doc("Reads this player's nametag text override.").Returns("The override, or an empty string when the player's own name is drawn."))
                .Method("getNametagColor", &Player::GetNametagColor, Framework::Scripting::Bind::Doc("Reads this player's nametag color.").Returns("Packed 0xAARRGGBB color; opaque white when untinted."))
                .Method("kick", &Player::Kick, Framework::Scripting::Bind::Doc("Disconnects this player from the server.").OptionalParam("reason", "Optional reason shown to the disconnected player; omitting it uses the generic kicked reason."), Framework::Scripting::Bind::Side::Server)
                .Method("emit", &Player::Emit, Framework::Scripting::Bind::Doc("Emits a named script event to this player's client connection.").Param("eventName", "Client event name.").OptionalParam("payloadJson", "Optional JSON payload forwarded verbatim to the owning client."), Framework::Scripting::Bind::Side::Server)
                .Method("getIP", &Player::GetAddress, Framework::Scripting::Bind::Doc("Returns this player's current remote network address.").Returns("Address string, or an empty string when the player or peer is unavailable."), Framework::Scripting::Bind::Side::Server)
                .Method("setNametagVisible", &Player::SetNametagVisible,
                    Framework::Scripting::Bind::Doc("Shows or hides the name over this player's head for every other player. The health bar has its own switch, and each player can still hide all nametags locally.").Param("visible", "True to show this player's nametag to everyone, false to hide it."),
                    Framework::Scripting::Bind::Side::Server)
                .Method("setNametagHealthVisible", &Player::SetNametagHealthVisible, Framework::Scripting::Bind::Doc("Shows or hides the health bar under this player's nametag, leaving the name itself alone.").Param("visible", "True to show the health bar under this player's name, false to hide it."),
                    Framework::Scripting::Bind::Side::Server)
                .Method("setNametagText", &Player::SetNametagText, Framework::Scripting::Bind::Doc("Overrides the text drawn on this player's nametag.").OptionalParam("text", "Text to show instead of the player's name, cut to 64 bytes; empty or omitted restores the name."), Framework::Scripting::Bind::Side::Server)
                .Method("setNametagColor", &Player::SetNametagColor, Framework::Scripting::Bind::Doc("Tints the text on this player's nametag.").Param("color", "Packed 0xAARRGGBB color."), Framework::Scripting::Bind::Side::Server)
                .Property("steamId", &Player::GetSteamId, "Authenticated Steam identifier, or an empty string when unavailable.", Framework::Scripting::Bind::Side::Server)
                .Property("epicId", &Player::GetEpicId, "Epic Games account identifier the client reported, not verified by the server, or an empty string when unavailable.", Framework::Scripting::Bind::Side::Server)
                .Property("discordId", &Player::GetDiscordId, "Authenticated Discord identifier, or an empty string when unavailable.", Framework::Scripting::Bind::Side::Server)
                .Property("discordName", &Player::GetDiscordName, "Discord username the client reported, not verified by the server, or an empty string when unavailable.", Framework::Scripting::Bind::Side::Server)
                .Property("hardwareId", &Player::GetHardwareId, "Framework hardware identifier, or an empty string when unavailable.", Framework::Scripting::Bind::Side::Server)
                .Property("ping", &Player::GetPing, "Current round-trip latency in milliseconds, or -1 when unavailable.", Framework::Scripting::Bind::Side::Server)
                .Property("ip", &Player::GetAddress, "Current remote network address, or an empty string when unavailable.", Framework::Scripting::Bind::Side::Server)
                // Crosses between runtimes as its network id.
                .Transfer(&Player::GetId);
        }();
        return definition;
    }

    v8pp::class_<Player> &Player::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            // v8pp inherit<Entity> requires Entity registered first.
            Entity::GetClass(isolate);
            return Definition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), ScriptingSide(isolate));
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

    void Player::RegisterLua(LuaRuntime &runtime) {
        Entity::RegisterLua(runtime);
        Definition().RegisterLuaGlobal(runtime.State(), runtime.Options().side);
    }

    void Player::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.Unregister(isolate);
    }
} // namespace Framework::Scripting::Builtins
