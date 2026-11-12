/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice.h"

#include <core_modules.h>
#include <scripting/builtins/entity.h>
#include <scripting/lua/lua_runtime.h>
#include <scripting/scripting_catalog.h>
#include <voice/server/voice_server.h>

#include <cstdint>
#include <string>

namespace Framework::Integrations::Server::Scripting::Builtins {
    namespace Bind = Framework::Scripting::Bind;

    namespace {
        Framework::Voice::VoiceServer *Resolve() {
            return CoreModules::GetVoiceServer();
        }

        // The owning connection behind a Player argument. Throws and returns false for a
        // non-entity or an unowned one, rather than silently routing to peer 0.
        bool ResolvePlayer(Bind::Call &call, size_t index, const char *fn, uint64_t &out) {
            const auto *entity = call.Arg(index).AsBound<Framework::Scripting::Builtins::Entity>();
            if (!entity) {
                call.ThrowTypeError(std::string(fn) + ": expected a Player");
                return false;
            }

            auto *handle = entity->GetHandle();
            if (!handle || handle->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
                call.ThrowError(std::string(fn) + ": player has no owning connection");
                return false;
            }

            out = static_cast<uint64_t>(handle->ownerGUID);
            return true;
        }

        bool ReadNumber(Bind::Call &call, size_t index, const char *fn, const char *what, float &out) {
            if (!call.Arg(index).IsNumber()) {
                call.ThrowTypeError(std::string(fn) + ": expected a numeric " + what);
                return false;
            }

            out = static_cast<float>(call.Arg(index).AsNumber());
            return true;
        }

        bool ReadTier(Bind::Call &call, size_t index, const char *fn, Framework::Voice::VoiceTier &out) {
            const double value = call.Arg(index).IsNumber() ? call.Arg(index).AsNumber() : -1.0;
            if (value != 0.0 && value != 1.0 && value != 2.0) {
                call.ThrowRangeError(std::string(fn) + ": expected a tier, 0 (whisper), 1 (normal) or 2 (shout)");
                return false;
            }

            out = static_cast<Framework::Voice::VoiceTier>(static_cast<uint8_t>(value));
            return true;
        }

        // A flag, read as the language of the script reads a condition, which is how JavaScript always
        // read it here: undefined, null, false, 0, NaN and the empty string are false there, nil and
        // false in Lua.
        bool ReadFlag(const Bind::Call &call, size_t index) {
            const Bind::Value flag = call.Arg(index);
            if (flag.IsNil()) {
                return false;
            }
            if (flag.IsBoolean()) {
                return flag.AsBoolean();
            }
            if (call.GetLanguage() == Framework::Scripting::Language::Lua) {
                return true;
            }
            if (flag.IsNumber()) {
                const double number = flag.AsNumber();
                return number != 0.0 && number == number;
            }
            return !flag.IsString() || !flag.AsString().empty();
        }

        void SetRange(Bind::Call &call) {
            float range = 0.0f;
            if (!ReadNumber(call, 0, "Voice.setRange", "range", range)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->SetProximityRange(range);
            }
        }

        void GetRange(Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(static_cast<double>(voice != nullptr ? voice->GetProximityRange() : Framework::Voice::kDefaultProximityRange));
        }

        void SetPlayerRange(Bind::Call &call) {
            uint64_t player = 0;
            float range     = 0.0f;
            if (!ResolvePlayer(call, 0, "Voice.setPlayerRange", player) || !ReadNumber(call, 1, "Voice.setPlayerRange", "range", range)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->SetPlayerRange(player, range);
            }
        }

        void GetPlayerRange(Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.getPlayerRange", player)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(static_cast<double>(voice != nullptr ? voice->GetRouter().GetEffectivePlayerRange(player) : Framework::Voice::kDefaultProximityRange));
        }

        void SetTierRange(Bind::Call &call) {
            Framework::Voice::VoiceTier tier = Framework::Voice::VoiceTier::Normal;
            float range                      = 0.0f;
            if (!ReadTier(call, 0, "Voice.setTierRange", tier) || !ReadNumber(call, 1, "Voice.setTierRange", "range", range)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->SetTierRange(tier, range);
            }
        }

