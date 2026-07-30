/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice_client.h"

#include <logging/logger.h>
#include <networking/network_client.h>
#include <utils/time.h>

#include <algorithm>
#include <limits>

namespace Framework::Voice {
    namespace {
        // Sized to sit on the device thread's stack.
        constexpr uint32_t kRenderChunkSamples = 480;

        // How much nearer, in squared distance, a newcomer must be to take the farthest
        // talker's slot. Stops two speakers at similar range trading it every tick.
        constexpr float kEvictionHysteresis = 1.2f;
    } // namespace

    // ------------------------------------------------------------------------------------
    // LocalVoiceSink
    // ------------------------------------------------------------------------------------

    LocalVoiceSink::~LocalVoiceSink() {
        Stop();
    }

    bool LocalVoiceSink::Start() {
        return _device.Start(&LocalVoiceSink::Render, this);
    }

    void LocalVoiceSink::Stop() {
        // Joins the device thread first, so the teardown below cannot race a render.
        _device.Stop();

        for (Slot &slot : _slots) {
            slot.id.store(0, std::memory_order_relaxed);
            slot.pcm.Clear();
        }
    }

    int LocalVoiceSink::FindSlot(uint64_t speaker) const {
        for (size_t i = 0; i < _slots.size(); i++) {
            if (_slots[i].id.load(std::memory_order_relaxed) == speaker) {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    int LocalVoiceSink::AcquireSlot(uint64_t speaker) {
        for (size_t i = 0; i < _slots.size(); i++) {
            Slot &slot = _slots[i];
            if (slot.id.load(std::memory_order_relaxed) != 0) {
                continue;
            }

            // Wait for the audio thread to drain what the previous occupant left. Clearing
            // from this thread would race the consumer.
            if (slot.pcm.Available() != 0) {
                continue;
            }

            slot.id.store(speaker, std::memory_order_release);
            return static_cast<int>(i);
        }

        return -1;
    }

    void LocalVoiceSink::Submit(uint64_t speaker, const int16_t *mono, uint32_t samples) {
        if (speaker == 0 || mono == nullptr || samples == 0) {
            return;
        }

        int slot = FindSlot(speaker);
        if (slot < 0) {
            slot = AcquireSlot(speaker);
        }

        if (slot < 0) {
            return;
        }

        // A full ring means the device is not consuming; dropping beats playing stale audio.
        _slots[slot].pcm.Push(mono, samples);
    }

    void LocalVoiceSink::ReleaseSpeaker(uint64_t speaker) {
        const int slot = FindSlot(speaker);
        if (slot < 0) {
            return;
        }

        _slots[slot].id.store(0, std::memory_order_release);
    }

    void LocalVoiceSink::PublishWorld(const ListenerTransform &listener, const SpeakerPlacement *speakers, size_t count) {
        World &back = _world[_writeSlot];

        back.listener = listener;
        back.id.fill(0);

        for (size_t i = 0; i < count; i++) {
            const int slot = FindSlot(speakers[i].speaker);
            if (slot < 0) {
                continue;
            }

            back.id[slot]       = speakers[i].speaker;
            back.position[slot] = speakers[i].position;
            back.range[slot]    = speakers[i].range;
        }

        _publishedSlot.store(_writeSlot, std::memory_order_release);

        // The three indices sum to 3, so the one no reader can hold falls out arithmetically.
        const uint32_t next = 3 - _writeSlot - _previousSlot;
        _previousSlot       = _writeSlot;
        _writeSlot          = next;
    }

    void LocalVoiceSink::SetMasterVolume(float volume) {
        _masterVolume.store(std::clamp(volume, 0.0f, 4.0f), std::memory_order_relaxed);
    }

    void LocalVoiceSink::Render(float *stereoOut, uint32_t frameCount, void *user) {
        static_cast<LocalVoiceSink *>(user)->RenderInto(stereoOut, frameCount);
    }

    void LocalVoiceSink::RenderInto(float *stereoOut, uint32_t frameCount) {
        const World &world = _world[_publishedSlot.load(std::memory_order_acquire)];

        int16_t scratch[kRenderChunkSamples];
        bool mixedAnything = false;

        for (size_t i = 0; i < _slots.size(); i++) {
            Slot &slot = _slots[i];

            const uint64_t id = slot.id.load(std::memory_order_acquire);
            if (id == 0) {
                // Draining here rather than on release keeps the ring single-consumer.
                while (slot.pcm.Pop(scratch, kRenderChunkSamples)) {
                }
                slot.primed = false;
                continue;
            }

            // Frames arrive in bursts every 50ms while this runs every 20ms, so playing on
            // the first frame leaves the buffer at zero and every hiccup punches a hole.
            if (!slot.primed) {
                if (slot.pcm.Available() < kJitterBufferFrames * kFrameSamples) {
                    continue;
                }

                slot.primed = true;
            }

            // Ran dry. Hold the remainder and re-prime: splicing silence mid-waveform is what
            // makes an underrun sound like distortion rather than a pause.
            if (slot.pcm.Available() < frameCount) {
                slot.primed = false;
                continue;
            }

            // Drifted deep. Skip the oldest audio back to the prime level so latency cannot
            // creep upwards. Requires a whole chunk of headroom, so it can never trim below
            // the target and starve the consume below.
            if (slot.pcm.Available() > kJitterBufferMaxFrames * kFrameSamples) {
                const size_t target = std::max<size_t>(kJitterBufferFrames * kFrameSamples, frameCount);
                while (slot.pcm.Available() >= target + kRenderChunkSamples) {
                    if (!slot.pcm.Pop(scratch, kRenderChunkSamples)) {
                        break;
                    }
                }
            }

            // A slot that changed hands since the last publish has no trustworthy position,
            // so its audio is consumed and dropped for one tick rather than mispanned.
            const bool positioned = world.id[i] == id;

            SpeakerGain gain;
            if (positioned) {
                gain = ComputeGain(world.listener, world.position[i], world.range[i]);
            }

            const bool audible = gain.left > 0.0f || gain.right > 0.0f;

            // Out-of-range speakers are consumed too, just not mixed: queued audio would pile
            // up and burst out the moment they came back into earshot.
            uint32_t remaining = frameCount;
            uint32_t offset    = 0;

            while (remaining > 0) {
                const uint32_t chunk = std::min(remaining, kRenderChunkSamples);
                if (!slot.pcm.Pop(scratch, chunk)) {
                    break;
                }

                if (audible) {
                    MixFrameInto(stereoOut + static_cast<size_t>(offset) * 2, scratch, chunk, gain);
                    mixedAnything = true;
                }

                offset += chunk;
                remaining -= chunk;
            }
        }

        if (!mixedAnything) {
            return;
        }

        LimitStereoBuffer(stereoOut, frameCount, _masterVolume.load(std::memory_order_relaxed));
    }

    // ------------------------------------------------------------------------------------
    // VoiceClient
    // ------------------------------------------------------------------------------------

    VoiceClient::~VoiceClient() {
        Shutdown();
    }

    bool VoiceClient::Init(Networking::NetworkClient *client) {
        if (client == nullptr || client->GetPeer() == nullptr) {
            return false;
        }

        _client = client;

        client->GetPeer()->AttachPlugin(&_voice);
        _attached = true;

        _sink = &_localSink;
        if (!_localSink.Start()) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: playback unavailable; remote players will be inaudible");
        }

        if (!_capture.Start()) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: no microphone; push-to-talk will do nothing");
        }

        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Voice client ready (microphone: {}, playback: {})", _capture.IsRunning(), _localSink.IsRunning());
        return true;
    }

    void VoiceClient::Shutdown() {
        CloseSession();

        _capture.Stop();
        _localSink.Stop();

        if (_attached && _client != nullptr && _client->GetPeer() != nullptr) {
            _client->GetPeer()->DetachPlugin(&_voice);
        }

        _attached = false;
        _client   = nullptr;
        _sink     = nullptr;
        _placements.clear();
        _published.clear();
    }

    void VoiceClient::OpenSession() {
        MafiaNet::RakPeerInterface *peer = _client->GetPeer();

        // A client is joined to exactly one server, so index 0 is it. Unassigned means the
        // connection has not settled; retry next tick rather than key the session on nothing.
        const MafiaNet::RakNetGUID server = peer->GetGUIDFromIndex(0);
        if (server == MafiaNet::UNASSIGNED_RAKNET_GUID) {
            return;
        }

        _self = peer->GetMyGUID();
        if (_self == MafiaNet::UNASSIGNED_RAKNET_GUID) {
            return;
        }

        _voice.SetRelayMode(true);
        // One decoded stream per speaker; the mixer needs them separate to position each.
        _voice.SetPerSpeakerOutput(true);
        _voice.Init(kSampleRate, kFrameBytes);

        // Opus picks its own rate from the sample rate unless told otherwise.
        _voice.SetEncoderBitrate(kBitrate);
        _voice.SetVAD(true);
        _voice.SetVBR(true);

        // Only the codec can bound decode: relay frames are decoded in OnReceive, before any
        // of our code sees them, so the mixer's slot array bounds mixing rather than CPU.
        _voice.SetMaxDecodedSpeakers(kMaxDecodedTalkers);

        // No SetNoiseFilter: RNNoise needs 480-sample frames and voice runs at 960.
        _voice.SetRelayTarget(server);

        _sessionOpen = true;
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Voice session open (relay {}, self {})", server.g, _self.g);
    }

    void VoiceClient::CloseSession() {
        if (!_sessionOpen) {
            return;
        }

        for (size_t i = 0; i < _admitted.size(); i++) {
            ReleaseAdmitted(static_cast<int>(i));
        }

        _voice.Deinit();
        _voice.SetRelayMode(false);
        _voice.SetRelayTarget(MafiaNet::UNASSIGNED_RAKNET_GUID);

        // The same peer GUID may be a different player on the next server.
        _placements.clear();

        _self         = MafiaNet::UNASSIGNED_RAKNET_GUID;
        _sessionOpen  = false;
        _transmitting = false;

        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Voice session closed");
    }

    void VoiceClient::UpdateSession() {
        const bool connected = _client->GetConnectionState() == Networking::PeerState::CONNECTED;

        if (connected && !_sessionOpen) {
            OpenSession();
        }
        else if (!connected && _sessionOpen) {
            CloseSession();
        }
    }

    void VoiceClient::Update() {
        if (_client == nullptr) {
            return;
        }

        UpdateSession();
        PumpCapture();
        PumpSpeakers();
        PublishWorld();
    }

    void VoiceClient::PumpCapture() {
        if (!_capture.IsRunning()) {
            return;
        }

        const bool transmit = _sessionOpen && _pushToTalkHeld && !_inputSuppressed;

        // Drained whether or not we transmit: left alone the ring fills, and the next
        // push-to-talk press would send all of it before anything the player just said.
        uint32_t frames = 0;
        while (_capture.ReadFrame(_frame.data())) {
            if (transmit) {
                _voice.SendFrame(_self, _frame.data());
            }
            frames++;
        }

        _transmitting = transmit && frames > 0;
    }

    void VoiceClient::PumpSpeakers() {
        if (!_sessionOpen) {
            return;
        }

        const int64_t nowMs = Utils::Time::GetTime();

        _voice.GetActiveSpeakers(_activeSpeakers);

        for (unsigned i = 0; i < _activeSpeakers.Size(); i++) {
            const MafiaNet::RakNetGUID guid = _activeSpeakers[i];
            const uint64_t speaker          = static_cast<uint64_t>(MafiaNet::ToPeerGuid(guid));

            int slot = FindAdmitted(speaker);
            if (slot < 0) {
                slot = AdmitSpeaker(speaker, nowMs);
            }

            // Drained admitted or not: the decode is already paid for, and leaving frames
            // queued only delays the audio handed back once a slot opens up.
            bool received = false;
            while (_voice.ReceiveFrameFrom(guid, _frame.data())) {
                if (slot >= 0 && _sink != nullptr) {
                    _sink->Submit(speaker, _frame.data(), kFrameSamples);
                }
                received = true;
            }

            if (received && slot >= 0) {
                _admitted[slot].lastFrame = nowMs;
            }
        }

        for (size_t i = 0; i < _admitted.size(); i++) {
            if (_admitted[i].id == 0) {
                continue;
            }

            if ((nowMs - _admitted[i].lastFrame) > static_cast<int64_t>(kSpeakerSilenceTimeoutMs)) {
                ReleaseAdmitted(static_cast<int>(i));
            }
        }
    }

    void VoiceClient::PublishWorld() {
        _published.clear();

        for (const AdmittedSpeaker &admitted : _admitted) {
            if (admitted.id == 0) {
                continue;
            }

            const auto it = _placements.find(admitted.id);
            if (it == _placements.end()) {
                continue;
            }

            _published.push_back(it->second.placement);
        }

        // Without a listener every gain is computed against the origin, so a mod that forgot
        // to publish one would debug silence.
        if (!_listenerSet && !_listenerWarned && !_published.empty()) {
            _listenerWarned = true;
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: no listener transform published; call VoiceClient::SetListenerTransform from the game camera");
        }

        _localSink.PublishWorld(_listener, _published.data(), _published.size());
    }

    int VoiceClient::FindAdmitted(uint64_t speaker) const {
        for (size_t i = 0; i < _admitted.size(); i++) {
            if (_admitted[i].id == speaker) {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    float VoiceClient::DistanceSqTo(uint64_t speaker) const {
        const auto it = _placements.find(speaker);
        if (it == _placements.end()) {
            return std::numeric_limits<float>::infinity();
        }

        const glm::vec3 delta = it->second.placement.position - _listener.position;
        return glm::dot(delta, delta);
    }

    int VoiceClient::AdmitSpeaker(uint64_t speaker, int64_t nowMs) {
        for (size_t i = 0; i < _admitted.size(); i++) {
            if (_admitted[i].id == 0) {
                _admitted[i].id        = speaker;
                _admitted[i].lastFrame = nowMs;
                return static_cast<int>(i);
            }
        }

        // Unplaceable speakers sort as infinitely far, so they are evicted first and never
        // displace one the player can actually see.
        const float candidateDistSq = DistanceSqTo(speaker);

        int farthest         = -1;
        float farthestDistSq = 0.0f;

        for (size_t i = 0; i < _admitted.size(); i++) {
            const float distSq = DistanceSqTo(_admitted[i].id);
            if (farthest < 0 || distSq > farthestDistSq) {
                farthest       = static_cast<int>(i);
                farthestDistSq = distSq;
            }
        }

        if (farthest < 0 || !(candidateDistSq * kEvictionHysteresis < farthestDistSq)) {
            return -1;
        }

        ReleaseAdmitted(farthest);
        _admitted[farthest].id        = speaker;
        _admitted[farthest].lastFrame = nowMs;
        return farthest;
    }

    void VoiceClient::ReleaseAdmitted(int slot) {
        if (_admitted[slot].id == 0) {
            return;
        }

        if (_sink != nullptr) {
            _sink->ReleaseSpeaker(_admitted[slot].id);
        }

        _admitted[slot].id        = 0;
        _admitted[slot].lastFrame = 0;
    }

    void VoiceClient::BeginSpeakerUpdate() {
        _placementGeneration++;
    }

    void VoiceClient::EndSpeakerUpdate() {
        for (auto it = _placements.begin(); it != _placements.end();) {
            if (it->second.generation == _placementGeneration) {
                ++it;
                continue;
            }

            // No longer replicated: drop the slot too, or it holds a decoder open for
            // someone who can no longer be heard.
            const int slot = FindAdmitted(it->first);
            if (slot >= 0) {
                ReleaseAdmitted(slot);
            }

            it = _placements.erase(it);
        }
    }

    bool VoiceClient::IsSelf(uint64_t speaker) const {
        return _sessionOpen && speaker == static_cast<uint64_t>(MafiaNet::ToPeerGuid(_self));
    }

    void VoiceClient::SetSpeakerPosition(uint64_t speaker, const glm::vec3 &position) {
        if (speaker == 0 || IsSelf(speaker)) {
            return;
        }

        PlacementEntry &entry    = _placements[speaker];
        entry.placement.speaker  = speaker;
        entry.placement.position = position;
        entry.generation         = _placementGeneration;
    }

    void VoiceClient::SetSpeakerRange(uint64_t speaker, float range) {
        if (speaker == 0 || IsSelf(speaker)) {
            return;
        }

        PlacementEntry &entry   = _placements[speaker];
        entry.placement.speaker = speaker;
        entry.placement.range   = range > 0.0f ? range : kDefaultProximityRange;
        entry.generation        = _placementGeneration;
    }

    void VoiceClient::RemoveSpeaker(uint64_t speaker) {
        _placements.erase(speaker);

        const int slot = FindAdmitted(speaker);
        if (slot >= 0) {
            ReleaseAdmitted(slot);
        }
    }

    void VoiceClient::SetSink(IVoiceSink *sink) {
        IVoiceSink *next = sink != nullptr ? sink : static_cast<IVoiceSink *>(&_localSink);
        if (next == _sink) {
            return;
        }

        // Hand admitted speakers back before switching, so an outgoing engine sink is never
        // left holding voices it will not be told to release.
        for (size_t i = 0; i < _admitted.size(); i++) {
            ReleaseAdmitted(static_cast<int>(i));
        }

        _sink = next;

        if (next == &_localSink) {
            _localSink.Start();
        }
        else {
            // Only one renderer at a time.
            _localSink.Stop();
        }
    }
} // namespace Framework::Voice
