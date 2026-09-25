/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>

namespace Framework::Voice {
    // Opus operates natively at 48kHz; 20ms frames are the standard trade-off between
    // latency and per-packet header overhead. Mono, because voice is positioned by the
    // client mixer rather than carried as stereo.
    constexpr uint32_t kSampleRate   = 48000;
    constexpr uint32_t kChannels     = 1;
    constexpr uint32_t kFrameSamples = 960; // 20ms at 48kHz
    constexpr uint32_t kFrameBytes   = kFrameSamples * sizeof(int16_t);

    // Target Opus bitrate. Opus' reference for a mono stream is 64kbps; 24kbps was
    // intelligible but audibly band-limited. 40kbps costs 5KB/s per talker, and proximity
    // bounds how many talkers the server relays at once.
    constexpr uint32_t kBitrate = 40000;

    // Speaker slots the client-side mixer allocates. Not a server-side guard: the router
    // returns every listener in range, and proximity is the only fan-out bound.
    constexpr uint32_t kMaxAudibleTalkers = 6;

    // Decoders the client keeps, via RakVoice::SetMaxDecodedSpeakers. Larger than
    // kMaxAudibleTalkers on purpose: were they equal, the codec's recency-based choice would
    // decide who is audible and the mixer's distance-based selection would have nothing left
    // to choose between.
    constexpr uint32_t kMaxDecodedTalkers = kMaxAudibleTalkers * 2;

    // Recipient sets are recomputed on this interval rather than per frame; at 50 frames
    // per second per talker, per-frame recomputation would be 50x the work for a set that
    // changes slowly.
    constexpr uint32_t kRecipientRefreshMs = 250;

    // Starting proximity audibility radius, in world units. A server overrides it through
    // VoiceServer::SetProximityRange.
    constexpr float kDefaultProximityRange = 25.0f;

    // Default push-to-talk binding, as a Win32 virtual-key code ('V').
    constexpr int kDefaultPushToTalkKey = 0x56;

    // Transmission continues this long after push-to-talk is released, so letting go a touch
    // early does not clip the last syllable. A silent tail costs nothing: VAD drops it.
    constexpr uint32_t kDefaultPushToTalkReleaseMs = 100;
    constexpr uint32_t kMaxPushToTalkReleaseMs     = 2000;

    // A talker counts as stopped after this long without a frame. Shorter flickers mid-sentence:
    // VAD punches holes between words and RakVoice flushes on a 50ms throttle.
    constexpr uint32_t kTalkingTimeoutMs = 300;

    // How long a remote speaker still reads as talking (VoiceClient::IsSpeakerTalking) after
    // their audio stops arriving. Longer than kTalkingTimeoutMs on purpose: VAD sends nothing
    // at all during silence -- RakVoice drops Opus's DTX frames -- so every pause between words
    // is a gap on the receiving side, and an indicator that followed the arrivals alone would
    // blink through a sentence. Long enough to bridge a pause, short enough that the icon still
    // goes when the sentence does.
    constexpr uint32_t kSpeakerTalkingHoldMs = 700;

    // A speaker's decoder is released after this long without a frame.
    constexpr uint32_t kSpeakerSilenceTimeoutMs = 2000;

    // Jitter buffer depth before a speaker starts playing, in 20ms frames. RakVoice flushes
    // on a 50ms throttle, so frames arrive in bursts of two or three while the device
    // consumes one every 20ms; playing immediately leaves the buffer riding empty and every
    // hiccup punches a hole mid-word. Costs up to 60ms before a speaker is heard.
    constexpr uint32_t kJitterBufferFrames = 3;

    // Ceiling on buffered audio per speaker, in 20ms frames. Late-packet bursts and
    // capture/playback clock drift only ever add depth, so without a ceiling voice falls
    // steadily further behind. Past this, the oldest audio is skipped back to
    // kJitterBufferFrames: one audible skip in exchange for bounded latency.
    constexpr uint32_t kJitterBufferMaxFrames = 12;

    // Bounds on the adaptive start depth, in 20ms frames. kJitterBufferFrames is where a
    // speaker starts before anything has been measured; from there the depth follows the
    // gaps between their arrivals. Two frames is the floor RakVoice's 50ms flush allows,
    // ten (200ms) is where a conversation starts to feel like a phone call.
    constexpr uint32_t kJitterBufferMinFrames       = 2;
    constexpr uint32_t kJitterBufferMaxTargetFrames = 10;

    // Arrival gaps longer than this are the talker pausing, not the network: VAD sends
    // nothing between words, so a gap of this size carries no information about jitter.
    constexpr uint32_t kJitterTalkspurtGapMs = 250;

    // How many recent arrival gaps the adaptive depth is taken from. At RakVoice's 50ms
    // flush this is the last ~2.5 seconds of speech.
    constexpr uint32_t kJitterWindowSamples = 48;

    // Transmit without a key: the microphone opens once its level crosses a threshold and
    // stays open for a hold after it falls back, so the gaps between words do not chop.
    // Levels are full-scale RMS in [0, 1], the same scale GetSpeakerLevel reports.
    constexpr float kDefaultVoiceActivationThreshold = 0.03f; // about -30 dBFS
    constexpr uint32_t kDefaultVoiceActivationHoldMs = 300;
    constexpr uint32_t kMaxVoiceActivationHoldMs     = 2000;

    // Frames kept from before the gate opens and sent with the first open frame, so the
    // onset of the word that tripped the threshold is not the part that gets lost.
    constexpr uint32_t kVoiceActivationPreRollFrames = 2;
} // namespace Framework::Voice
