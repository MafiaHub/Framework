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

#include <mafianet/GetTime.h>
#include <mafianet/statistics.h>
#include <logging/logger.h>

#include <unordered_set>

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

        InitMetrics();
    }

    void NetworkPeer::InitMetrics() {
        auto &reg = Metrics::Registry::Get();
        _netBytesRx = reg.RegisterCounter("fw_net_bytes_total", "Network bytes transferred", {{"direction", "rx"}});
        _netBytesTx = reg.RegisterCounter("fw_net_bytes_total", "Network bytes transferred", {{"direction", "tx"}});
        _netPacketsRx = reg.RegisterCounter("fw_net_packets_received_total", "Valid transport packets received");
        _netRpcSignalsTx = reg.RegisterCounter("fw_net_rpc_signals_total", "RPC4 Signal calls initiated");
        _netUnknownPackets = reg.RegisterCounter("fw_net_unknown_packets_total", "Unknown packet IDs received");
        _netPingSeconds = reg.RegisterHistogram("fw_net_ping_seconds", "Per-connection round-trip time in seconds", Metrics::Buckets::Exponential(0.005, 2.0, 9));
        _netPacketLoss  = reg.RegisterGauge("fw_net_packet_loss_ratio", "Mean packet-loss ratio across connections");
        _netPendingResend = reg.RegisterGauge("fw_net_pending_bytes", "Transport queued bytes", {{"buffer", "resend"}});
        _netPendingSend   = reg.RegisterGauge("fw_net_pending_bytes", "Transport queued bytes", {{"buffer", "send"}});
        _netConnections   = reg.RegisterGauge("fw_net_connections", "Active connection count");
        _netConnections->Set(0.0);
    }

    void NetworkPeer::SampleTransportStats() {
        DataStructures::List<MafiaNet::SystemAddress> addresses;
        DataStructures::List<MafiaNet::RakNetGUID>    guids;
        DataStructures::List<MafiaNet::RakNetStatistics> stats;
        _peer->GetStatisticsList(addresses, guids, stats);

        const unsigned n = stats.Size();
        if (n == 0) {
            _lastBytesRx.clear();
            _lastBytesTx.clear();
            if (_netConnections) {
                _netConnections->Set(0.0);
            }
            if (_netPacketLoss) {
                _netPacketLoss->Set(0.0);
            }
            if (_netPendingResend) {
                _netPendingResend->Set(0.0);
            }
            if (_netPendingSend) {
                _netPendingSend->Set(0.0);
            }
            return;
        }

        if (_netConnections) {
            _netConnections->Set(static_cast<double>(n));
        }

        uint64_t pendingReliable   = 0;
        uint64_t pendingUnreliable = 0;
        double   lossSum           = 0.0;
        std::unordered_set<uint64_t> live;
        live.reserve(n);

        for (unsigned i = 0; i < n; ++i) {
            const auto &s    = stats[i];
            const auto  guid = guids[i].g;
            live.insert(guid);

            const uint64_t curRx = s.runningTotal[MafiaNet::ACTUAL_BYTES_RECEIVED];
            const uint64_t curTx = s.runningTotal[MafiaNet::ACTUAL_BYTES_SENT];
            const auto rxIt = _lastBytesRx.find(guid);
            const uint64_t rxDelta = rxIt == _lastBytesRx.end() ? curRx : curRx >= rxIt->second ? curRx - rxIt->second : curRx;
            if (rxDelta && _netBytesRx) {
                _netBytesRx->Inc(rxDelta);
            }
            _lastBytesRx[guid] = curRx;
            const auto txIt = _lastBytesTx.find(guid);
            const uint64_t txDelta = txIt == _lastBytesTx.end() ? curTx : curTx >= txIt->second ? curTx - txIt->second : curTx;
            if (txDelta && _netBytesTx) {
                _netBytesTx->Inc(txDelta);
            }
            _lastBytesTx[guid] = curTx;

            pendingReliable += s.bytesInResendBuffer;
            for (int p = 0; p < MafiaNet::NUMBER_OF_PRIORITIES; ++p) {
                pendingUnreliable += static_cast<uint64_t>(s.bytesInSendBuffer[p]);
            }
            lossSum += static_cast<double>(s.packetlossLastSecond);
        }

        for (auto it = _lastBytesRx.begin(); it != _lastBytesRx.end();) {
            it = live.contains(it->first) ? std::next(it) : _lastBytesRx.erase(it);
        }
        for (auto it = _lastBytesTx.begin(); it != _lastBytesTx.end();) {
            it = live.contains(it->first) ? std::next(it) : _lastBytesTx.erase(it);
        }

        if (_netPacketLoss) {
            _netPacketLoss->Set(lossSum / static_cast<double>(n));
        }
        if (_netPendingResend) {
            _netPendingResend->Set(static_cast<double>(pendingReliable));
        }
        if (_netPendingSend) {
            _netPendingSend->Set(static_cast<double>(pendingUnreliable));
        }

        const MafiaNet::Time nowMs = MafiaNet::GetTime();
        if (_netPingSeconds && nowMs - _lastPingSampleMs >= 1000) {
            _lastPingSampleMs = nowMs;
            for (unsigned i = 0; i < n; ++i) {
                const int pingMs = GetPing(guids[i]);
                if (pingMs >= 0) {
                    _netPingSeconds->Observe(static_cast<double>(pingMs) / 1000.0);
                }
            }
        }
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

        SampleTransportStats();

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

            if (_netPacketsRx) {
                _netPacketsRx->Inc();
            }

            if (!HandlePacket(packetID, _packet)) {
                if (IsReplicationPacket(packetID)) {
                    continue;
                }
                Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->trace("Received unknown packet {}", packetID);
                if (_netUnknownPackets) {
                    _netUnknownPackets->Inc();
                }
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
