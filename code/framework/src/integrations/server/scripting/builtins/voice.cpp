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
#include <scripting/builtins/script_args.h>
#include <scripting/scripting_catalog.h>
#include <voice/server/voice_server.h>

#include <cstdint>
#include <string>

namespace Framework::Integrations::Server::Scripting::Builtins {
    namespace {
        Framework::Voice::VoiceServer *Resolve() {
            return CoreModules::GetVoiceServer();
        }

        // The owning connection behind a Player argument. Throws and returns false for a
        // non-entity or an unowned one, rather than silently routing to peer 0.
        bool ResolvePlayer(Framework::Scripting::Bind::Call &call, size_t index, const char *fn, uint64_t &out) {
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

        bool ReadNumber(Framework::Scripting::Bind::Call &call, size_t index, const char *fn, const char *what, float &out) {
            if (!call.Arg(index).IsNumber()) {
                call.ThrowTypeError(std::string(fn) + ": expected a numeric " + what);
                return false;
            }

            out = static_cast<float>(call.Arg(index).AsNumber());
            return true;
        }

        bool ReadTier(Framework::Scripting::Bind::Call &call, size_t index, const char *fn, Framework::Voice::VoiceTier &out) {
            const double value = call.Arg(index).IsNumber() ? call.Arg(index).AsNumber() : -1.0;
            if (value != 0.0 && value != 1.0 && value != 2.0) {
                call.ThrowRangeError(std::string(fn) + ": expected a tier, 0 (whisper), 1 (normal) or 2 (shout)");
                return false;
            }

            out = static_cast<Framework::Voice::VoiceTier>(static_cast<uint8_t>(value));
            return true;
        }

        void SetRange(Framework::Scripting::Bind::Call &call) {
            float range = 0.0f;
            if (!ReadNumber(call, 0, "Voice.setRange", "range", range)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->SetProximityRange(range);
            }
        }

        void GetRange(Framework::Scripting::Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(static_cast<double>(voice != nullptr ? voice->GetProximityRange() : Framework::Voice::kDefaultProximityRange));
        }

        void SetPlayerRange(Framework::Scripting::Bind::Call &call) {
            uint64_t player = 0;
            float range     = 0.0f;
            if (!ResolvePlayer(call, 0, "Voice.setPlayerRange", player) || !ReadNumber(call, 1, "Voice.setPlayerRange", "range", range)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->SetPlayerRange(player, range);
            }
        }

        void GetPlayerRange(Framework::Scripting::Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.getPlayerRange", player)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(static_cast<double>(voice != nullptr ? voice->GetRouter().GetEffectivePlayerRange(player) : Framework::Voice::kDefaultProximityRange));
        }

        void SetTierRange(Framework::Scripting::Bind::Call &call) {
            Framework::Voice::VoiceTier tier = Framework::Voice::VoiceTier::Normal;
            float range                      = 0.0f;
            if (!ReadTier(call, 0, "Voice.setTierRange", tier) || !ReadNumber(call, 1, "Voice.setTierRange", "range", range)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->SetTierRange(tier, range);
            }
        }

        void GetTierRange(Framework::Scripting::Bind::Call &call) {
            Framework::Voice::VoiceTier tier = Framework::Voice::VoiceTier::Normal;
            if (!ReadTier(call, 0, "Voice.getTierRange", tier)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(static_cast<double>(voice != nullptr ? voice->GetTierRange(tier) : Framework::Voice::kDefaultProximityRange));
        }

        void SetPlayerTier(Framework::Scripting::Bind::Call &call) {
            uint64_t player                  = 0;
            Framework::Voice::VoiceTier tier = Framework::Voice::VoiceTier::Normal;
            if (!ResolvePlayer(call, 0, "Voice.setPlayerTier", player) || !ReadTier(call, 1, "Voice.setPlayerTier", tier)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->SetPlayerTier(player, tier);
            }
        }

        void GetPlayerTier(Framework::Scripting::Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.getPlayerTier", player)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(static_cast<uint32_t>(voice != nullptr ? voice->GetRouter().GetPlayerTier(player) : Framework::Voice::VoiceTier::Normal));
        }

        void SetPlayerMuted(Framework::Scripting::Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.setPlayerMuted", player)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->GetRouter().SetPlayerMuted(player, Framework::Scripting::Builtins::ReadFlag(call, 1));
            }
        }

        void IsPlayerMuted(Framework::Scripting::Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.isPlayerMuted", player)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(voice != nullptr && voice->GetRouter().IsPlayerMuted(player));
        }

        void SetPlayerDeaf(Framework::Scripting::Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.setPlayerDeaf", player)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->GetRouter().SetPlayerDeaf(player, Framework::Scripting::Builtins::ReadFlag(call, 1));
            }
        }

