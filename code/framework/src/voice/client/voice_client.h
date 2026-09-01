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
#include "mixer.h"
#include "push_to_talk_gate.h"
#include "voice/voice_config.h"

#include <mafianet/RakVoice.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Framework::Networking {
    class NetworkClient;
} // namespace Framework::Networking

namespace Framework::Voice {
    // ~340ms of decoded audio per speaker.
    constexpr size_t kSpeakerRingSamples = 16384;

    struct SpeakerPlacement {
        uint64_t speaker = 0;
        glm::vec3 position {0.0f};
        float range = kDefaultProximityRange;
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
        void PublishWorld(const ListenerTransform &listener, const SpeakerPlacement *speakers, size_t count);

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
            SpscRing<int16_t, kSpeakerRingSamples> pcm;
            // Jitter buffer filled enough to play. Audio thread only, so not atomic.
            bool primed = false;
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

        // The raw key state. Gating conditions belong in SetTransmitBlocked: folded in here they
        // would extend the release delay rather than cut it.
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

        // False outside a session, and inside one when there is no capture device -- the client is
        // listen-only.
        bool HasMicrophone() const {
            return _capture.IsRunning();
        }

        bool IsTransmitting() const {
            return _transmitting;
        }

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

        // Built-in mixer only; a custom sink applies its own gain.
        void SetMasterVolume(float volume) {
            _localSink.SetMasterVolume(volume);
        }

        float GetMasterVolume() const {
            return _localSink.GetMasterVolume();
        }

        // Rolloff shape: the fraction of range within which a speaker is at full volume. Raising
        // it flattens the near field without moving the cutoff, which is the knob a player who
        // says "I can't hear anyone standing next to me" actually needs.
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
        int _pushToTalkKey    = kDefaultPushToTalkKey;
        PushToTalkGate _ptt {};
        MafiaNet::RakNetGUID _self {};
        MafiaNet::RakNetGUID _server {};

        CaptureDevice _capture;
        LocalVoiceSink _localSink;
        IVoiceSink *_sink = nullptr;

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