        void GetTierRange(Bind::Call &call) {
            Framework::Voice::VoiceTier tier = Framework::Voice::VoiceTier::Normal;
            if (!ReadTier(call, 0, "Voice.getTierRange", tier)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(static_cast<double>(voice != nullptr ? voice->GetTierRange(tier) : Framework::Voice::kDefaultProximityRange));
        }

        void SetPlayerTier(Bind::Call &call) {
            uint64_t player                  = 0;
            Framework::Voice::VoiceTier tier = Framework::Voice::VoiceTier::Normal;
            if (!ResolvePlayer(call, 0, "Voice.setPlayerTier", player) || !ReadTier(call, 1, "Voice.setPlayerTier", tier)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->SetPlayerTier(player, tier);
            }
        }

        void GetPlayerTier(Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.getPlayerTier", player)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(static_cast<uint32_t>(voice != nullptr ? voice->GetRouter().GetPlayerTier(player) : Framework::Voice::VoiceTier::Normal));
        }

        void SetPlayerMuted(Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.setPlayerMuted", player)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->GetRouter().SetPlayerMuted(player, ReadFlag(call, 1));
            }
        }

        void IsPlayerMuted(Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.isPlayerMuted", player)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(voice != nullptr && voice->GetRouter().IsPlayerMuted(player));
        }

        void SetPlayerDeaf(Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.setPlayerDeaf", player)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->GetRouter().SetPlayerDeaf(player, ReadFlag(call, 1));
            }
        }

        void IsPlayerDeaf(Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.isPlayerDeaf", player)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(voice != nullptr && voice->GetRouter().IsPlayerDeaf(player));
        }

        void SetLocalMute(Bind::Call &call) {
            uint64_t listener = 0;
            uint64_t target   = 0;
            if (!ResolvePlayer(call, 0, "Voice.setLocalMute", listener) || !ResolvePlayer(call, 1, "Voice.setLocalMute", target)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->GetRouter().SetLocalMute(listener, target, ReadFlag(call, 2));
            }
        }

        void IsLocallyMuted(Bind::Call &call) {
            uint64_t listener = 0;
            uint64_t target   = 0;
            if (!ResolvePlayer(call, 0, "Voice.isLocallyMuted", listener) || !ResolvePlayer(call, 1, "Voice.isLocallyMuted", target)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(voice != nullptr && voice->GetRouter().IsLocallyMuted(listener, target));
        }

        void IsPlayerVoiceEnabled(Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.isPlayerVoiceEnabled", player)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(voice != nullptr && !voice->GetRouter().IsPlayerVoiceDisabled(player));
        }

        void IsPlayerTalking(Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.isPlayerTalking", player)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(voice != nullptr && voice->IsPlayerTalking(player));
        }
    } // namespace

