/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice.h"

#include "core_modules.h"

#include <scripting/scripting_catalog.h>
#include <utils/key_names.h>
#include <voice/client/voice_client.h>

#include <v8pp/convert.hpp>

#include <string>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace {
        void ThrowError(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, message)));
        }

        // Null before the client is running, where setters no-op and getters report defaults.
        Framework::Voice::VoiceClient *Resolve() {
            return CoreModules::GetVoiceClient();
        }
    } // namespace

    void Voice::SetEnabledCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1) {
            ThrowError(isolate, "Voice.setEnabled: expected (enabled)");
            return;
        }
        if (auto *voice = Resolve()) {
            voice->SetEnabled(args[0]->BooleanValue(isolate));
        }
    }

    void Voice::IsEnabledCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *voice = Resolve();
        args.GetReturnValue().Set(voice != nullptr && voice->IsEnabled());
    }

    void Voice::SetVolumeCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1 || !args[0]->IsNumber()) {
            ThrowError(isolate, "Voice.setVolume: expected (volume)");
            return;
        }
        if (auto *voice = Resolve()) {
            voice->SetMasterVolume(static_cast<float>(args[0]->NumberValue(isolate->GetCurrentContext()).FromMaybe(1.0)));
        }
    }

    void Voice::GetVolumeCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *voice = Resolve();
        args.GetReturnValue().Set(voice != nullptr ? voice->GetMasterVolume() : 1.0f);
    }

    void Voice::SetHearingRangeCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1 || !args[0]->IsNumber()) {
            ThrowError(isolate, "Voice.setHearingRange: expected (range)");
            return;
        }
        if (auto *voice = Resolve()) {
            voice->SetHearingRange(static_cast<float>(args[0]->NumberValue(isolate->GetCurrentContext()).FromMaybe(0.0)));
        }
    }

    void Voice::GetHearingRangeCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *voice = Resolve();
        args.GetReturnValue().Set(voice != nullptr ? voice->GetHearingRange() : 0.0f);
    }

    void Voice::GetRangeCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *voice = Resolve();
        args.GetReturnValue().Set(voice != nullptr ? voice->GetDefaultSpeakerRange() : Framework::Voice::kDefaultProximityRange);
    }

    void Voice::SetPushToTalkKeyCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1 || !args[0]->IsString()) {
            ThrowError(isolate, "Voice.setPushToTalkKey: expected (key)");
            return;
        }

        const std::string name = v8pp::from_v8<std::string>(isolate, args[0]);
        const int vk           = Utils::KeyNames::ToVirtualKey(name);
        if (vk < 0) {
            ThrowError(isolate, "Voice.setPushToTalkKey: unknown key name '" + name + "'");
            return;
        }

        if (auto *voice = Resolve()) {
            voice->SetPushToTalkKey(vk);
        }
    }

    void Voice::GetPushToTalkKeyCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);

        auto *voice    = Resolve();
        const int vk   = voice != nullptr ? voice->GetPushToTalkKey() : Framework::Voice::kDefaultPushToTalkKey;
        args.GetReturnValue().Set(v8pp::to_v8(isolate, Utils::KeyNames::FromVirtualKey(vk)));
    }

    void Voice::SetPushToTalkReleaseDelayCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1 || !args[0]->IsNumber()) {
            ThrowError(isolate, "Voice.setPushToTalkReleaseDelay: expected (milliseconds)");
            return;
        }

        const double ms = args[0]->NumberValue(isolate->GetCurrentContext()).FromMaybe(0.0);
        if (auto *voice = Resolve()) {
            voice->SetPushToTalkReleaseDelay(ms > 0.0 ? static_cast<uint32_t>(ms) : 0u);
        }
    }

    void Voice::GetPushToTalkReleaseDelayCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *voice = Resolve();
        args.GetReturnValue().Set(static_cast<uint32_t>(voice != nullptr ? voice->GetPushToTalkReleaseDelay() : Framework::Voice::kDefaultPushToTalkReleaseMs));
    }

    void Voice::IsTalkingCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *voice = Resolve();
        args.GetReturnValue().Set(voice != nullptr && voice->IsLocalTalking());
    }

    void Voice::HasMicrophoneCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *voice = Resolve();
        args.GetReturnValue().Set(voice != nullptr && voice->HasMicrophone());
    }

    void Voice::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager) {
        (void)resourceManager;
        if (!isolate || context.IsEmpty() || target.IsEmpty()) {
            return;
        }

        const auto attach = [&](v8::Local<v8::Object> obj, const char *name, v8::FunctionCallback callback) {
            v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate, callback);
            obj->Set(context, v8pp::to_v8(isolate, name), tmpl->GetFunction(context).ToLocalChecked()).Check();
        };

        v8::Local<v8::Object> voiceObj = v8::Object::New(isolate);
        attach(voiceObj, "setEnabled", &Voice::SetEnabledCallback);
        attach(voiceObj, "isEnabled", &Voice::IsEnabledCallback);
        attach(voiceObj, "setVolume", &Voice::SetVolumeCallback);
        attach(voiceObj, "getVolume", &Voice::GetVolumeCallback);
        attach(voiceObj, "setHearingRange", &Voice::SetHearingRangeCallback);
        attach(voiceObj, "getHearingRange", &Voice::GetHearingRangeCallback);
        attach(voiceObj, "getRange", &Voice::GetRangeCallback);
        attach(voiceObj, "setPushToTalkKey", &Voice::SetPushToTalkKeyCallback);
        attach(voiceObj, "getPushToTalkKey", &Voice::GetPushToTalkKeyCallback);
        attach(voiceObj, "setPushToTalkReleaseDelay", &Voice::SetPushToTalkReleaseDelayCallback);
        attach(voiceObj, "getPushToTalkReleaseDelay", &Voice::GetPushToTalkReleaseDelayCallback);
        attach(voiceObj, "isTalking", &Voice::IsTalkingCallback);
        attach(voiceObj, "hasMicrophone", &Voice::HasMicrophoneCallback);
        target->Set(context, v8pp::to_v8(isolate, "Voice"), voiceObj).Check();

        auto &metadata = Framework::Scripting::GetScriptingCatalog(isolate).global_object("Voice", "Local player's proximity voice chat settings: on/off, playback volume, hearing range and the push-to-talk binding.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setEnabled",
            v8pp::metadata::docs("void", {v8pp::metadata::param("enabled", "boolean", false, "Whether voice chat runs at all for this player.")},
                "Turns voice chat on or off. Off closes the microphone and playback devices and tells the server to stop relaying voice to this client.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isEnabled", v8pp::metadata::docs("boolean", {}, "Checks whether voice chat is enabled for this player.", "True unless the player turned voice chat off.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setVolume",
            v8pp::metadata::docs("void", {v8pp::metadata::param("volume", "number", false, "Playback gain, where 1 is unattenuated. Clamped to 0..4.")},
                "Sets the playback volume of incoming voice. Applies to the built-in mixer only.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getVolume", v8pp::metadata::docs("number", {}, "Reads the voice playback volume.", "Current gain, where 1 is unattenuated.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setHearingRange",
            v8pp::metadata::docs("void", {v8pp::metadata::param("range", "number", false, "Audibility radius in world units; 0 removes the local limit.")},
                "Narrows how far this player hears others. Can only reduce the server's range, since a talker beyond it is never relayed.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getHearingRange", v8pp::metadata::docs("number", {}, "Reads the local hearing-range limit.", "Radius in world units, or 0 when the server's range applies unreduced.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getRange", v8pp::metadata::docs("number", {}, "Reads the server's proximity range for talkers with no override of their own.", "Radius in world units.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setPushToTalkKey",
            v8pp::metadata::docs("void", {v8pp::metadata::param("key", "string", false, "Case-insensitive key name, using the same names as Key.bind.")},
                "Rebinds push-to-talk. Unknown key names throw.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getPushToTalkKey", v8pp::metadata::docs("string", {}, "Reads the push-to-talk binding.", "Canonical key name, e.g. \"v\".")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setPushToTalkReleaseDelay",
            v8pp::metadata::docs("void", {v8pp::metadata::param("milliseconds", "number", false, "How long to keep transmitting after the key goes up. Clamped to 0..2000.")},
                "Sets how long transmission continues after push-to-talk is released, so letting go slightly early does not clip the end of a word. Muting, turning voice off and losing input focus still stop it at once.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getPushToTalkReleaseDelay",
            v8pp::metadata::docs("number", {}, "Reads the push-to-talk release delay.", "Delay in milliseconds; 0 when transmission stops the moment the key is released.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isTalking",
            v8pp::metadata::docs("boolean", {}, "Checks whether the local player is speaking right now. The same state raises the voiceStart and voiceStop events.",
                "True while push-to-talk is open -- held, or still inside the release delay -- and the microphone is producing audio.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("hasMicrophone", v8pp::metadata::docs("boolean", {}, "Checks whether a capture device opened for this session.", "False when the player has no working microphone, i.e. they are listen-only.")));
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
