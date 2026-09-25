/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "audio_device.h"
#include "i_voice_sink.h"
#include "i_voice_source.h"
#include "mixer.h"
#include "noise_suppressor.h"
#include "playout_buffer.h"
#include "push_to_talk_gate.h"
#include "voice/voice_config.h"
#include "voice_activity_gate.h"

#include <mafianet/RakVoice.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Framework::Networking {
    class NetworkClient;
} // namespace Framework::Networking

namespace Framework::Voice {
    // ~340ms of decoded audio per speaker.
    constexpr size_t kSpeakerRingSamples = 16384;

    // What opens the microphone.
    enum class TransmitMode : uint8_t {
        // A held key, plus the release delay.
        PushToTalk,
        // The microphone's own level crossing a threshold, plus a hold. No key at all.
        VoiceActivity,
    };

    // Built-in output path: mixes audible speakers into the default playback device with
    // distance attenuation and constant-power panning.
    //
    // Submit/ReleaseSpeaker/PublishWorld are main thread; Render runs on the device thread.
    // They meet only through the per-slot rings, the per-slot atomic ids and the world
    // snapshot.
    class LocalVoiceSink final: public IVoiceSink {
      public:
        LocalVoiceSink() = default;
        ~LocalVoiceSink() override;

        // The device thread holds a pointer to this object.
        LocalVoiceSink(const LocalVoiceSink &)            = delete;
        LocalVoiceSink &operator=(const LocalVoiceSink &) = delete;

        // False when there is no playback device; the sink stays inert and Submit discards.
        bool Start();
        // Idempotent. Joins the device thread before touching slot state.
        void Stop();

        bool IsRunning() const {
            return _device.IsRunning();
        }

        void Submit(uint64_t speaker, const int16_t *mono, uint32_t samples) override;
        void ReleaseSpeaker(uint64_t speaker) override;

        // Listener and speakers replace the mixer's world as one atomic flip; published
        // separately, the mixer could pan a new orientation against a stale position.
        void PublishWorld(const ListenerTransform &listener, const SpeakerPlacement *speakers, size_t count) override;

        // Applied after mixing, before limiting. Clamped to [0, 4].
        void SetMasterVolume(float volume);

        float GetMasterVolume() const {
            return _masterVolume.load(std::memory_order_relaxed);
        }

        // Full-volume radius as a fraction of range. Atomic: the audio thread reads it per frame
        // while the game sets it from a settings UI. Clamped to (0, 1].
        void SetFullVolumeFraction(float fraction);

        float GetFullVolumeFraction() const {
            return _fullVolumeFraction.load(std::memory_order_relaxed);
        }

      private:
        struct Slot {
            // 0 = free. Published last on acquire, cleared first on release.
            std::atomic<uint64_t> id {0};
            PlayoutBuffer<kSpeakerRingSamples> audio;
        };

        struct World {
            ListenerTransform listener;
            std::array<uint64_t, kMaxAudibleTalkers> id {};
            std::array<glm::vec3, kMaxAudibleTalkers> position {};
            std::array<float, kMaxAudibleTalkers> range {};
        };

        static void Render(float *stereoOut, uint32_t frameCount, void *user);
        void RenderInto(float *stereoOut, uint32_t frameCount);

        int FindSlot(uint64_t speaker) const;
        // Free slot whose ring has already drained, or -1.
        int AcquireSlot(uint64_t speaker);

        PlaybackDevice _device;
        std::array<Slot, kMaxAudibleTalkers> _slots;

        // Triple buffered: with two, the writer's next target is the buffer a reader picked
        // up one callback ago. _writeSlot and _previousSlot are main thread only.
        std::array<World, 3> _world {};
        std::atomic<uint32_t> _publishedSlot {0};
        uint32_t _writeSlot    = 1;
        uint32_t _previousSlot = 0;

        std::atomic<float> _masterVolume {1.0f};
        std::atomic<float> _fullVolumeFraction {kDefaultFullVolumeFraction};
    };

