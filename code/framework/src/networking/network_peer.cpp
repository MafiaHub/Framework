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

#include <logging/logger.h>

namespace Framework::Networking {
    NetworkPeer::NetworkPeer() {
        _peer = MafiaNet::RakPeerInterface::GetInstance();

        // RPC4 and StatisticsHistory can be attached before Startup(); the ReplicationManager is
        // attached by the concrete peer's Init() once its connection factory exists.
        _peer->AttachPlugin(&_rpc);
        _peer->AttachPlugin(&_statisticsHistory);
        _peer->AttachPlugin(&_twoWayAuth);
        _peer->AttachPlugin(&_readyEvent);
        _statisticsHistory.SetTrackConnections(true, 0, true);

        _replicationManager = std::make_unique<Replication::ReplicationManager>();
    }

    NetworkPeer::~NetworkPeer() = default;

    void NetworkPeer::SetBuildToken(const std::string &token) {
        _buildToken = token;
        if (!RegisterBuildToken()) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->error("Failed to register networking build token");
        }
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

        // Rebuild the spatial index before ReplicaManager3 computes per-connection relevance.
        if (_replicationManager) {
            _replicationManager->RebuildInterest();
        }

        for (_packet = _peer->Receive(); _packet; _peer->DeallocatePacket(_packet), _packet = _peer->Receive()) {
            if (_packet->length == 0) {
                continue;
            }
            const int offset = ResolvePacketDataOffset(_packet->data, _packet->length);
            if (offset < 0) {
                continue;
            }
            _packetDataOffset = offset;
            uint8_t packetID = _packet->data[_packetDataOffset];

            if (!HandlePacket(packetID, _packet)) {
                if (IsReplicationPacket(packetID)) {
                    continue;
                }
                Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->trace("Received unknown packet {}", packetID);
                if (_onUnknownPacketCallback) {
                    _onUnknownPacketCallback(_packet);
                }
            }
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
        case ID_REPLICA_MANAGER_DOWNLOAD_COMPLETE:
            return true;
        default:
            return false;
        }
    }

    const char *NetworkPeer::GetStartupResultString(uint8_t id) {
        return StartupResultString[id];
    }

    const char *NetworkPeer::GetConnectionAttemptString(uint8_t id) {
        return ConnectionAttemptString[id];
    }
} // namespace Framework::Networking
