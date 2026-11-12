/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice.h"

#include "core_modules.h"

#include <input/input.h>
#include <scripting/lua/lua_runtime.h>
#include <scripting/scripting_catalog.h>
#include <utils/key_names.h>
#include <voice/client/voice_client.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace Bind = Framework::Scripting::Bind;

    namespace {
        // Null before the client is running, where setters no-op and getters report defaults.
        Framework::Voice::VoiceClient *Resolve() {
            return CoreModules::GetVoiceClient();
        }

        // The spellings scripts use for TransmitMode.
        constexpr const char *kPushToTalk    = "pushToTalk";
        constexpr const char *kVoiceActivity = "voiceActivity";

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
            if (flag.IsString()) {
                return !flag.AsString().empty();
            }
            // Of what is left only the zero of a BigInt is false.
            return flag.Kind() != Bind::ValueKind::Other || flag.ToDisplayString() != "0";
        }

        // The number at `index`. Throws `expected` and gives false for anything else.
        bool ReadNumber(Bind::Call &call, size_t index, const char *expected, double &out) {
            if (!call.Arg(index).IsNumber()) {
                call.ThrowError(expected);
                return false;
            }
            out = call.Arg(index).AsNumber();
            return true;
        }

        bool ReadString(Bind::Call &call, size_t index, const char *expected, std::string &out) {
            if (!call.Arg(index).IsString()) {
                call.ThrowError(expected);
                return false;
            }
            out = call.Arg(index).AsString();
            return true;
        }

        void SetEnabled(Bind::Call &call) {
            if (call.ArgCount() < 1) {
                call.ThrowError("Voice.setEnabled: expected (enabled)");
                return;
            }
            if (auto *voice = Resolve()) {
                voice->SetEnabled(ReadFlag(call, 0));
            }
        }

        void IsEnabled(Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(voice != nullptr && voice->IsEnabled());
        }

        void SetVolume(Bind::Call &call) {
            double volume = 1.0;
            if (!ReadNumber(call, 0, "Voice.setVolume: expected (volume)", volume)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->SetMasterVolume(static_cast<float>(volume));
            }
        }

        void GetVolume(Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(static_cast<double>(voice != nullptr ? voice->GetMasterVolume() : 1.0f));
        }

        void SetHearingRange(Bind::Call &call) {
            double range = 0.0;
            if (!ReadNumber(call, 0, "Voice.setHearingRange: expected (range)", range)) {
                return;
            }
            if (auto *voice = Resolve()) {
                voice->SetHearingRange(static_cast<float>(range));
            }
        }

        void GetHearingRange(Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(static_cast<double>(voice != nullptr ? voice->GetHearingRange() : 0.0f));
        }

        void GetRange(Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(static_cast<double>(voice != nullptr ? voice->GetDefaultSpeakerRange() : Framework::Voice::kDefaultProximityRange));
        }

        void SetTier(Bind::Call &call) {
            const double tier = call.Arg(0).IsNumber() ? call.Arg(0).AsNumber() : -1.0;
            if (tier != 0.0 && tier != 1.0 && tier != 2.0) {
                call.ThrowError("Voice.setTier: expected (tier), 0 (whisper), 1 (normal) or 2 (shout)");
                return;
            }
            if (auto *voice = Resolve()) {
                voice->SetTier(static_cast<Framework::Voice::VoiceTier>(static_cast<uint8_t>(tier)));
            }
        }

        void GetTier(Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(static_cast<uint32_t>(voice != nullptr ? voice->GetTier() : Framework::Voice::VoiceTier::Normal));
        }

        void SetPushToTalkKey(Bind::Call &call) {
            std::string name;
            if (!ReadString(call, 0, "Voice.setPushToTalkKey: expected (key)", name)) {
                return;
            }

            const int vk = Utils::KeyNames::ToVirtualKey(name);
            if (vk < 0) {
                call.ThrowError("Voice.setPushToTalkKey: unknown key name '" + name + "'");
                return;
            }
            if (Framework::Input::IsWheelKey(vk)) {
                call.ThrowError("Voice.setPushToTalkKey: '" + name + "' is never held, so it cannot be push-to-talk");
                return;
            }

            if (auto *voice = Resolve()) {
                voice->SetPushToTalkKey(vk);
            }
        }

        void GetPushToTalkKey(Bind::Call &call) {
            auto *voice  = Resolve();
            const int vk = voice != nullptr ? voice->GetPushToTalkKey() : Framework::Voice::kDefaultPushToTalkKey;
            call.Return(Utils::KeyNames::FromVirtualKey(vk));
        }

        void SetPushToTalkReleaseDelay(Bind::Call &call) {
            double ms = 0.0;
            if (!ReadNumber(call, 0, "Voice.setPushToTalkReleaseDelay: expected (milliseconds)", ms)) {
                return;
            }

            // Bounded to what the cast can hold; the client clamps it far below.
            if (auto *voice = Resolve()) {
                voice->SetPushToTalkReleaseDelay(ms > 0.0 ? static_cast<uint32_t>(std::min(ms, static_cast<double>(std::numeric_limits<uint32_t>::max()))) : 0u);
            }
        }

        void GetPushToTalkReleaseDelay(Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(static_cast<uint32_t>(voice != nullptr ? voice->GetPushToTalkReleaseDelay() : Framework::Voice::kDefaultPushToTalkReleaseMs));
        }

        void IsTalking(Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(voice != nullptr && voice->IsLocalTalking());
        }

        void HasMicrophone(Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(voice != nullptr && voice->HasMicrophone());
        }

        void SetTransmitMode(Bind::Call &call) {
            std::string mode;
            if (!ReadString(call, 0, "Voice.setTransmitMode: expected (mode)", mode)) {
                return;
            }
            if (mode != kPushToTalk && mode != kVoiceActivity) {
                call.ThrowError("Voice.setTransmitMode: unknown mode '" + mode + "'; expected 'pushToTalk' or 'voiceActivity'");
                return;
            }

            if (auto *voice = Resolve()) {
                voice->SetTransmitMode(mode == kVoiceActivity ? Framework::Voice::TransmitMode::VoiceActivity : Framework::Voice::TransmitMode::PushToTalk);
            }
        }

        void GetTransmitMode(Bind::Call &call) {
            auto *voice         = Resolve();
            const bool activity = voice != nullptr && voice->GetTransmitMode() == Framework::Voice::TransmitMode::VoiceActivity;
            call.Return(activity ? kVoiceActivity : kPushToTalk);
        }

        void SetActivationThreshold(Bind::Call &call) {
            double level = 0.0;
            if (!ReadNumber(call, 0, "Voice.setActivationThreshold: expected (level)", level)) {
                return;
            }

            if (auto *voice = Resolve()) {
                voice->SetVoiceActivationThreshold(static_cast<float>(level));
            }
        }

        void GetActivationThreshold(Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(static_cast<double>(voice != nullptr ? voice->GetVoiceActivationThreshold() : Framework::Voice::kDefaultVoiceActivationThreshold));
        }

        void GetInputLevel(Bind::Call &call) {
            // The microphone is open all session, so the ungated level would let any server watch a
            // player who thinks they are muted. IsTransmitting drops on ticks that drained no frame.
            auto *voice = Resolve();
            call.Return(static_cast<double>(voice != nullptr && voice->IsLocalTalking() ? voice->GetInputLevel() : 0.0f));
        }

        void SetNoiseSuppression(Bind::Call &call) {
            if (call.ArgCount() < 1) {
                call.ThrowError("Voice.setNoiseSuppression: expected (enabled)");
                return;
            }

            if (auto *voice = Resolve()) {
                voice->SetNoiseSuppression(ReadFlag(call, 0));
            }
        }

        void IsNoiseSuppressionEnabled(Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(voice != nullptr && voice->IsNoiseSuppressionEnabled());
        }

        void GetInputDevices(Bind::Call &call) {
            auto *voice                          = Resolve();
            const std::vector<std::string> names = voice != nullptr ? voice->ListCaptureDevices() : std::vector<std::string> {};
            Bind::Value list                     = call.NewArray();
            for (const std::string &name : names) {
                list.Append(call.String(name));
            }
            call.Return(list);
        }

        void SetInputDevice(Bind::Call &call) {
            std::string name;
            if (!ReadString(call, 0, "Voice.setInputDevice: expected (name)", name)) {
                return;
            }

            if (auto *voice = Resolve()) {
                voice->SetCaptureDevice(name);
            }
        }

        void GetInputDevice(Bind::Call &call) {
            auto *voice = Resolve();
            call.Return(voice != nullptr ? voice->GetCaptureDevice() : std::string {});
        }
    } // namespace

    const Bind::Namespace &Voice::Definition() {
        using Bind::Doc;
        using Bind::Side;
        using Bind::TypeOf;
        using Bind::Type::Custom;

        static const auto definition = [] {
            const auto number  = TypeOf<double>();
            const auto boolean = TypeOf<bool>();
            const auto text    = TypeOf<std::string>();
            const auto mode    = Custom("'pushToTalk' | 'voiceActivity'", "'pushToTalk' | 'voiceActivity'");
            return Bind::Namespace("Voice", "Local player's proximity voice chat settings: on/off, playback volume, hearing range and the push-to-talk binding.")
                .Function("setEnabled", &SetEnabled, Doc("Turns voice chat on or off. Off closes the microphone and playback devices and tells the server to stop relaying voice to this client.").Param("enabled", boolean, "Whether voice chat runs at all for this player."),
                    Side::Client)
                .Function("isEnabled", &IsEnabled, Doc("Checks whether voice chat is enabled for this player.").ReturnType(boolean).Returns("True unless the player turned voice chat off."), Side::Client)
                .Function("setVolume", &SetVolume, Doc("Sets the playback volume of incoming voice. A game that plays voice through its own audio engine applies the same gain there.").Param("volume", number, "Playback gain, where 1 is unattenuated. Clamped to 0..4."),
                    Side::Client)
                .Function("getVolume", &GetVolume, Doc("Reads the voice playback volume.").ReturnType(number).Returns("Current gain, where 1 is unattenuated."), Side::Client)
                .Function("setHearingRange", &SetHearingRange, Doc("Narrows how far this player hears others. Can only reduce the server's range, since a talker beyond it is never relayed.").Param("range", number, "Audibility radius in world units; 0 removes the local limit."),
                    Side::Client)
                .Function("getHearingRange", &GetHearingRange, Doc("Reads the local hearing-range limit.").ReturnType(number).Returns("Radius in world units, or 0 when the server's range applies unreduced."), Side::Client)
                .Function("getRange", &GetRange, Doc("Reads the server's proximity range for talkers with no override of their own.").ReturnType(number).Returns("Radius in world units."), Side::Client)
                .Function("setTier", &SetTier, Doc("Switches how far this player's voice carries. A request: the server paces it, decides the radius, and may move the player itself.").Param("tier", number, "Voice tier: 0 whisper, 1 normal, 2 shout."), Side::Client)
                .Function("getTier", &GetTier, Doc("Reads the voice tier this player is on.").ReturnType(number).Returns("0 whisper, 1 normal, 2 shout."), Side::Client)
                .Function("setPushToTalkKey", &SetPushToTalkKey, Doc("Rebinds push-to-talk. Unknown key names and the wheel, which is never held, throw.").Param("key", text, "Case-insensitive key name, using the same names as Key.bind."), Side::Client)
                .Function("getPushToTalkKey", &GetPushToTalkKey, Doc("Reads the push-to-talk binding.").ReturnType(text).Returns("Canonical key name, e.g. \"v\"."), Side::Client)
                .Function("setPushToTalkReleaseDelay", &SetPushToTalkReleaseDelay,
                    Doc("Sets how long transmission continues after push-to-talk is released, so letting go slightly early does not clip the end of a word. Muting, turning voice off and starting to type into a text field still stop it at once.")
                        .Param("milliseconds", number, "How long to keep transmitting after the key goes up. Clamped to 0..2000."),
                    Side::Client)
                .Function("getPushToTalkReleaseDelay", &GetPushToTalkReleaseDelay, Doc("Reads the push-to-talk release delay.").ReturnType(number).Returns("Delay in milliseconds; 0 when transmission stops the moment the key is released."), Side::Client)
                .Function("isTalking", &IsTalking,
                    Doc("Checks whether the local player is speaking right now. The same state raises the voiceStart and voiceStop events.")
                        .ReturnType(boolean)
                        .Returns("True while push-to-talk is open -- held, or still inside the release delay -- and the microphone is producing audio."),
                    Side::Client)
                .Function("hasMicrophone", &HasMicrophone, Doc("Checks whether a capture device opened for this session.").ReturnType(boolean).Returns("False when the player has no working microphone, i.e. they are listen-only."), Side::Client)
                .Function("setTransmitMode", &SetTransmitMode,
                    Doc("Chooses what opens the microphone. Switching closes whatever the previous mode had open. Unknown modes throw.")
                        .Param("mode", mode, "pushToTalk sends while the key is held; voiceActivity sends whenever the microphone is louder than the activation threshold."),
                    Side::Client)
                .Function("getTransmitMode", &GetTransmitMode, Doc("Reads what opens the microphone.").ReturnType(mode).Returns("The current transmit mode."), Side::Client)
                .Function("setActivationThreshold", &SetActivationThreshold,
                    Doc("Sets the voice-activation sensitivity: lower sends quieter speech, higher ignores more background noise. Clamped to 0..1.").Param("level", number, "Microphone level, 0 to 1 on the scale getInputLevel reports, above which voice activation sends."),
                    Side::Client)
                .Function("getActivationThreshold", &GetActivationThreshold, Doc("Reads the voice-activation threshold.").ReturnType(number).Returns("Level from 0 to 1."), Side::Client)
                .Function("getInputLevel", &GetInputLevel,
                    Doc("Reads how loud the microphone is while this player is sending voice. The microphone stays open for the whole session, so what it hears the rest of the time is not given to scripts.")
                        .ReturnType(number)
                        .Returns("Smoothed level from 0 to 1 while isTalking is true; 0 otherwise."),
                    Side::Client)
                .Function("setNoiseSuppression", &SetNoiseSuppression,
                    Doc("Turns noise suppression on or off. It runs before the level is measured, so it also keeps steady noise from tripping voice activation.").Param("enabled", boolean, "Whether to filter background noise out of the microphone."), Side::Client)
                .Function("isNoiseSuppressionEnabled", &IsNoiseSuppressionEnabled, Doc("Checks whether noise suppression is on.").ReturnType(boolean).Returns("True while background noise is being filtered."), Side::Client)
                .Function("getInputDevices", &GetInputDevices, Doc("Lists the microphones voice can record from.").ReturnType(TypeOf<std::vector<std::string>>()).Returns("Device names as the player would recognise them; empty when the game chooses the device itself."),
                    Side::Client)
                .Function("setInputDevice", &SetInputDevice,
                    Doc("Chooses the microphone. A running microphone is reopened on the new device; a device that is no longer connected falls back to the default.").Param("name", text, "A name from getInputDevices, or an empty string for the system default."), Side::Client)
                .Function("getInputDevice", &GetInputDevice, Doc("Reads the chosen microphone.").ReturnType(text).Returns("Its name, or an empty string for the system default."), Side::Client);
        }();
        return definition;
    }

    void Voice::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager) {
        if (!isolate || context.IsEmpty() || target.IsEmpty()) {
            return;
        }
        if (resourceManager != nullptr) {
            Bind::SetCallResourceManager(isolate, resourceManager);
        }
        Definition().MaterialiseV8(isolate, context, target, Framework::Scripting::GetScriptingCatalog(isolate), Bind::Side::Client);

        auto &events = Framework::Scripting::GetScriptingCatalog(isolate).data_type("EventMap");
        events.add_property("voiceStart", "[]", "Dispatched when the local player starts speaking, as isTalking turns true.");
        events.add_property("voiceStop", "[]", "Dispatched when the local player stops speaking, as isTalking turns false.");
    }

    void Voice::RegisterLua(Framework::Scripting::LuaRuntime &runtime) {
        Definition().RegisterLuaGlobal(runtime.State(), runtime.Options().side);
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