    const Bind::Namespace &Voice::Definition() {
        using Bind::Doc;
        using Bind::Side;
        using Bind::TypeOf;
        using Framework::Scripting::Builtins::Entity;

        static const auto definition = [] {
            const auto number    = TypeOf<double>();
            const auto boolean   = TypeOf<bool>();
            const auto player    = TypeOf<Entity>();
            const char *tierHelp = "Voice tier: 0 whisper, 1 normal, 2 shout.";
            const char *query    = "Player to query.";
            const char *radius   = "Radius in world units.";
            return Bind::Namespace("Voice", "Server-side proximity voice rules: how far voice carries, and who may talk to or hear whom.")
                .Function("setRange", &SetRange,
                    Doc("Sets how far voice carries for talkers with no override of their own. Connected clients are told, so their playback fades out at the same distance.").Param("range", number, "Audibility radius in world units; values <= 0 restore the default of 25."),
                    Side::Server)
                .Function("getRange", &GetRange, Doc("Reads the server-wide proximity range.").ReturnType(number).Returns(radius), Side::Server)
                .Function("setPlayerRange", &SetPlayerRange,
                    Doc("Overrides how far one player's voice carries, whichever voice tier they chose.")
                        .Param("player", player, "Player whose voice carries the given distance.")
                        .Param("range", number, "Audibility radius in world units; values <= 0 return them to the radius of their voice tier."),
                    Side::Server)
                .Function("getPlayerRange", &GetPlayerRange, Doc("Reads how far a player's voice carries, with the server-wide default already resolved.").Param("player", player, query).ReturnType(number).Returns(radius), Side::Server)
                .Function("setTierRange", &SetTierRange,
                    Doc("Sets how far a voice tier carries. Players switch tier themselves; whisper starts at 3, shout at 25, and normal carries the server-wide range. Set a tier to 0 to make it carry the server-wide range, which takes it away. Connected clients are told.")
                        .Param("tier", number, tierHelp)
                        .Param("range", number, "Audibility radius in world units; values <= 0 make the tier carry the server-wide range."),
                    Side::Server)
                .Function("getTierRange", &GetTierRange, Doc("Reads how far a voice tier carries, with the server-wide range already resolved.").Param("tier", number, tierHelp).ReturnType(number).Returns(radius), Side::Server)
                .Function("setPlayerTier", &SetPlayerTier,
                    Doc("Puts a player on a voice tier, as if they had switched to it. Their own indicator follows, and the playerVoiceTierChange event is not raised.").Param("player", player, "Player to move.").Param("tier", number, tierHelp), Side::Server)
                .Function("getPlayerTier", &GetPlayerTier,
                    Doc("Reads the voice tier a player is on. A player switching tier raises the playerVoiceTierChange event with the player and the new tier.").Param("player", player, query).ReturnType(number).Returns("0 whisper, 1 normal, 2 shout."), Side::Server)
                .Function("setPlayerMuted", &SetPlayerMuted, Doc("Server-wide mute: a muted player's voice reaches nobody.").Param("player", player, "Player to mute or unmute.").Param("muted", boolean, "True to stop their voice reaching anyone."), Side::Server)
                .Function("isPlayerMuted", &IsPlayerMuted, Doc("Checks the server-wide mute flag.").Param("player", player, query).ReturnType(boolean).Returns("True when the player's voice reaches nobody."), Side::Server)
                .Function("setPlayerDeaf", &SetPlayerDeaf, Doc("Server-wide deafen: a deaf player receives nobody.").Param("player", player, "Player to deafen or undeafen.").Param("deaf", boolean, "True to stop them receiving anyone's voice."), Side::Server)
                .Function("isPlayerDeaf", &IsPlayerDeaf, Doc("Checks the server-wide deafen flag.").Param("player", player, query).ReturnType(boolean).Returns("True when the player receives nobody's voice."), Side::Server)
                .Function("setLocalMute", &SetLocalMute,
                    Doc("One-way mute between two players, enforced by the server rather than the client.")
                        .Param("listener", player, "Player who stops hearing the target.")
                        .Param("target", player, "Player the listener stops hearing.")
                        .Param("muted", boolean, "True to mute, false to restore."),
                    Side::Server)
                .Function("isLocallyMuted", &IsLocallyMuted, Doc("Checks a one-way mute.").Param("listener", player, "Player doing the muting.").Param("target", player, "Player being muted.").ReturnType(boolean).Returns("True when the listener does not receive the target."),
                    Side::Server)
                .Function("isPlayerVoiceEnabled", &IsPlayerVoiceEnabled,
                    Doc("Checks whether a player left voice chat enabled in their own client settings. A preference, not a permission: use setPlayerMuted or setPlayerDeaf to enforce anything.")
                        .Param("player", player, query)
                        .ReturnType(boolean)
                        .Returns("True unless the player turned voice chat off."),
                    Side::Server)
                .Function("isPlayerTalking", &IsPlayerTalking,
                    Doc("Checks whether a player is speaking right now. Tracks speech rather than the push-to-talk key: silence is dropped before it reaches the server, and the state clears shortly after the last frame. The same signal raises the playerVoiceStart and playerVoiceStop events.")
                        .Param("player", player, query)
                        .ReturnType(boolean)
                        .Returns("True while the player's voice is reaching the server."),
                    Side::Server);
        }();
        return definition;
    }

    void Voice::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (!isolate || global.IsEmpty()) {
            return;
        }
        Definition().MaterialiseV8(isolate, isolate->GetCurrentContext(), global, Framework::Scripting::GetScriptingCatalog(isolate), Bind::Side::Server);

        auto &events = Framework::Scripting::GetScriptingCatalog(isolate).data_type("EventMap");
        events.add_property("playerVoiceStart", "[player: Player]", "Dispatched when a player starts speaking, as isPlayerTalking turns true.");
        events.add_property("playerVoiceStop", "[player: Player]", "Dispatched shortly after a player's last voice frame, as isPlayerTalking turns false. A player who disconnects while talking gets no stop, so clear them on disconnect too.");
        events.add_property("playerVoiceTierChange", "[player: Player, tier: number]", "Dispatched when a player switches voice tier themselves: 0 whisper, 1 normal, 2 shout. setPlayerTier does not raise it.");
    }

    void Voice::RegisterLua(Framework::Scripting::LuaRuntime &runtime) {
        Definition().RegisterLuaGlobal(runtime.State(), runtime.Options().side);
    }

} // namespace Framework::Integrations::Server::Scripting::Builtins
