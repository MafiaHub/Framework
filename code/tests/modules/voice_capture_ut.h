/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/server/voice_capture.h"
#include "voice/server/voice_server.h"
#include "voice/voice_config.h"

#include <mafianet/message_identifiers.h>
#include <mafianet/types.h>
#include <mafianet/voice.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

MODULE(voice_capture, {
    using namespace Framework::Voice;

    const std::vector<uint8_t> first  = {0x11, 0x12};
    const std::vector<uint8_t> second = {0x21};

    const auto tap = [](VoiceCapture &capture, uint64_t guid, const std::vector<uint8_t> &packet, int64_t nowMs = 0) {
        capture.Tap(guid, nowMs, packet.data(), packet.size());
    };

    IT("holds the packets one player sends, and nobody else's", {
        VoiceCapture capture;
        EQUALS(capture.Start(1, 7, 4000, 0), true);
        tap(capture, 1, first);
        tap(capture, 2, second);
        capture.Checkpoint();

        std::vector<CaptureResult> out;
        capture.OnTalkingStopped(1, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].ticket, static_cast<uint64_t>(7));
        EQUALS(out[0].packets.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets[0] == first, true);
        EQUALS(capture.IsCapturing(1), false);
    });

    IT("refuses a second capture of a player until the first ends", {
        VoiceCapture capture;
        EQUALS(capture.Start(1, 1, 4000, 0), true);
        EQUALS(capture.Start(1, 2, 4000, 0), false);
        EQUALS(capture.Start(2, 3, 4000, 0), true);

        std::vector<CaptureResult> out;
        capture.Finish(1, out);
        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].ticket, static_cast<uint64_t>(1));
        EQUALS(capture.Start(1, 4, 4000, 0), true);
    });

    // A capture started in the quiet tail of an utterance sees that utterance's stop with nothing
    // heard; the password is in the next one.
    IT("waits through a stop that ends an utterance from before the call", {
        VoiceCapture capture;
        capture.Start(1, 1, 4000, 0);
        capture.Checkpoint();

        std::vector<CaptureResult> out;
        capture.OnTalkingStopped(1, out);
        EQUALS(out.size(), static_cast<size_t>(0));
        EQUALS(capture.IsCapturing(1), true);

        tap(capture, 1, first);
        capture.Checkpoint();
        capture.OnTalkingStopped(1, out);
        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets.size(), static_cast<size_t>(1));
    });

    IT("ends on a stop with the packets heard before it, not the next utterance's", {
        VoiceCapture capture;
        capture.Start(1, 1, 4000, 0);
        tap(capture, 1, first);
        capture.Checkpoint();
        tap(capture, 1, second);

        std::vector<CaptureResult> out;
        capture.OnTalkingStopped(1, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets[0] == first, true);
    });

    IT("settles empty at the deadline when nothing was sent", {
        VoiceCapture capture;
        capture.Start(1, 1, 4000, 1000);

        std::vector<CaptureResult> out;
        capture.Expire(4999, out);
        EQUALS(out.size(), static_cast<size_t>(0));

        capture.Expire(5000, out);
        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets.empty(), true);
    });

    // The tick that ends it can come late; what was said after the deadline is not part of it.
    IT("drops packets that arrive at or after its deadline", {
        VoiceCapture capture;
        capture.Start(1, 1, 4000, 1000);
        tap(capture, 1, first, 4999);
        tap(capture, 1, second, 5000);

        std::vector<CaptureResult> out;
        capture.Expire(6000, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets[0] == first, true);
    });

    IT("settles a capture with no time to run empty, at the next expiry", {
        VoiceCapture capture;
        capture.Start(1, 1, 0, 0);
        tap(capture, 1, first);

        std::vector<CaptureResult> out;
        capture.Expire(0, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets.empty(), true);
    });

    IT("settles once when a stop and its deadline land in the same tick", {
        VoiceCapture capture;
        capture.Start(1, 1, 4000, 0);
        tap(capture, 1, first);
        capture.Checkpoint();

        std::vector<CaptureResult> out;
        capture.OnTalkingStopped(1, out);
        capture.Expire(4000, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets.size(), static_cast<size_t>(1));
    });

    IT("drops a packet larger than one frame can encode", {
        VoiceCapture capture;
        capture.Start(1, 1, 4000, 0);
        const std::vector<uint8_t> largest(MafiaNet::RAKVOICE_MAX_FRAME_PAYLOAD, 0x31);
        const std::vector<uint8_t> oversized(MafiaNet::RAKVOICE_MAX_FRAME_PAYLOAD + 1, 0x32);
        tap(capture, 1, largest);
        tap(capture, 1, oversized);

        std::vector<CaptureResult> out;
        capture.Finish(1, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets[0] == largest, true);
    });

    IT("clamps the duration to kMaxCaptureMs", {
        VoiceCapture capture;
        capture.Start(1, 1, 60000, 0);

        std::vector<CaptureResult> out;
        capture.Expire(static_cast<int64_t>(kMaxCaptureMs) - 1, out);
        EQUALS(out.size(), static_cast<size_t>(0));

        capture.Expire(static_cast<int64_t>(kMaxCaptureMs), out);
        EQUALS(out.size(), static_cast<size_t>(1));
    });

    // The duration is wall time; a client sending faster than one frame per 20 ms must not be
    // able to grow a capture past what that duration holds.
    IT("takes at most a packet per frame of its duration, then ends", {
        VoiceCapture capture;
        capture.Start(1, 1, 5 * kFrameMs, 0);
        for (int i = 0; i < 8; ++i) {
            tap(capture, 1, first);
        }

        std::vector<CaptureResult> out;
        capture.Expire(1, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets.size(), static_cast<size_t>(5));
    });

    IT("ends with everything it holds when the player leaves", {
        VoiceCapture capture;
        capture.Start(1, 1, 4000, 0);
        tap(capture, 1, first);
        tap(capture, 1, second);

        std::vector<CaptureResult> out;
        capture.Finish(1, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].packets.size(), static_cast<size_t>(2));
        EQUALS(capture.Empty(), true);
    });

    IT("drops a cancelled capture without a result", {
        VoiceCapture capture;
        capture.Start(1, 7, 4000, 0);
        capture.Cancel(7);
        tap(capture, 1, first);

        std::vector<CaptureResult> out;
        capture.Expire(10000, out);
        capture.Finish(1, out);

        EQUALS(out.size(), static_cast<size_t>(0));
        EQUALS(capture.Empty(), true);
    });

    // Through the relay, as a client's RakVoice frames arrive on the network thread.
    const MafiaNet::PeerGuid alice = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(0xA11CE));
    const MafiaNet::PeerGuid bob   = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(0xB0B));
    const auto relay               = [](VoiceServer &voice, MafiaNet::PeerGuid origin, MafiaNet::PeerGuid sender, const std::vector<uint8_t> &payload) {
        std::vector<unsigned char> data(MafiaNet::RAKVOICE_RELAY_HEADER_SIZE, 0);
        data[0]                                       = ID_RAKVOICE_RELAY_DATA;
        data[MafiaNet::RAKVOICE_RELAY_OFFSET_VERSION] = MafiaNet::RAKVOICE_RELAY_FORMAT_VERSION;
        const uint64_t raw                            = static_cast<uint64_t>(origin);
        std::memcpy(data.data() + MafiaNet::RAKVOICE_RELAY_OFFSET_ORIGIN, &raw, sizeof(raw));
        data.insert(data.end(), payload.begin(), payload.end());

        MafiaNet::Packet packet {};
        packet.data   = data.data();
        packet.length = static_cast<unsigned int>(data.size());
        packet.guid   = MafiaNet::ToGuid(sender);
        voice.OnVoiceFrame(&packet);
    };

    IT("hands over the payloads a player relays once they stop talking", {
        VoiceServer voice;
        const uint64_t ticket = voice.StartCapture(static_cast<uint64_t>(alice), 4000);
        const uint64_t again  = voice.StartCapture(static_cast<uint64_t>(alice), 4000);

        relay(voice, alice, alice, first);
        relay(voice, alice, bob, second); // someone else speaking as alice
        relay(voice, alice, alice, second);
        voice.Update();
        std::vector<CaptureResult> whileTalking;
        voice.DrainCaptures(whileTalking);

        std::this_thread::sleep_for(std::chrono::milliseconds(kTalkingTimeoutMs + 50));
        voice.Update();
        std::vector<CaptureResult> out;
        voice.DrainCaptures(out);

        NEQUALS(ticket, static_cast<uint64_t>(0));
        EQUALS(again, static_cast<uint64_t>(0));
        EQUALS(whileTalking.size(), static_cast<size_t>(0));
        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].ticket, ticket);
        EQUALS(out[0].packets.size(), static_cast<size_t>(2));
        EQUALS(out[0].packets[0] == first, true);
        EQUALS(out[0].packets[1] == second, true);
    });

    IT("settles a player's capture when they disconnect", {
        VoiceServer voice;
        const uint64_t ticket = voice.StartCapture(static_cast<uint64_t>(alice), 4000);
        relay(voice, alice, alice, first);
        voice.OnPlayerDisconnect(static_cast<uint64_t>(alice));

        std::vector<CaptureResult> out;
        voice.DrainCaptures(out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0].ticket, ticket);
        EQUALS(out[0].packets.size(), static_cast<size_t>(1));
        NEQUALS(voice.StartCapture(static_cast<uint64_t>(alice), 4000), static_cast<uint64_t>(0));
    });
});
