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
#include <mafianet/MessageIdentifiers.h>
#include <networking/channels.h>
#include <networking/network_server.h>
#include <networking/replication/network_entity.h>
#include <networking/replication/replication_manager.h>
#include <networking/rpc/voice_settings.h>
#include <utils/time.h>

#include <iterator>

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
        _voice.SetOrderingChannels(Framework::Networking::ToOrderingChannel(Framework::Networking::Channel::VoiceFrames), Framework::Networking::ToOrderingChannel(Framework::Networking::Channel::VoiceControl));
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
        _talking.clear();
        _talkingChanges.clear();
        _tierAppliedAtMs.clear();
        _pendingTiers.clear();
        _tierChanges.clear();
    }

    void VoiceServer::Update() {
        // Recipient cache entries expire lazily in RecipientsFor(); the periodic work is the tier
        // switches that were waiting on the floor, and retiring talkers who went quiet, since
        // silence produces no packet to notice.
        if (_talking.empty() && _pendingTiers.empty()) {
            return;
        }

        const int64_t nowMs = Utils::Time::GetTime();
        for (auto it = _pendingTiers.begin(); it != _pendingTiers.end();) {
            it = ApplyTierRequest(it->first, it->second, nowMs) ? _pendingTiers.erase(it) : std::next(it);
        }

        for (auto it = _talking.begin(); it != _talking.end();) {
            if ((nowMs - it->second) <= static_cast<int64_t>(kTalkingTimeoutMs)) {
                ++it;
                continue;
            }

            const uint64_t guid = it->first;
            it                  = _talking.erase(it);
            _talkingChanges.push_back({guid, false});
        }
    }

    void VoiceServer::SyncAvatars(const Networking::Replication::ReplicationManager &replication) {
        // Avatars, not every owned entity: a delegated NPC or ridden horse carries the player's
        // guid too, and taking it would move their voice to wherever that entity stands.
        bool audibilityChanged = false;
        _router.BeginAvatarPass();
        replication.ForEachAvatar([this, &audibilityChanged](MafiaNet::PeerGuid peer, Networking::Replication::NetworkEntity *avatar) {
            audibilityChanged |= _router.SetPlayerAvatar(static_cast<uint64_t>(peer), avatar->position, avatar->GetVirtualWorld());
        });
        audibilityChanged |= _router.EndAvatarPass();

        // Moves ride the refresh interval; a world change, or an avatar appearing or going, cuts
        // or restores audio now, in both directions.
        if (audibilityChanged) {
            InvalidateRecipients();
        }
    }

    void VoiceServer::MarkTalking(uint64_t talker, int64_t nowMs) {
        const auto [it, inserted] = _talking.emplace(talker, nowMs);
        it->second                = nowMs;
        if (inserted) {
            _talkingChanges.push_back({talker, true});
        }
    }

    bool VoiceServer::IsPlayerTalking(uint64_t guid) const {
        return _talking.find(guid) != _talking.end();
    }

    void VoiceServer::DrainTalkingChanges(std::vector<TalkingChange> &out) {
        out.clear();
        out.swap(_talkingChanges);
    }

    void VoiceServer::OnPlayerDisconnect(uint64_t guid) {
        _router.RemovePlayer(guid);
        _cache.erase(guid);
        // No stop edge: the avatar is torn down in ReplicationManager::OnClosedConnection,
        // which RakNet fires before this runs, so the event could not name the player.
        _talking.erase(guid);
        _tierAppliedAtMs.erase(guid);
        _pendingTiers.erase(guid);
        InvalidateRecipients();
    }

    void VoiceServer::InvalidateRecipients() {
        for (auto &entry : _cache) {
            entry.second.computedAtMs = 0;
        }
    }

    Networking::RPC::VoiceSettings VoiceServer::BuildSettings() const {
        Networking::RPC::VoiceSettings payload;
        payload.proximityRange = _router.GetDefaultRange();
        payload.tierRanges     = _router.GetTierRanges();
        return payload;
    }

    Networking::RPC::VoiceSpeakerRange VoiceServer::BuildPlayerRange(uint64_t guid) const {
        Networking::RPC::VoiceSpeakerRange payload;
        payload.player = guid;
        payload.range  = _router.GetPlayerRange(guid);
        payload.tier   = static_cast<uint8_t>(_router.GetPlayerTier(guid));
        return payload;
    }

    void VoiceServer::SetProximityRange(float meters) {
        _router.SetDefaultRange(meters);
        InvalidateRecipients();

        if (_server != nullptr) {
            Networking::RPC::VoiceSettings payload = BuildSettings();
            _server->BroadcastRPC(payload);
        }
    }

    void VoiceServer::SetTierRange(VoiceTier tier, float meters) {
        _router.SetTierRange(tier, meters);
        InvalidateRecipients();

        if (_server != nullptr) {
            Networking::RPC::VoiceSettings payload = BuildSettings();
            _server->BroadcastRPC(payload);
        }
    }

    void VoiceServer::SetPlayerRange(uint64_t guid, float meters) {
        _router.SetPlayerRange(guid, meters);
        InvalidateRecipients();

        // Broadcast, not sent to the talker: it is every listener's mixer that has to know
        // how far this voice carries.
        if (_server != nullptr) {
            Networking::RPC::VoiceSpeakerRange payload = BuildPlayerRange(guid);
            _server->BroadcastRPC(payload);
        }
    }

    void VoiceServer::SetPlayerTier(uint64_t guid, VoiceTier tier) {
        if (tier >= VoiceTier::Count || _router.GetPlayerTier(guid) == tier) {
            return;
        }

        _router.SetPlayerTier(guid, tier);
        InvalidateRecipients();

        // The talker included, for their own indicator.
        if (_server != nullptr) {
            Networking::RPC::VoiceSpeakerRange payload = BuildPlayerRange(guid);
            _server->BroadcastRPC(payload);
        }
    }

    void VoiceServer::SendSettingsTo(MafiaNet::RakNetGUID guid) {
        if (_server == nullptr) {
            return;
        }

        Networking::RPC::VoiceSettings settings = BuildSettings();
        _server->SendRPC(settings, guid);

        for (const uint64_t player : _router.GetPlayersWithRangeRules()) {
            Networking::RPC::VoiceSpeakerRange payload = BuildPlayerRange(player);
            _server->SendRPC(payload, guid);
        }
    }

    void VoiceServer::OnTierRequest(uint64_t guid, uint8_t tier) {
        if (tier >= static_cast<uint8_t>(VoiceTier::Count)) {
            return;
        }

        const VoiceTier requested = static_cast<VoiceTier>(tier);
        if (ApplyTierRequest(guid, requested, Utils::Time::GetTime())) {
            _pendingTiers.erase(guid);
        }
        else {
            _pendingTiers[guid] = requested;
        }
    }

    bool VoiceServer::ApplyTierRequest(uint64_t guid, VoiceTier tier, int64_t nowMs) {
        // A request for the tier already held changes nothing anyone needs telling, and is what
        // every client sends on connect.
        if (_router.GetPlayerTier(guid) == tier) {
            return true;
        }

        // Every applied switch is a broadcast to the whole server, so a client that ignores its
        // own pacing is paced here instead.
        const auto it = _tierAppliedAtMs.find(guid);
        if (it != _tierAppliedAtMs.end() && (nowMs - it->second) < static_cast<int64_t>(kTierChangeFloorMs)) {
            return false;
        }

        _tierAppliedAtMs[guid] = nowMs;
        SetPlayerTier(guid, tier);
        _tierChanges.push_back({guid, tier});
        return true;
    }

    void VoiceServer::DrainTierChanges(std::vector<TierChange> &out) {
        out.clear();
        out.swap(_tierChanges);
    }

    void VoiceServer::OnPlayerPreference(uint64_t guid, bool enabled) {
        if (_router.IsPlayerVoiceDisabled(guid) == !enabled) {
            return;
        }

        _router.SetPlayerVoiceDisabled(guid, !enabled);
        InvalidateRecipients();
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
        if (packet == nullptr || packet->length == 0) {
            return;
        }

        // The relay format is defined from byte 0: MafiaNet writes the id there and both of
        // its readers use fixed offsets from it, so a timestamp-prefixed relay frame cannot
        // be parsed at all. The dispatcher upstream identifies packets through
        // GetPacketDataOffset(), which skips an ID_TIMESTAMP prefix, so the two would
        // disagree if such a frame ever arrived. Assert the invariant here rather than
        // threading an offset through a format that has nowhere to put it -- MafiaNet's
        // RelayFrame rejects the same mismatch, so today this is belt and braces.
        if (packet->data[0] != ID_RAKVOICE_RELAY_DATA) {
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

        const uint64_t talker = static_cast<uint64_t>(MafiaNet::ToPeerGuid(origin));

        // Above the recipient check: a talker with nobody in earshot is still talking.
        MarkTalking(talker, Utils::Time::GetTime());

        const auto &recipients = RecipientsFor(talker);
        if (recipients.empty()) {
            return;
        }

        _voice.RelayFrame(packet, recipients.data(), static_cast<int>(recipients.size()));
    }
} // namespace Framework::Voice