    // Client half of voice: the RakVoice relay session, push-to-talk, and speaker admission.
    // Main thread only -- RakVoice must never be touched from the audio callback.
    class VoiceClient final {
      public:
        VoiceClient() = default;
        ~VoiceClient();

        // The RakVoice member's address is registered with RakPeer.
        VoiceClient(const VoiceClient &)            = delete;
        VoiceClient &operator=(const VoiceClient &) = delete;

        // Attaches to the peer. The audio devices open with the relay session rather than here.
        bool Init(Networking::NetworkClient *client);
        // Idempotent.
        void Shutdown();

        // Once per client tick.
        void Update();

        // --- player settings ---

        // Off closes the session outright: no capture, playback or decode, and the server is
        // told to stop relaying to this client.
        void SetEnabled(bool enabled);

        bool IsEnabled() const {
            return _enabled;
        }

        // Ceiling on how far this client hears, in world units; <= 0 for none. Can only
        // narrow the server's range -- a talker past that is never relayed in the first place.
        void SetHearingRange(float range);

        float GetHearingRange() const {
            return _hearingRange;
        }

        // The server's radius, for talkers with no override. Set from the VoiceSettings RPC.
        void SetDefaultSpeakerRange(float range);

        float GetDefaultSpeakerRange() const {
            return _defaultSpeakerRange;
        }

        // --- microphone ---

        // Push-to-talk by default. Switching cuts whatever the old mode had open.
        void SetTransmitMode(TransmitMode mode);

        TransmitMode GetTransmitMode() const {
            return _transmitMode;
        }

        // Voice activation's trigger level, as full-scale RMS in [0, 1] -- the scale
        // GetInputLevel reports, so a settings UI can draw one against the other.
        void SetVoiceActivationThreshold(float threshold) {
            _vad.SetThreshold(threshold);
        }

        float GetVoiceActivationThreshold() const {
            return _vad.GetThreshold();
        }

        // How long voice activation stays open after the level drops, in milliseconds.
        void SetVoiceActivationHold(uint32_t ms) {
            _vad.SetHold(ms);
        }

        uint32_t GetVoiceActivationHold() const {
            return _vad.GetHold();
        }

        // RNNoise over the microphone, ahead of the level, the gate and the encoder. Off by
        // default: it costs a little CPU and colours some voices.
        void SetNoiseSuppression(bool enabled);

        bool IsNoiseSuppressionEnabled() const {
            return _noiseSuppression;
        }

        // The recording devices the installed source can open, and the one it opens. An engine
        // source that cannot choose lists none. Changing it restarts a running microphone.
        std::vector<std::string> ListCaptureDevices() const;
        void SetCaptureDevice(const std::string &name);
        std::string GetCaptureDevice() const;

        // The raw key state. Gating conditions belong in SetTransmitBlocked: folded in here they
        // would extend the release delay rather than cut it. Ignored under voice activation.
        void SetPushToTalk(bool held);

        bool IsPushToTalkHeld() const {
            return _ptt.IsHeld();
        }

        // Milliseconds, clamped to kMaxPushToTalkReleaseMs. 0 stops on release.
        void SetPushToTalkReleaseDelay(uint32_t ms) {
            _ptt.SetReleaseDelay(ms);
        }

        uint32_t GetPushToTalkReleaseDelay() const {
            return _ptt.GetReleaseDelay();
        }

        // The push-to-talk binding, as a Win32 virtual-key code. Stored, not polled: the host
        // mod polls it and feeds the result back through SetPushToTalk.
        void SetPushToTalkKey(int virtualKey) {
            _pushToTalkKey = virtualKey;
        }

        int GetPushToTalkKey() const {
            return _pushToTalkKey;
        }

        // False outside a session, and inside one when the installed source found no
        // microphone -- the client is listen-only.
        bool HasMicrophone() const {
            return _source != nullptr && _source->IsRunning();
        }

        bool IsTransmitting() const {
            return _transmitting;
        }

        // --- speech envelope ---

        // Smoothed loudness in [0, 1] of the frames handed to the sink, for mouth animation.
        float GetSpeakerLevel(uint64_t speaker) const;

