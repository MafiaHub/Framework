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
#include <scripting/scripting_catalog.h>
#include <voice/server/voice_server.h>

#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <cstdint>
#include <string>

namespace Framework::Integrations::Server::Scripting::Builtins {
    namespace {
        Framework::Voice::VoiceServer *Resolve() {
            return CoreModules::GetVoiceServer();
        }

        // The owning connection behind a Player argument. Throws and returns false for a
        // non-entity or an unowned one, rather than silently routing to peer 0.
        bool ResolvePlayer(const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *fn, uint64_t &out) {
            v8::Isolate *isolate = info.GetIsolate();

            auto *entity = info.Length() > index ? v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(isolate, info[index]) : nullptr;
            if (!entity) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, std::string(fn) + ": expected a Player")));
                return false;
            }

            auto *handle = entity->GetHandle();
            if (!handle || handle->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
                isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, std::string(fn) + ": player has no owning connection")));
                return false;
            }

            out = static_cast<uint64_t>(handle->ownerGUID);
            return true;
        }

        bool ReadNumber(const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *fn, const char *what, float &out) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() <= index || !info[index]->IsNumber()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, std::string(fn) + ": expected a numeric " + what)));
                return false;
            }

            out = static_cast<float>(info[index]->NumberValue(isolate->GetCurrentContext()).FromMaybe(0.0));
            return true;
        }
    } // namespace

    void Voice::JS_SetRange(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        float range = 0.0f;
        if (!ReadNumber(info, 0, "Voice.setRange", "range", range)) {
            return;
        }
        if (auto *voice = Resolve()) {
            voice->SetProximityRange(range);
        }
    }

    void Voice::JS_GetRange(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *voice = Resolve();
        info.GetReturnValue().Set(voice != nullptr ? voice->GetProximityRange() : Framework::Voice::kDefaultProximityRange);
    }

    void Voice::JS_SetPlayerRange(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        uint64_t player = 0;
        float range     = 0.0f;
        if (!ResolvePlayer(info, 0, "Voice.setPlayerRange", player) || !ReadNumber(info, 1, "Voice.setPlayerRange", "range", range)) {
            return;
        }
        if (auto *voice = Resolve()) {
            voice->SetPlayerRange(player, range);
        }
    }

    void Voice::JS_GetPlayerRange(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        uint64_t player = 0;
        if (!ResolvePlayer(info, 0, "Voice.getPlayerRange", player)) {
            return;
        }

        auto *voice = Resolve();
        info.GetReturnValue().Set(voice != nullptr ? voice->GetRouter().GetEffectivePlayerRange(player) : Framework::Voice::kDefaultProximityRange);
    }

    void Voice::JS_SetPlayerMuted(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        uint64_t player = 0;
        if (!ResolvePlayer(info, 0, "Voice.setPlayerMuted", player)) {
            return;
        }
        if (auto *voice = Resolve()) {
            voice->GetRouter().SetPlayerMuted(player, info.Length() > 1 && info[1]->BooleanValue(isolate));
        }
    }

    void Voice::JS_IsPlayerMuted(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        uint64_t player = 0;
        if (!ResolvePlayer(info, 0, "Voice.isPlayerMuted", player)) {
            return;
        }

        auto *voice = Resolve();
        info.GetReturnValue().Set(voice != nullptr && voice->GetRouter().IsPlayerMuted(player));
    }

    void Voice::JS_SetPlayerDeaf(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        uint64_t player = 0;
        if (!ResolvePlayer(info, 0, "Voice.setPlayerDeaf", player)) {
            return;
        }
        if (auto *voice = Resolve()) {
            voice->GetRouter().SetPlayerDeaf(player, info.Length() > 1 && info[1]->BooleanValue(isolate));
        }
    }

    void Voice::JS_IsPlayerDeaf(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        uint64_t player = 0;
        if (!ResolvePlayer(info, 0, "Voice.isPlayerDeaf", player)) {
            return;
        }

        auto *voice = Resolve();
        info.GetReturnValue().Set(voice != nullptr && voice->GetRouter().IsPlayerDeaf(player));
    }

    void Voice::JS_SetLocalMute(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        uint64_t listener = 0;
        uint64_t target   = 0;
        if (!ResolvePlayer(info, 0, "Voice.setLocalMute", listener) || !ResolvePlayer(info, 1, "Voice.setLocalMute", target)) {
            return;
        }
        if (auto *voice = Resolve()) {
            voice->GetRouter().SetLocalMute(listener, target, info.Length() > 2 && info[2]->BooleanValue(isolate));
        }
    }

    void Voice::JS_IsLocallyMuted(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        uint64_t listener = 0;
        uint64_t target   = 0;
        if (!ResolvePlayer(info, 0, "Voice.isLocallyMuted", listener) || !ResolvePlayer(info, 1, "Voice.isLocallyMuted", target)) {
            return;
        }

        auto *voice = Resolve();
        info.GetReturnValue().Set(voice != nullptr && voice->GetRouter().IsLocallyMuted(listener, target));
    }

    void Voice::JS_IsPlayerVoiceEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        uint64_t player = 0;
        if (!ResolvePlayer(info, 0, "Voice.isPlayerVoiceEnabled", player)) {
            return;
        }

        auto *voice = Resolve();
        info.GetReturnValue().Set(voice != nullptr && !voice->GetRouter().IsPlayerVoiceDisabled(player));
    }

    void Voice::JS_IsPlayerTalking(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        uint64_t player = 0;
        if (!ResolvePlayer(info, 0, "Voice.isPlayerTalking", player)) {
            return;
        }

        auto *voice = Resolve();
        info.GetReturnValue().Set(voice != nullptr && voice->IsPlayerTalking(player));
    }

    void Voice::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (!isolate || global.IsEmpty()) {
            return;
        }
        auto ctx = isolate->GetCurrentContext();

        v8::Local<v8::Object> voiceObj = v8::Object::New(isolate);
        const auto attach              = [&](const char *name, v8::FunctionCallback cb) {
            voiceObj->Set(ctx, v8pp::to_v8(isolate, name), v8::Function::New(ctx, cb).ToLocalChecked()).Check();
        };
        attach("setRange", &Voice::JS_SetRange);
        attach("getRange", &Voice::JS_GetRange);
        attach("setPlayerRange", &Voice::JS_SetPlayerRange);
        attach("getPlayerRange", &Voice::JS_GetPlayerRange);
        attach("setPlayerMuted", &Voice::JS_SetPlayerMuted);
        attach("isPlayerMuted", &Voice::JS_IsPlayerMuted);
        attach("setPlayerDeaf", &Voice::JS_SetPlayerDeaf);
        attach("isPlayerDeaf", &Voice::JS_IsPlayerDeaf);
        attach("setLocalMute", &Voice::JS_SetLocalMute);
        attach("isLocallyMuted", &Voice::JS_IsLocallyMuted);
        attach("isPlayerVoiceEnabled", &Voice::JS_IsPlayerVoiceEnabled);
        attach("isPlayerTalking", &Voice::JS_IsPlayerTalking);
        global->Set(ctx, v8pp::to_v8(isolate, "Voice"), voiceObj).Check();

        auto &metadata = Framework::Scripting::GetScriptingCatalog(isolate).global_object("Voice", "Server-side proximity voice rules: how far voice carries, and who may talk to or hear whom.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setRange",
            v8pp::metadata::docs("void", {v8pp::metadata::param("range", "number", false, "Audibility radius in world units; values <= 0 restore the default of 25.")},
                "Sets how far voice carries for talkers with no override of their own. Connected clients are told, so their playback fades out at the same distance.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getRange", v8pp::metadata::docs("number", {}, "Reads the server-wide proximity range.", "Radius in world units.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setPlayerRange",
            v8pp::metadata::docs("void",
                {
                    v8pp::metadata::param("player", "Entity", false, "Player whose voice carries the given distance."),
                    v8pp::metadata::param("range", "number", false, "Audibility radius in world units; values <= 0 return them to the server-wide range."),
                },
                "Overrides how far one player's voice carries, for whisper and shout modes.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getPlayerRange",
            v8pp::metadata::docs("number", {v8pp::metadata::param("player", "Entity", false, "Player to query.")}, "Reads how far a player's voice carries, with the server-wide default already resolved.", "Radius in world units.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setPlayerMuted",
            v8pp::metadata::docs("void",
                {
                    v8pp::metadata::param("player", "Entity", false, "Player to mute or unmute."),
                    v8pp::metadata::param("muted", "boolean", false, "True to stop their voice reaching anyone."),
                },
                "Server-wide mute: a muted player's voice reaches nobody.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isPlayerMuted",
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("player", "Entity", false, "Player to query.")}, "Checks the server-wide mute flag.", "True when the player's voice reaches nobody.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setPlayerDeaf",
            v8pp::metadata::docs("void",
                {
                    v8pp::metadata::param("player", "Entity", false, "Player to deafen or undeafen."),
                    v8pp::metadata::param("deaf", "boolean", false, "True to stop them receiving anyone's voice."),
                },
                "Server-wide deafen: a deaf player receives nobody.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isPlayerDeaf",
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("player", "Entity", false, "Player to query.")}, "Checks the server-wide deafen flag.", "True when the player receives nobody's voice.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setLocalMute",
            v8pp::metadata::docs("void",
                {
                    v8pp::metadata::param("listener", "Entity", false, "Player who stops hearing the target."),
                    v8pp::metadata::param("target", "Entity", false, "Player the listener stops hearing."),
                    v8pp::metadata::param("muted", "boolean", false, "True to mute, false to restore."),
                },
                "One-way mute between two players, enforced by the server rather than the client.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isLocallyMuted",
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("listener", "Entity", false, "Player doing the muting."),
                    v8pp::metadata::param("target", "Entity", false, "Player being muted."),
                },
                "Checks a one-way mute.", "True when the listener does not receive the target.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isPlayerVoiceEnabled",
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("player", "Entity", false, "Player to query.")},
                "Checks whether a player left voice chat enabled in their own client settings. A preference, not a permission: use setPlayerMuted or setPlayerDeaf to enforce anything.", "True unless the player turned voice chat off.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isPlayerTalking",
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("player", "Entity", false, "Player to query.")},
                "Checks whether a player is speaking right now. Tracks speech rather than the push-to-talk key: silence is dropped before it reaches the server, and the state clears shortly after the last frame. The same signal raises the playerVoiceStart and playerVoiceStop events.",
                "True while the player's voice is reaching the server.")));
    }

} // namespace Framework::Integrations::Server::Scripting::Builtins
