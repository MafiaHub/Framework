/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice_server.h"

#include "voice/voice_config.h"

#include <logging/logger.h>
#include <networking/network_server.h>
#include <utils/time.h>

namespace Framework::Voice {
    bool VoiceServer::Init(Networking::NetworkServer *server) {
        if (server == nullptr || server->GetPeer() == nullptr) {
            return false;
        }

        _server = server;

        // Relay host only. SetRelayHost is mandatory: without it RakVoice consumes
        // ID_RAKVOICE_RELAY_DATA inside RakPeer::Receive and the packet never reaches the
        // application loop, so OnVoiceFrame would never fire and voice would fail silently.
        //
        // SetRelayMode is deliberately NOT set. It is the flag a talking/listening client
        // enables: it opens a self-keyed encoder channel and makes Update() walk the relay
        // reap branch. A host neither encodes nor decodes -- RelayFrame ignores relayMode
        // entirely -- so setting it would only put the plugin in a contradictory state.
        // No Init() call either, so no codec is ever allocated here.
        _voice.SetRelayHost(true);
        server->GetPeer()->AttachPlugin(&_voice);
        _attached = true;

        // Debug, not info: no client on any platform can produce or consume a frame until the
        // client half lands, so announcing this on every server would only mislead operators.
        Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Voice relay attached");
        return true;
    }

    VoiceServer::~VoiceServer() {
        // RakPeer holds a raw PluginInterface2* and PluginInterface2's destructor does not
        // self-detach, so a VoiceServer destroyed while still attached would leave the peer
        // with a dangling pointer. This object is owned outside NetworkPeer, so its lifetime
        // is not tied to the peer's; detach here rather than depending on member declaration
        // order in whatever owns it.
        Shutdown();
    }

    void VoiceServer::Shutdown() {
        // Idempotent: the explicit shutdown path and the destructor both land here.
        if (_attached && _server != nullptr && _server->GetPeer() != nullptr) {
            _server->GetPeer()->DetachPlugin(&_voice);
        }
        _attached = false;
        _server   = nullptr;
        _cache.clear();
    }

    void VoiceServer::Update() {
        // Nothing periodic is required: cache entries expire lazily in RecipientsFor().
        // Kept as an explicit hook so M2's channel bookkeeping has somewhere to live.
    }

    void VoiceServer::OnPlayerDisconnect(uint64_t guid) {
        _router.RemovePlayer(guid);
        _cache.erase(guid);
    }

    const std::vector<MafiaNet::RakNetGUID> &VoiceServer::RecipientsFor(uint64_t talker) {
        auto &entry         = _cache[talker];
        const int64_t nowMs = Utils::Time::GetTime();

        if (entry.computedAtMs != 0 && (nowMs - entry.computedAtMs) < static_cast<int64_t>(kRecipientRefreshMs)) {
            return entry.guids;
        }

        _router.ComputeRecipients(talker, _scratch);

        entry.guids.clear();
        entry.guids.reserve(_scratch.size());
        for (const uint64_t guid : _scratch) {
            entry.guids.push_back(MafiaNet::ToGuid(static_cast<MafiaNet::PeerGuid>(guid)));
        }
        entry.computedAtMs = nowMs;

        return entry.guids;
    }

    void VoiceServer::OnVoiceFrame(MafiaNet::Packet *packet) {
        if (packet == nullptr) {
            return;
        }

        // A frame carrying nothing past the relay header is useless to every receiver, which
        // drops it on the same test. Rejecting it here denies an amplification primitive: the
        // fan-out below multiplies one inbound packet into one send per in-range listener, so
        // a client spamming header-only frames would cost the server that multiple in egress.
        if (packet->length <= MafiaNet::RAKVOICE_RELAY_HEADER_SIZE) {
            return;
        }

        // Upper bound for the same reason: RelayFrame forwards the payload verbatim once per
        // recipient, so an oversized "frame" from a modified client would be amplified across
        // the whole proximity set. No legitimate frame exceeds one maximum Opus packet.
        if (packet->length > MafiaNet::RAKVOICE_RELAY_HEADER_SIZE + MafiaNet::RAKVOICE_MAX_OPUS_PACKET_SIZE) {
            return;
        }

        const MafiaNet::RakNetGUID origin = MafiaNet::RakVoice::ReadRelayOrigin(packet);
        if (origin == MafiaNet::UNASSIGNED_RAKNET_GUID) {
            return;
        }

        // A client may only speak as itself. Without this check a modified client could
        // stamp someone else's GUID and impersonate them.
        if (origin != packet->guid) {
            return;
        }

        const auto &recipients = RecipientsFor(static_cast<uint64_t>(MafiaNet::ToPeerGuid(origin)));
        if (recipients.empty()) {
            return;
        }

        _voice.RelayFrame(packet, recipients.data(), static_cast<int>(recipients.size()));
    }
} // namespace Framework::Voice