        // The same for our own microphone; non-zero only while transmitting.
        float GetLocalLevel() const {
            return _localLevel;
        }

        // How loud the microphone is, sent or not: after noise suppression, before the gate.
        // What a sensitivity setting is tuned against. 0 while no microphone is open.
        float GetInputLevel() const {
            return _inputLevel;
        }

        // --- talking state ---

        // Whether the local player is speaking -- push-to-talk held or voice activation open,
        // and audio going out -- debounced so a tick that drained no capture frame does not
        // read as a stop. A remote talker's counterpart is IsSpeakerTalking, which answers
        // for this client's playback rather than for the other player.
        bool IsLocalTalking() const {
            return _localTalking;
        }

        // Whether this client is hearing `speaker` right now, held across the gaps between
        // words for kSpeakerTalkingHoldMs. A fact about this client's playback, not the other player: someone out of
        // range, muted for us or outside the audible set reads as silent.
        bool IsSpeakerTalking(uint64_t speaker) const;

        // --- spatialisation ---

        // The one thing the framework cannot derive itself, since it depends on the game
        // camera. Z-up games must pass up as +Z; the mixer's +Y default collapses the pan.
        void SetListenerTransform(const ListenerTransform &listener) {
            _listener    = listener;
            _listenerSet = true;
        }

        const ListenerTransform &GetListenerTransform() const {
            return _listener;
        }

        // Speakers not named between these two calls are dropped. The client Instance drives
        // a pass per tick from the replicated entity set.
        void BeginSpeakerUpdate();
        void EndSpeakerUpdate();

        // Speakers with no known position are still heard, but are evicted first. Our own
        // GUID is ignored.
        void SetSpeakerPosition(uint64_t speaker, const glm::vec3 &position);
        // <= 0 restores the server's default range.
        void SetSpeakerRange(uint64_t speaker, float range);
        void RemoveSpeaker(uint64_t speaker);

        // Blocks transmission regardless of push-to-talk, cutting the release delay short. Set by
        // the client Instance while its chat box has the caret or a web view holds focus.
        void SetInputSuppressed(bool suppressed);

        // The mod-owned half of the same block: window focus, a game menu, locked controls. Ored
        // with the framework's, so neither side clears the other's.
        void SetTransmitBlocked(bool blocked);

        // --- output ---

        // Redirects decoded audio to a game engine; nullptr restores the built-in mixer.
        // The built-in device is stopped while a custom sink is installed.
        void SetSink(IVoiceSink *sink);

        IVoiceSink *GetSink() const {
            return _sink;
        }

        // --- input ---

        // Takes the microphone from a game engine instead of the built-in capture device;
        // nullptr restores the built-in one. The two never hold the microphone at once.
        void SetSource(IVoiceSource *source);

        IVoiceSource *GetSource() const {
            return _source;
        }

        // The player's voice volume, in [0, 4]. Stored on the built-in mixer, which applies it;
        // an engine sink reads it back and applies it on whatever its engine mixes voice into,
        // the same way it reads the rolloff below.
        void SetMasterVolume(float volume) {
            _localSink.SetMasterVolume(volume);
        }

        float GetMasterVolume() const {
            return _localSink.GetMasterVolume();
        }

        // Rolloff shape: the fraction of range within which a speaker is at full volume. Raising
        // it flattens the near field without moving the cutoff, which is the knob a player who
        // says "I can't hear anyone standing next to me" actually needs.
        //
        // Stored on the built-in mixer, but it describes the curve rather than the renderer, so
        // an engine sink reads it back and shapes its own attenuation the same way.
        void SetFullVolumeFraction(float fraction) {
            _localSink.SetFullVolumeFraction(fraction);
        }

        float GetFullVolumeFraction() const {
            return _localSink.GetFullVolumeFraction();
        }

      private:
        struct AdmittedSpeaker {
            uint64_t id       = 0;
            int64_t lastFrame = 0;
            float level       = 0.0f;
            // The level holds until the audio handed to the sink has played out.
            int64_t audioUntil = 0;
        };