        void IsPlayerDeaf(Framework::Scripting::Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.isPlayerDeaf", player)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(voice != nullptr && voice->GetRouter().IsPlayerDeaf(player));
        }

        void SetLocalMute(Framework::Scripting::Bind::Call &call) {
            uint64_t listener = 0;
            uint64_t target   = 0;
            if (!ResolvePlayer(call, 0, "Voice.setLocalMute", listener) || !ResolvePlayer(call, 1, "Voice.setLocalMute", target)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->GetRouter().SetLocalMute(listener, target, Framework::Scripting::Builtins::ReadFlag(call, 2));
            }
        }

        void IsLocallyMuted(Framework::Scripting::Bind::Call &call) {
            uint64_t listener = 0;
            uint64_t target   = 0;
            if (!ResolvePlayer(call, 0, "Voice.isLocallyMuted", listener) || !ResolvePlayer(call, 1, "Voice.isLocallyMuted", target)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(voice != nullptr && voice->GetRouter().IsLocallyMuted(listener, target));
        }

        void IsPlayerVoiceEnabled(Framework::Scripting::Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.isPlayerVoiceEnabled", player)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(voice != nullptr && !voice->GetRouter().IsPlayerVoiceDisabled(player));
        }

        void IsPlayerTalking(Framework::Scripting::Bind::Call &call) {
            uint64_t player = 0;
            if (!ResolvePlayer(call, 0, "Voice.isPlayerTalking", player)) {
                return;
            }

            auto *voice = Resolve();
            call.Return(voice != nullptr && voice->IsPlayerTalking(player));
        }
    } // namespace

