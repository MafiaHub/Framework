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
            _packetDataOffset = 0;
            if (_packet->length == 0) {
                continue;
            }
            MafiaNet::TimeMS TS  = 0;
            if (_packet->length > 1 + sizeof(MafiaNet::TimeMS) && _packet->data[0] == ID_TIMESTAMP) {
                MafiaNet::BitStream timestamp(_packet->data + 1, sizeof(MafiaNet::TimeMS) + 1, false);
                timestamp.Read(TS);
                _packetDataOffset = 1 + sizeof(MafiaNet::TimeMS);
            }

            if (static_cast<uint32_t>(_packetDataOffset) >= _packet->length) {
                continue;
            }
            uint8_t packetID = _packet->data[_packetDataOffset];

            if (!HandlePacket(packetID, _packet)) {
                Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->trace("Received unknown packet {}", packetID);
                if (_onUnknownPacketCallback) {
                    _onUnknownPacketCallback(_packet);
                }
            }
        }
    }

    const char *NetworkPeer::GetStartupResultString(uint8_t id) {
        return StartupResultString[id];
    }

    const char *NetworkPeer::GetConnectionAttemptString(uint8_t id) {
        return ConnectionAttemptString[id];
    }
} // namespace Framework::Networking
