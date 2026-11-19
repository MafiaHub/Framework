/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "player.h"
#include "../engine_helpers.h"
#include "../scripting_catalog.h"

#include <core_modules.h>
#include <integrations/shared/rpc/emit_script_event.h>
#include <networking/account_key.h>
#include <networking/network_peer.h>
#include <networking/replication/nametag_state.h>
#include <networking/rpc/client_identity.h>
#include <utils/string_utils.h>

#include <algorithm>
#include <sstream>
#include <utility>

namespace Framework::Scripting::Builtins {
    void Player::Kick(const std::string &reason) {
        MafiaNet::PeerGuid owner = MafiaNet::UNASSIGNED_PEER_GUID;
        if (!ResolveOwner(owner)) {
            return;
        }
        auto *peer = CoreModules::GetNetworkPeer();
        if (!peer) {
            return;
        }
        peer->KickPlayer(MafiaNet::ToGuid(owner), reason.empty() ? Networking::DisconnectionReason::KICKED : Networking::DisconnectionReason::KICKED_CUSTOM, reason);
    }

    void Player::Emit(const std::string &eventName, const std::string &payloadJson) {
        if (eventName.empty()) {
            return;
        }
        MafiaNet::PeerGuid owner = MafiaNet::UNASSIGNED_PEER_GUID;
        if (!ResolveOwner(owner)) {
            return;
        }
        auto *peer = CoreModules::GetNetworkPeer();
        if (!peer) {
            return;
        }
        Framework::Integrations::Shared::RPC::EmitScriptEvent ev;
        ev.FromParameters(eventName, payloadJson);
        peer->SendRPC(ev, MafiaNet::ToGuid(owner));
    }

    std::string Player::GetAccountId() const {
        const auto identity = ResolveIdentity();
        return identity ? Networking::AccountIdOf(identity->publicKey) : "";
    }