    const Framework::Scripting::Bind::Namespace &Voice::Definition() {
        static const auto definition = [] {
            const auto number    = Framework::Scripting::Bind::TypeOf<double>();
            const auto boolean   = Framework::Scripting::Bind::TypeOf<bool>();
            const auto player    = Framework::Scripting::Bind::TypeOf<Framework::Scripting::Builtins::Entity>();
            const char *tierHelp = "Voice tier: 0 whisper, 1 normal, 2 shout.";
            const char *query    = "Player to query.";
            const char *radius   = "Radius in world units.";
            return Framework::Scripting::Bind::Namespace("Voice", "Server-side proximity voice rules: how far voice carries, and who may talk to or hear whom.", Framework::Scripting::Bind::Side::Server)
                .Function("setRange", &SetRange,
                    Framework::Scripting::Bind::Doc("Sets how far voice carries for talkers with no override of their own. Connected clients are told, so their playback fades out at the same distance.").Param("range", number, "Audibility radius in world units; values <= 0 restore the default of 25."),
                    Framework::Scripting::Bind::Side::Server)
                .Function("getRange", &GetRange, Framework::Scripting::Bind::Doc("Reads the server-wide proximity range.").ReturnType(number).Returns(radius), Framework::Scripting::Bind::Side::Server)
                .Function("setPlayerRange", &SetPlayerRange,
                    Framework::Scripting::Bind::Doc("Overrides how far one player's voice carries, whichever voice tier they chose.")
                        .Param("player", player, "Player whose voice carries the given distance.")
                        .Param("range", number, "Audibility radius in world units; values <= 0 return them to the radius of their voice tier."),
                    Framework::Scripting::Bind::Side::Server)
                .Function("getPlayerRange", &GetPlayerRange, Framework::Scripting::Bind::Doc("Reads how far a player's voice carries, with the server-wide default already resolved.").Param("player", player, query).ReturnType(number).Returns(radius), Framework::Scripting::Bind::Side::Server)
                .Function("setTierRange", &SetTierRange,
                    Framework::Scripting::Bind::Doc("Sets how far a voice tier carries. Players switch tier themselves; whisper starts at 3, shout at 25, and normal carries the server-wide range. Set a tier to 0 to make it carry the server-wide range, which takes it away. Connected clients are told.")
                        .Param("tier", number, tierHelp)
                        .Param("range", number, "Audibility radius in world units; values <= 0 make the tier carry the server-wide range."),
                    Framework::Scripting::Bind::Side::Server)
                .Function("getTierRange", &GetTierRange, Framework::Scripting::Bind::Doc("Reads how far a voice tier carries, with the server-wide range already resolved.").Param("tier", number, tierHelp).ReturnType(number).Returns(radius), Framework::Scripting::Bind::Side::Server)
                .Function("setPlayerTier", &SetPlayerTier,
                    Framework::Scripting::Bind::Doc("Puts a player on a voice tier, as if they had switched to it. Their own indicator follows, and the playerVoiceTierChange event is not raised.").Param("player", player, "Player to move.").Param("tier", number, tierHelp), Framework::Scripting::Bind::Side::Server)
                .Function("getPlayerTier", &GetPlayerTier,
                    Framework::Scripting::Bind::Doc("Reads the voice tier a player is on. A player switching tier raises the playerVoiceTierChange event with the player and the new tier.").Param("player", player, query).ReturnType(number).Returns("0 whisper, 1 normal, 2 shout."), Framework::Scripting::Bind::Side::Server)
                .Function("setPlayerMuted", &SetPlayerMuted, Framework::Scripting::Bind::Doc("Server-wide mute: a muted player's voice reaches nobody.").Param("player", player, "Player to mute or unmute.").Param("muted", boolean, "True to stop their voice reaching anyone."), Framework::Scripting::Bind::Side::Server)
                .Function("isPlayerMuted", &IsPlayerMuted, Framework::Scripting::Bind::Doc("Checks the server-wide mute flag.").Param("player", player, query).ReturnType(boolean).Returns("True when the player's voice reaches nobody."), Framework::Scripting::Bind::Side::Server)
                .Function("setPlayerDeaf", &SetPlayerDeaf, Framework::Scripting::Bind::Doc("Server-wide deafen: a deaf player receives nobody.").Param("player", player, "Player to deafen or undeafen.").Param("deaf", boolean, "True to stop them receiving anyone's voice."), Framework::Scripting::Bind::Side::Server)
                .Function("isPlayerDeaf", &IsPlayerDeaf, Framework::Scripting::Bind::Doc("Checks the server-wide deafen flag.").Param("player", player, query).ReturnType(boolean).Returns("True when the player receives nobody's voice."), Framework::Scripting::Bind::Side::Server)
                .Function("setLocalMute", &SetLocalMute,
                    Framework::Scripting::Bind::Doc("One-way mute between two players, enforced by the server rather than the client.")
                        .Param("listener", player, "Player who stops hearing the target.")
                        .Param("target", player, "Player the listener stops hearing.")
                        .Param("muted", boolean, "True to mute, false to restore."),
                    Framework::Scripting::Bind::Side::Server)
                .Function("isLocallyMuted", &IsLocallyMuted, Framework::Scripting::Bind::Doc("Checks a one-way mute.").Param("listener", player, "Player doing the muting.").Param("target", player, "Player being muted.").ReturnType(boolean).Returns("True when the listener does not receive the target."),
                    Framework::Scripting::Bind::Side::Server)
                .Function("isPlayerVoiceEnabled", &IsPlayerVoiceEnabled,
                    Framework::Scripting::Bind::Doc("Checks whether a player left voice chat enabled in their own client settings. A preference, not a permission: use setPlayerMuted or setPlayerDeaf to enforce anything.")
                        .Param("player", player, query)
                        .ReturnType(boolean)
                        .Returns("True unless the player turned voice chat off."),
                    Framework::Scripting::Bind::Side::Server)
                .Function("isPlayerTalking", &IsPlayerTalking,
                    Framework::Scripting::Bind::Doc("Checks whether a player is speaking right now. Tracks speech rather than the push-to-talk key: silence is dropped before it reaches the server, and the state clears shortly after the last frame. The same signal raises the playerVoiceStart and playerVoiceStop events.")
                        .Param("player", player, query)
                        .ReturnType(boolean)
                        .Returns("True while the player's voice is reaching the server."),
                    Framework::Scripting::Bind::Side::Server);
        }();
        return definition;
    }

    void Voice::Register(Framework::Scripting::Bind::Registrar &registrar, Framework::Scripting::Bind::Languages languages) {
        registrar.Global(Definition(), languages);
        if (!registrar.HasJavaScript(languages)) {
            return;
        }

        v8::Isolate *isolate = registrar.Isolate();
        auto &events = Framework::Scripting::GetScriptingCatalog(isolate).data_type("EventMap");
        events.add_property("playerVoiceStart", "[player: Player]", "Dispatched when a player starts speaking, as isPlayerTalking turns true.");
        events.add_property("playerVoiceStop", "[player: Player]", "Dispatched shortly after a player's last voice frame, as isPlayerTalking turns false. A player who disconnects while talking gets no stop, so clear them on disconnect too.");
        events.add_property("playerVoiceTierChange", "[player: Player, tier: number]", "Dispatched when a player switches voice tier themselves: 0 whisper, 1 normal, 2 shout. setPlayerTier does not raise it.");
    }

} // namespace Framework::Integrations::Server::Scripting::Builtins
