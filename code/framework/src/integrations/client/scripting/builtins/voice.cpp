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
#include <vector>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace {
        void ThrowError(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, message)));
        }

        // Null before the client is running, where setters no-op and getters report defaults.
        Framework::Voice::VoiceClient *Resolve() {
            return CoreModules::GetVoiceClient();
        }

        // The spellings scripts use for TransmitMode.
        constexpr const char *kPushToTalk    = "pushToTalk";
        constexpr const char *kVoiceActivity = "voiceActivity";
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

    void Voice::SetTransmitModeCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1 || !args[0]->IsString()) {
            ThrowError(isolate, "Voice.setTransmitMode: expected (mode)");
            return;
        }

        const std::string mode = v8pp::from_v8<std::string>(isolate, args[0]);
        if (mode != kPushToTalk && mode != kVoiceActivity) {
            ThrowError(isolate, "Voice.setTransmitMode: unknown mode '" + mode + "'; expected 'pushToTalk' or 'voiceActivity'");
            return;
        }

        if (auto *voice = Resolve()) {
            voice->SetTransmitMode(mode == kVoiceActivity ? Framework::Voice::TransmitMode::VoiceActivity : Framework::Voice::TransmitMode::PushToTalk);
        }
    }

    void Voice::GetTransmitModeCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);

        auto *voice         = Resolve();
        const bool activity = voice != nullptr && voice->GetTransmitMode() == Framework::Voice::TransmitMode::VoiceActivity;
        args.GetReturnValue().Set(v8pp::to_v8(isolate, activity ? kVoiceActivity : kPushToTalk));
    }

    void Voice::SetActivationThresholdCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1 || !args[0]->IsNumber()) {
            ThrowError(isolate, "Voice.setActivationThreshold: expected (level)");
            return;
        }

        if (auto *voice = Resolve()) {
            voice->SetVoiceActivationThreshold(static_cast<float>(args[0]->NumberValue(isolate->GetCurrentContext()).FromMaybe(Framework::Voice::kDefaultVoiceActivationThreshold)));
        }
    }

    void Voice::GetActivationThresholdCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *voice = Resolve();
        args.GetReturnValue().Set(voice != nullptr ? voice->GetVoiceActivationThreshold() : Framework::Voice::kDefaultVoiceActivationThreshold);
    }

    void Voice::GetInputLevelCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *voice = Resolve();
        args.GetReturnValue().Set(voice != nullptr ? voice->GetInputLevel() : 0.0f);
    }

    void Voice::SetNoiseSuppressionCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1) {
            ThrowError(isolate, "Voice.setNoiseSuppression: expected (enabled)");
            return;
        }

        if (auto *voice = Resolve()) {
            voice->SetNoiseSuppression(args[0]->BooleanValue(isolate));
        }
    }

    void Voice::IsNoiseSuppressionEnabledCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *voice = Resolve();
        args.GetReturnValue().Set(voice != nullptr && voice->IsNoiseSuppressionEnabled());
    }

    void Voice::GetInputDevicesCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);

        auto *voice                          = Resolve();
        const std::vector<std::string> names = voice != nullptr ? voice->ListCaptureDevices() : std::vector<std::string> {};
        args.GetReturnValue().Set(v8pp::to_v8(isolate, names));
    }

    void Voice::SetInputDeviceCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1 || !args[0]->IsString()) {
            ThrowError(isolate, "Voice.setInputDevice: expected (name)");
            return;
        }

        if (auto *voice = Resolve()) {
            voice->SetCaptureDevice(v8pp::from_v8<std::string>(isolate, args[0]));
        }
    }

    void Voice::GetInputDeviceCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);

        auto *voice = Resolve();
        args.GetReturnValue().Set(v8pp::to_v8(isolate, voice != nullptr ? voice->GetCaptureDevice() : std::string {}));
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
        attach(voiceObj, "setTransmitMode", &Voice::SetTransmitModeCallback);
        attach(voiceObj, "getTransmitMode", &Voice::GetTransmitModeCallback);
        attach(voiceObj, "setActivationThreshold", &Voice::SetActivationThresholdCallback);
        attach(voiceObj, "getActivationThreshold", &Voice::GetActivationThresholdCallback);
        attach(voiceObj, "getInputLevel", &Voice::GetInputLevelCallback);
        attach(voiceObj, "setNoiseSuppression", &Voice::SetNoiseSuppressionCallback);
        attach(voiceObj, "isNoiseSuppressionEnabled", &Voice::IsNoiseSuppressionEnabledCallback);
        attach(voiceObj, "getInputDevices", &Voice::GetInputDevicesCallback);
        attach(voiceObj, "setInputDevice", &Voice::SetInputDeviceCallback);
        attach(voiceObj, "getInputDevice", &Voice::GetInputDeviceCallback);
        target->Set(context, v8pp::to_v8(isolate, "Voice"), voiceObj).Check();

        auto &metadata = Framework::Scripting::GetScriptingCatalog(isolate).global_object("Voice", "Local player's proximity voice chat settings: on/off, playback volume, hearing range and the push-to-talk binding.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setEnabled",
            v8pp::metadata::docs("void", {v8pp::metadata::param("enabled", "boolean", false, "Whether voice chat runs at all for this player.")},
                "Turns voice chat on or off. Off closes the microphone and playback devices and tells the server to stop relaying voice to this client.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isEnabled", v8pp::metadata::docs("boolean", {}, "Checks whether voice chat is enabled for this player.", "True unless the player turned voice chat off.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setVolume", v8pp::metadata::docs("void", {v8pp::metadata::param("volume", "number", false, "Playback gain, where 1 is unattenuated. Clamped to 0..4.")},
                                                                                           "Sets the playback volume of incoming voice. A game that plays voice through its own audio engine applies the same gain there.")));
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
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setTransmitMode",
            v8pp::metadata::docs("void", {v8pp::metadata::param("mode", "'pushToTalk' | 'voiceActivity'", false, "pushToTalk sends while the key is held; voiceActivity sends whenever the microphone is louder than the activation threshold.")},
                "Chooses what opens the microphone. Switching closes whatever the previous mode had open. Unknown modes throw.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getTransmitMode", v8pp::metadata::docs("'pushToTalk' | 'voiceActivity'", {}, "Reads what opens the microphone.", "The current transmit mode.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setActivationThreshold", v8pp::metadata::docs("void", {v8pp::metadata::param("level", "number", false, "Microphone level, 0 to 1 on the scale getInputLevel reports, above which voice activation sends.")},
                                                                                                        "Sets the voice-activation sensitivity: lower sends quieter speech, higher ignores more background noise. Clamped to 0..1.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getActivationThreshold", v8pp::metadata::docs("number", {}, "Reads the voice-activation threshold.", "Level from 0 to 1.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getInputLevel",
            v8pp::metadata::docs("number", {}, "Reads how loud the microphone is right now, whether or not anything is being sent -- the value to draw a level meter from and to tune the activation threshold against.",
                "Smoothed level from 0 to 1; 0 while no microphone is open.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setNoiseSuppression", v8pp::metadata::docs("void", {v8pp::metadata::param("enabled", "boolean", false, "Whether to filter background noise out of the microphone.")},
                                                                                                     "Turns noise suppression on or off. It runs before the level is measured, so it also keeps steady noise from tripping voice activation.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isNoiseSuppressionEnabled", v8pp::metadata::docs("boolean", {}, "Checks whether noise suppression is on.", "True while background noise is being filtered.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getInputDevices", v8pp::metadata::docs("string[]", {}, "Lists the microphones voice can record from.", "Device names as the player would recognise them; empty when the game chooses the device itself.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setInputDevice", v8pp::metadata::docs("void", {v8pp::metadata::param("name", "string", false, "A name from getInputDevices, or an empty string for the system default.")},
                                                                                                "Chooses the microphone. A running microphone is reopened on the new device; a device that is no longer connected falls back to the default.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getInputDevice", v8pp::metadata::docs("string", {}, "Reads the chosen microphone.", "Its name, or an empty string for the system default.")));
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