        // Tagged with the pass that last touched it, so EndSpeakerUpdate can retire the rest.
        struct PlacementEntry {
            SpeakerPlacement placement;
            uint32_t generation = 0;
        };

        // The server GUID and our own only exist while connected, so the session is opened
        // and closed around each connection rather than at Init.
        void UpdateSession();
        void OpenSession();
        void CloseSession();

        // Tells the server whether to keep relaying to us. No-op until the connection settles.
        void PublishPreference();

        // Own override, else the server default, then narrowed by the hearing range.
        float ResolveRange(uint64_t speaker) const;

        // Opened with the session, not at Init: miniaudio's WASAPI backend CoInitializes the
        // calling thread into the MTA and holds it, and an injected mod's Init can run before the
        // host game has chosen its own apartment. See Init.
        void StartDevices();
        void StopDevices();

        void PumpCapture();
        // Whichever gate the mode uses, closed at once.
        void CutGates();
        void KeepPreRoll(const int16_t *frame);
        void SendPreRoll();
        void PumpSpeakers();
        void PublishWorld();

        int FindAdmitted(uint64_t speaker) const;
        bool IsSelf(uint64_t speaker) const;
        // Evicts the most distant talker if needed; -1 when every slot holds someone nearer.
        int AdmitSpeaker(uint64_t speaker, int64_t nowMs);
        void ReleaseAdmitted(int slot);
        // Infinity when the position is unknown.
        float DistanceSqTo(uint64_t speaker) const;

        Networking::NetworkClient *_client = nullptr;
        MafiaNet::RakVoice _voice;
        bool _attached        = false;
        bool _sessionOpen     = false;
        bool _enabled         = true;
        bool _inputSuppressed = false;
        bool _transmitBlocked = false;
        bool _transmitting    = false;
        bool _preferenceSent  = false;
        float _localLevel     = 0.0f;
        float _inputLevel     = 0.0f;
        bool _localTalking    = false;
        // Holds our own state across a tick that drained no capture frame.
        int64_t _localTalkingUntil = 0;
        // One step per tick, shared by every envelope.
        int64_t _envelopeMs = 0;
        float _envelopeStep = 0.0f;
        int _pushToTalkKey  = kDefaultPushToTalkKey;
        TransmitMode _transmitMode = TransmitMode::PushToTalk;
        PushToTalkGate _ptt {};
        VoiceActivityGate _vad {};
        // Whether the last captured frame went out, so the first one to open voice activation
        // knows to send the pre-roll ahead of itself.
        bool _gateWasOpen = false;
        std::array<std::array<int16_t, kFrameSamples>, kVoiceActivationPreRollFrames> _preRoll {};
        uint32_t _preRollCount = 0;
        uint32_t _preRollNext  = 0;
        bool _noiseSuppression = false;
        NoiseSuppressor _denoiser;
        MafiaNet::RakNetGUID _self {};
        MafiaNet::RakNetGUID _server {};

        CaptureDevice _capture;
        LocalVoiceSink _localSink;
        IVoiceSink *_sink     = nullptr;
        IVoiceSource *_source = nullptr;

        ListenerTransform _listener {};
        bool _listenerSet    = false;
        bool _listenerWarned = false;

        std::unordered_map<uint64_t, PlacementEntry> _placements;
        // Not generational, unlike _placements: a range must outlive the talker streaming out.
        std::unordered_map<uint64_t, float> _speakerRanges;
        uint32_t _placementGeneration = 0;
        float _hearingRange           = 0.0f;
        float _defaultSpeakerRange    = kDefaultProximityRange;
        std::array<AdmittedSpeaker, kMaxAudibleTalkers> _admitted {};

        // Reused every tick so the per-frame path never allocates.
        std::array<int16_t, kFrameSamples> _frame {};
        std::vector<SpeakerPlacement> _published;
        DataStructures::List<MafiaNet::RakNetGUID> _activeSpeakers;
    };
} // namespace Framework::Voice