    v8::Local<v8::Object> Player::NewUnverifiedIdentity(v8::Isolate *isolate, v8::Local<v8::Context> context, const Networking::RPC::ClientIdentity *identity) {
        v8::EscapableHandleScope scope(isolate);
        v8::Local<v8::Object> claims = v8::Object::New(isolate);
        const std::string empty;
        const std::pair<const char *, const std::string *> fields[] = {
            {"steamId", identity ? &identity->steamId : &empty},
            {"epicId", identity ? &identity->epicId : &empty},
            {"discordId", identity ? &identity->discordId : &empty},
            {"discordName", identity ? &identity->discordName : &empty},
            {"hardwareId", identity ? &identity->hardwareId : &empty},
        };
        for (const auto &[name, value] : fields) {
            claims->CreateDataProperty(context, v8pp::to_v8(isolate, name), v8pp::to_v8(isolate, *value)).Check();
        }
        claims->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).Check();
        return scope.Escape(claims);
    }

    void Player::DocumentUnverifiedIdentity(v8::Isolate *isolate) {
        auto &type = GetScriptingCatalog(isolate).data_type("UnverifiedIdentity", "What a player's client says about itself. The client writes every field and anyone can put anything here: Discord ids are public, and a modified client sends whichever it likes. "
                                                                                  "Show them, log them, or link them to an account the player proved some other way. Never key ownership, bans or permissions on them; that is `accountId`.");
        if (std::ranges::any_of(type.properties, [](const auto &property) {
                return property.name == "steamId";
            })) {
            return;
        }
        type.add_property("steamId", "string", "Steam identifier the client reported, or an empty string.", true);
        type.add_property("epicId", "string", "Epic Games account identifier the client reported, or an empty string.", true);
        type.add_property("discordId", "string", "Discord user identifier the client reported, or an empty string.", true);
        type.add_property("discordName", "string", "Discord username the client reported, or an empty string.", true);
        type.add_property("hardwareId", "string", "Framework hardware identifier the client reported, or an empty string.", true);
    }

    v8::Local<v8::FunctionTemplate> Player::MovedIdentityGetter(v8::Isolate *isolate, const char *field) {
        return v8::FunctionTemplate::New(
            isolate,
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                v8::Isolate *isolate      = info.GetIsolate();
                const std::string field   = v8pp::from_v8<std::string>(isolate, info.Data());
                const std::string message = field + " is not proof of who a player is: any client can report any value. Key ownership and permissions on accountId, and read the reported value as unverified." + field + ".";
                ThrowError(isolate, message.c_str());
            },
            v8pp::to_v8(isolate, field));
    }

    std::string Player::GetSteamId() const {
        const auto identity = ResolveIdentity();
        return identity ? identity->steamId : "";
    }

    std::string Player::GetEpicId() const {
        const auto identity = ResolveIdentity();
        return identity ? identity->epicId : "";
    }

    std::string Player::GetDiscordId() const {
        const auto identity = ResolveIdentity();
        return identity ? identity->discordId : "";
    }

    std::string Player::GetDiscordName() const {
        const auto identity = ResolveIdentity();
        return identity ? identity->discordName : "";
    }

    std::string Player::GetHardwareId() const {
        const auto identity = ResolveIdentity();
        return identity ? identity->hardwareId : "";
    }

    int Player::GetPing() const {
        MafiaNet::PeerGuid owner = MafiaNet::UNASSIGNED_PEER_GUID;
        if (!ResolveOwner(owner)) {
            return -1;
        }
        auto *peer = CoreModules::GetNetworkPeer();
        return peer ? peer->GetPing(MafiaNet::ToGuid(owner)) : -1;
    }

    std::string Player::GetAddress() const {
        MafiaNet::PeerGuid owner = MafiaNet::UNASSIGNED_PEER_GUID;
        if (!ResolveOwner(owner)) {
            return "";
        }
        auto *peer = CoreModules::GetNetworkPeer();
        return peer ? peer->GetAddress(MafiaNet::ToGuid(owner)) : "";
    }

    Player::HeldNametag Player::ResolveNametag() const {
        HeldNametag held;
        held.entity = Resolve();
        held.state  = held.entity ? held.entity->GetNametag() : nullptr;
        return held;
    }

    Player::HeldNametag Player::WritableNametag() const {
        // Server-only, like Kick: the state is a ServerField, so a client write would never leave it.
        return CoreModules::GetClientInstance() ? HeldNametag() : ResolveNametag();
    }

    void Player::SetNametagComponent(Networking::Replication::NametagComponent component, bool enabled) {
        if (const auto state = WritableNametag()) {
            state->Set(component, enabled);
        }
    }

    bool Player::HasNametagComponent(Networking::Replication::NametagComponent component) const {
        const auto state = ResolveNametag();
        return state && state->Has(component);
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
        if (const auto state = WritableNametag()) {
            state->text = text;
            Utils::StringUtils::TruncateUtf8(state->text, Networking::Replication::NametagState::kMaxTextBytes);
        }
    }

    std::string Player::GetNametagText() const {
        const auto state = ResolveNametag();
        return state ? state->text : "";
    }

    void Player::SetNametagColor(uint32_t color) {
        if (const auto state = WritableNametag()) {
            state->color = color;
        }
    }

    uint32_t Player::GetNametagColor() const {
        const auto state = ResolveNametag();
        return state ? state->color : 0xFFFFFFFF;
    }

    std::string Player::ToString() const {
        std::ostringstream ss;
        ss << "Player{ id: " << _id << " }";
        return ss.str();
    }

    v8pp::class_<Player> &Player::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            // v8pp inherit<Entity> requires Entity registered first.
            Entity::GetClass(isolate);

            auto cls = std::make_unique<v8pp::class_<Player>>(isolate, GetScriptingCatalog(isolate), "Player", "Framework-owned base handle for a connected player, extended by each game or mod.");
            cls->auto_wrap_objects(true);
            cls->inherit<Entity>()
                .ctor<uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a wrapper for an existing connected player with this ID; it does not connect or spawn a player."),
                    &Entity::ConstructFromScript<Player>)
                .function("toString", &Player::ToString, v8pp::metadata::docs("string", {}, "Formats this player handle for logging and debugging.", "Text containing the player's network entity ID."))
                .function("isNametagVisible", &Player::IsNametagVisible, v8pp::metadata::docs("boolean", {}, "Checks whether this player's nametag is shown to other players.", "True unless the nametag was hidden; false also when this game has no nametags."))
                .function("isNametagHealthVisible", &Player::IsNametagHealthVisible, v8pp::metadata::docs("boolean", {}, "Checks whether the health bar under this player's nametag is shown.", "True unless the health bar was hidden; false also when this game has no nametags."))
                .function("getNametagText", &Player::GetNametagText, v8pp::metadata::docs("string", {}, "Reads this player's nametag text override.", "The override, or an empty string when the player's own name is drawn."))
                .function("getNametagColor", &Player::GetNametagColor, v8pp::metadata::docs("number", {}, "Reads this player's nametag color.", "Packed 0xAARRGGBB color; opaque white when untinted."));

            // Connection-level surface; NetworkPeer implements it only in NetworkServer. The getters above
            // stay on both sides, they read the replica.
            if (!IsClientScripting(isolate)) {
                cls->function("kick", &Player::Kick, v8pp::metadata::docs("void", {v8pp::metadata::param("reason", "string", true, "Optional reason shown to the disconnected player; omitting it uses the generic kicked reason.")}, "Disconnects this player from the server."));
                cls->function("emit", &Player::Emit,
                    v8pp::metadata::docs("void", {v8pp::metadata::param("eventName", "string", false, "Client event name."), v8pp::metadata::param("payloadJson", "string", true, "Optional JSON payload forwarded verbatim to the owning client.")},
                        "Emits a named script event to this player's client connection."));
                cls->function("getIP", &Player::GetAddress, v8pp::metadata::docs("string", {}, "Returns this player's current remote network address.", "Address string, or an empty string when the player or peer is unavailable."));
                cls->function("setNametagVisible", &Player::SetNametagVisible,
                    v8pp::metadata::docs("void", {v8pp::metadata::param("visible", "boolean", false, "True to show this player's nametag to everyone, false to hide it.")},
                        "Shows or hides the name over this player's head for every other player. The health bar has its own switch, and each player can still hide all nametags locally."));
                cls->function("setNametagHealthVisible", &Player::SetNametagHealthVisible,
                    v8pp::metadata::docs("void", {v8pp::metadata::param("visible", "boolean", false, "True to show the health bar under this player's name, false to hide it.")}, "Shows or hides the health bar under this player's nametag, leaving the name itself alone."));
                cls->function("setNametagText", &Player::SetNametagText,
                    v8pp::metadata::docs("void", {v8pp::metadata::param("text", "string", true, "Text to show instead of the player's name, cut to 64 bytes; empty or omitted restores the name.")}, "Overrides the text drawn on this player's nametag."));
                cls->function("setNametagColor", &Player::SetNametagColor, v8pp::metadata::docs("void", {v8pp::metadata::param("color", "number", false, "Packed 0xAARRGGBB color.")}, "Tints the text on this player's nametag."));
                cls->property("accountId", &Player::GetAccountId,
                    v8pp::metadata::property_docs("string", "The account this player proved when they connected: 32 hex characters, the same on every server and in every session, that no other client can claim. Key saved data, ownership, bans and permissions on it. "
                                                            "It stands for a key the player's client keeps on their computer, so a player who loses that file starts a new account. Empty once the player has left."));
                DocumentUnverifiedIdentity(isolate);
                {
                    auto unverifiedGetter = v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                        v8::Isolate *isolate = info.GetIsolate();
                        auto *self           = v8pp::class_<Player>::unwrap_object(isolate, info.This());
                        if (!self) {
                            return;
                        }
                        const auto identity = self->ResolveIdentity();
                        info.GetReturnValue().Set(NewUnverifiedIdentity(isolate, isolate->GetCurrentContext(), identity ? &*identity : nullptr));
                    });
                    cls->accessor_property("unverified", unverifiedGetter, v8::Local<v8::FunctionTemplate>(),
                        v8pp::metadata::property_docs("UnverifiedIdentity", "The Steam, Epic and Discord ids, Discord name and hardware id this player's client reported. Not proof of anything: see UnverifiedIdentity, and recognise a player by accountId."));
                }
                // Left out of the catalog: they exist only to fail loudly in a script written against the old names.
                for (const char *field : kMovedIdentityFields) {
                    cls->class_function_template()->PrototypeTemplate()->SetAccessorProperty(v8pp::to_v8(isolate, field).As<v8::Name>(), MovedIdentityGetter(isolate, field), v8::Local<v8::FunctionTemplate>(), v8::DontEnum);
                }
                cls->property("ping", &Player::GetPing, v8pp::metadata::property_docs("number", "Current round-trip latency in milliseconds, or -1 when unavailable."));
                cls->property("ip", &Player::GetAddress, v8pp::metadata::property_docs("string", "Current remote network address, or an empty string when unavailable."));
            }

            return cls;
        });
    }

    std::optional<Networking::RPC::ClientIdentity> Player::ResolveIdentity() const {
        MafiaNet::PeerGuid owner = MafiaNet::UNASSIGNED_PEER_GUID;
        if (!ResolveOwner(owner)) {
            return std::nullopt;
        }
        auto *peer = CoreModules::GetNetworkPeer();
        return peer ? peer->GetPeerIdentity(MafiaNet::ToGuid(owner)) : std::nullopt;
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
