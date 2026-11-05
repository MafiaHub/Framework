/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice_capture.h"

#include "voice/voice_config.h"

#include <mafianet/voice.h>

#include <algorithm>

namespace Framework::Voice {
    bool VoiceCapture::Start(uint64_t guid, uint64_t ticket, uint32_t maxMs, int64_t nowMs) {
        const auto [it, inserted] = _captures.try_emplace(guid);
        if (!inserted) {
            return false;
        }

        maxMs                 = std::min(maxMs, kMaxCaptureMs);
        it->second.ticket     = ticket;
        it->second.deadlineMs = nowMs + maxMs;
        it->second.maxPackets = (maxMs + kFrameMs - 1) / kFrameMs;
        // Up front, so under the relay lock Tap allocates only the packet's own copy.
        it->second.packets.reserve(it->second.maxPackets);
        return true;
    }

    void VoiceCapture::Tap(uint64_t guid, int64_t nowMs, const uint8_t *data, size_t size) {
        // The relay takes up to a whole Opus buffer; only a modified client sends more than a frame.
        if (_captures.empty() || size > MafiaNet::RAKVOICE_MAX_FRAME_PAYLOAD) {
            return;
        }

        // Past the deadline it only waits for the tick that ends it.
        const auto it = _captures.find(guid);
        if (it == _captures.end() || nowMs >= it->second.deadlineMs || it->second.packets.size() >= it->second.maxPackets) {
            return;
        }

        it->second.packets.emplace_back(data, data + size);
    }

    void VoiceCapture::Checkpoint() {
        for (auto &[guid, capture] : _captures) {
            capture.heard = capture.packets.size();
        }
    }

    void VoiceCapture::OnTalkingStopped(uint64_t guid, std::vector<CaptureResult> &out) {
        const auto it = _captures.find(guid);
        if (it == _captures.end() || it->second.heard == 0) {
            return;
        }

        // Anything past the checkpoint arrived after the stop was decided: the next utterance.
        it->second.packets.resize(it->second.heard);
        End(it, out);
    }

    void VoiceCapture::Expire(int64_t nowMs, std::vector<CaptureResult> &out) {
        for (auto it = _captures.begin(); it != _captures.end();) {
            const Capture &capture = it->second;
            it                     = nowMs >= capture.deadlineMs || capture.packets.size() >= capture.maxPackets ? End(it, out) : std::next(it);
        }
    }

    void VoiceCapture::Finish(uint64_t guid, std::vector<CaptureResult> &out) {
        const auto it = _captures.find(guid);
        if (it != _captures.end()) {
            End(it, out);
        }
    }

    void VoiceCapture::Cancel(uint64_t ticket) {
        std::erase_if(_captures, [ticket](const auto &entry) {
            return entry.second.ticket == ticket;
        });
    }

    void VoiceCapture::Clear() {
        _captures.clear();
    }

    bool VoiceCapture::IsCapturing(uint64_t guid) const {
        return _captures.contains(guid);
    }

    VoiceCapture::Captures::iterator VoiceCapture::End(Captures::iterator it, std::vector<CaptureResult> &out) {
        out.push_back({it->second.ticket, std::move(it->second.packets)});
        return _captures.erase(it);
    }
} // namespace Framework::Voice
