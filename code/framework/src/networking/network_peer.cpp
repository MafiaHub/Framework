/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "network_peer.h"

#include "errors.h"
#include "replication/replication_manager.h"
#include "replication/world.h"

#include <logging/logger.h>
#include <utils/profiler.h>

namespace Framework::Networking {
    NetworkPeer::NetworkPeer() {
        _peer = MafiaNet::RakPeerInterface::GetInstance();

        // RPC4 can be attached before Startup(); the ReplicationManager is attached by the concrete
        // peer's Init() once its connection factory exists.
        _peer->AttachPlugin(&_rpc);
        _peer->AttachPlugin(&_twoWayAuth);
        _peer->AttachPlugin(&_readyEvent);
        // A peer stalled past MafiaNet's default handshake window (a client loading, a server under a
        // join burst) would otherwise be dropped with a build verification timeout. Both ends need it.
        _twoWayAuth.SetTimeout(kBuildVerificationTimeoutMs);

        _replicationManager = std::make_unique<Replication::ReplicationManager>();
    }

    NetworkPeer::~NetworkPeer() = default;

    void NetworkPeer::SetBuildToken(const std::string &token) {
        _buildToken = token;
        if (!RegisterBuildToken()) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->error("Failed to register networking build token");
        }
    }

    void NetworkPeer::SetIdentifierSalt(std::uint64_t salt) {
        if (salt == _identifierSalt) {
            return;
        }
        // Slots registered under the old salt would otherwise keep answering to keys no peer sends.
        for (const auto &registered : _rpcHandlers) {
            _rpc.UnregisterSlot(ResolveIdentifier(registered.identifier));
        }
        _identifierSalt = salt;
        {
            const std::lock_guard guard(_identifierTokensMutex);
            _identifierTokens.clear();
        }
        for (const auto &registered : _rpcHandlers) {
            _rpc.RegisterSlot(ResolveIdentifier(registered.identifier), &NetworkPeer::DispatchRPC, registered.slot.get(), 0);
        }
    }

    const char *NetworkPeer::ResolveIdentifier(RPC::Identifier identifier) {
        const std::lock_guard guard(_identifierTokensMutex);
        const auto it = _identifierTokens.find(identifier.digest);
        if (it != _identifierTokens.end()) {
            return it->second.c_str();
        }
        return _identifierTokens.emplace(identifier.digest, RPC::TokenFor(_identifierSalt, identifier)).first->second.c_str();
    }

    bool NetworkPeer::RegisterBuildToken() {
        if (_buildToken.empty()) {
            return false;
        }
        // Re-registering the same token must be a no-op: the Clear() fallback below wipes the
        // plugin's in-flight challenge state, which would fail peers mid-handshake for nothing.
        if (_buildToken == _registeredToken) {
            return true;
        }

        const MafiaNet::RakString token(_buildToken.c_str());
        if (!_twoWayAuth.AddPassword(kBuildChallengeId, token)) {
            // TwoWayAuthentication cannot overwrite an identifier. A genuine token change pays the
            // Clear() — unavoidable, since the old token must stop validating.
            _twoWayAuth.Clear();
            if (!_twoWayAuth.AddPassword(kBuildChallengeId, token)) {
                return false;
            }
        }
        _registeredToken = _buildToken;
        return true;
    }

    void NetworkPeer::Update() {
        if (!_peer) {
            return;
        }

        // Rebuild the spatial index before ReplicaManager3 computes per-connection relevance, then
        // send this tick's state-bag changes — the flush asks each connection what it has
        // constructed, so it reads the relevance this rebuild just settled.
        _replicationManager->RebuildInterest();
        // After the rebuild and before the flush: the election reads the positions this tick
        // settled, and a handover it decides is an ownership change the flush then sees, so an
        // owner-scoped bag key travels to the peer that has just been given the entity rather
        // than to the one that had it a moment ago.
        _replicationManager->Delegation().Update();
        _replicationManager->FlushStateBags();

        // Receive() updates every plugin before each packet; the replication pass runs on the first
        // one only, ahead of this drain's packets, which the next pass then serializes.
        _replicationManager->BeginNetworkUpdate();
        DrainPackets();
        _replicationManager->EndNetworkUpdate();
    }

    void NetworkPeer::DrainPackets(const std::atomic<bool> *stop) {
        if (!_peer) {
            return;
        }
        FW_PROFILE_SCOPE_N("Networking::Receive");
        [[maybe_unused]] uint64_t packets = 0; // read by the profiler zone only
        while (stop == nullptr || !stop->load(std::memory_order_acquire)) {
            BeforeReceive();
            _packet = _peer->Receive();
            if (_packet == nullptr) {
                break;
            }
            ++packets;
            Dispatch();
            _peer->DeallocatePacket(_packet);
        }
        _packet = nullptr;
        // What the pass and the inbound state put off while they held an entity: a destruction or a
        // state change made from inside one of their callbacks.
        Replication::World::Settle();
        FW_PROFILE_VALUE(packets);
    }

    void NetworkPeer::Dispatch() {
        if (_packet->length == 0) {
            return;
        }
        const int offset = ResolvePacketDataOffset(_packet->data, _packet->length);
        if (offset < 0) {
            return;
        }
        _packetDataOffset      = offset;
        const uint8_t packetID = _packet->data[_packetDataOffset];
        if (_packetDataOffset != 0 && !IsAllowedBehindTimestamp(packetID)) {
            ++_droppedBehindTimestamp;
            const auto now = std::chrono::steady_clock::now();
            if (now >= _nextTimestampDropWarning) {
                Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->warn("Dropped {} control packet(s) sent behind a timestamp, last one {} from {}", _droppedBehindTimestamp, packetID, _packet->guid.g);
                _droppedBehindTimestamp   = 0;
                _nextTimestampDropWarning = now + std::chrono::seconds(1);
            }
            return;
        }

        if (!HandlePacket(packetID, _packet)) {
            if (IsReplicationPacket(packetID)) {
                return;
            }
            if (_onUnknownPacketCallback && _onUnknownPacketCallback(_packet)) {
                return;
            }
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->trace("Received unknown packet {}", packetID);
        }
    }

    int NetworkPeer::ResolvePacketDataOffset(const uint8_t *data, uint32_t length) {
        if (length == 0) {
            return -1;
        }
        if (data[0] != ID_TIMESTAMP) {
            return 0;
        }
        // ID_TIMESTAMP is followed by a MafiaNet::Time (8 bytes); a frame too short to also hold a
        // real id is malformed — drop it rather than dispatching ID_TIMESTAMP as the id.
        if (length <= 1 + sizeof(MafiaNet::Time)) {
            return -1;
        }
        return 1 + static_cast<int>(sizeof(MafiaNet::Time));
    }

    bool NetworkPeer::IsReplicationPacket(uint8_t packetID) {
        switch (packetID) {
        case ID_REPLICA_MANAGER_CONSTRUCTION:
        case ID_REPLICA_MANAGER_SCOPE_CHANGE:
        case ID_REPLICA_MANAGER_SERIALIZE:
        case ID_REPLICA_MANAGER_DOWNLOAD_STARTED:
        case ID_REPLICA_MANAGER_DOWNLOAD_COMPLETE: return true;
        default: return false;
        }
    }

    bool NetworkPeer::IsAllowedBehindTimestamp(uint8_t packetID) {
        return packetID >= ID_USER_PACKET_ENUM || IsReplicationPacket(packetID);
    }

    const char *NetworkPeer::GetStartupResultString(uint8_t id) {
        return StartupResultString[id];
    }

    const char *NetworkPeer::GetConnectionAttemptString(uint8_t id) {
        return ConnectionAttemptString[id];
    }
} // namespace Framework::Networking
