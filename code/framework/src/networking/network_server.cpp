/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "network_server.h"

#include "replication/replication_manager.h"

#include <logging/logger.h>
#include <mafianet/BitStream.h>
#include <mafianet/MessageIdentifiers.h>
#include <mafianet/guid_util.h>

namespace Framework::Networking {
    namespace {
        // How long Shutdown() blocks to flush the disconnection notification to connected peers, in
        // ms. Larger than the client's because the server broadcasts to every connection.
        constexpr unsigned int kShutdownBlockDurationMs = 1000;

        const char *DisconnectionReasonName(DisconnectionReason r) {
            switch (r) {
            case DisconnectionReason::NO_FREE_SLOT: return "no_free_slot";
            case DisconnectionReason::GRACEFUL_SHUTDOWN: return "graceful_shutdown";
            case DisconnectionReason::LOST: return "lost";
            case DisconnectionReason::FAILED: return "failed";
            case DisconnectionReason::INVALID_PASSWORD: return "invalid_password";
            case DisconnectionReason::WRONG_VERSION: return "wrong_version";
            case DisconnectionReason::BANNED: return "banned";
            case DisconnectionReason::KICKED: return "kicked";
            case DisconnectionReason::KICKED_CUSTOM: return "kicked_custom";
            case DisconnectionReason::KICKED_INVALID_PACKET: return "kicked_invalid_packet";
            case DisconnectionReason::UNKNOWN: return "unknown";
            }
            return "unknown";
        }
    } // namespace

    Utils::Result<void, Error> NetworkServer::Init(const std::string &host, int32_t port, int32_t maxPlayers, const std::string &password) {
        if (port <= 0 || port > 65535) {
            return Error("Invalid server port: " + std::to_string(port));
        }
        if (maxPlayers <= 0 || maxPlayers > 65535) {
            return Error("Invalid maxPlayers: " + std::to_string(maxPlayers));
        }
        auto newSocketSd                     = MafiaNet::SocketDescriptor((uint16_t)port, host.c_str());
        const MafiaNet::StartupResult result = _peer->Startup(maxPlayers, &newSocketSd, 1);
        if (result != MafiaNet::RAKNET_STARTED) {
            return Error(std::string("Failed to start networking peer: ") + GetStartupResultString((uint8_t)result));
        }

        if (!password.empty()) {
            _peer->SetIncomingPassword(password.c_str(), (uint32_t)password.length());
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Applying incoming password to networking peer");
        }

        _peer->SetMaximumIncomingConnections((uint16_t)maxPlayers);

        _assetStreamer.SetFileListTransferPlugin(&_fileListTransfer);
        _peer->AttachPlugin(&_fileListTransfer);
        _peer->AttachPlugin(&_assetStreamer);
        RegisterBuildToken();

        // Run replication as the authoritative server.
        _replicationManager->Init(this, true);

        // Gate replication behind the handshake: don't auto-create the connection on connect (see
        // PushReplicationConnection). autoDestroy stays on so dropped peers are torn down.
        _replicationManager->SetAutoManageConnections(false, true);

        InitMetrics();

        _initialized = true;
        return {};
    }

    void NetworkServer::InitMetrics() {
        auto &reg = Metrics::Registry::Get();
        static constexpr DisconnectionReason kAllReasons[] = {
            DisconnectionReason::NO_FREE_SLOT,
            DisconnectionReason::GRACEFUL_SHUTDOWN,
            DisconnectionReason::LOST,
            DisconnectionReason::FAILED,
            DisconnectionReason::INVALID_PASSWORD,
            DisconnectionReason::WRONG_VERSION,
            DisconnectionReason::BANNED,
            DisconnectionReason::KICKED,
            DisconnectionReason::KICKED_CUSTOM,
            DisconnectionReason::KICKED_INVALID_PACKET,
            DisconnectionReason::UNKNOWN,
        };
        for (const DisconnectionReason r : kAllReasons) {
            _kickCounters[r]   = reg.RegisterCounter("fw_net_kicks_total", "Players kicked, by reason", {{"reason", DisconnectionReasonName(r)}});
            _closedCounters[r] = reg.RegisterCounter("fw_net_connections_closed_total", "Accepted connections closed, by reason", {{"reason", DisconnectionReasonName(r)}});
        }
        _connFailVersion          = reg.RegisterCounter("fw_net_connection_failures_total", "Connection failures by handshake stage", {{"stage", "version"}});
        _connFailReady            = reg.RegisterCounter("fw_net_connection_failures_total", "Connection failures by handshake stage", {{"stage", "ready"}});
        _connectionsAccepted      = reg.RegisterCounter("fw_net_connections_accepted_total", "Transport connections accepted by the server");
        _connectionsReady         = reg.RegisterCounter("fw_net_connections_ready_total", "Connections that completed authentication, assets, and identity");
        _authenticatedConnections = reg.RegisterGauge("fw_net_authenticated_connections", "Currently connected peers that passed build authentication");
        _readyConnections         = reg.RegisterGauge("fw_net_ready_connections", "Currently connected peers that completed the ready handshake");
        _authenticatedConnections->Set(0.0);
        _readyConnections->Set(0.0);
    }

    bool NetworkServer::HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) {
        switch (packetID) {
        case ID_NEW_INCOMING_CONNECTION: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Incoming connection request {}", MafiaNet::to_string(packet->guid));
            if (_acceptedClients.insert(packet->guid.g).second && _connectionsAccepted) {
                _connectionsAccepted->Inc();
            }
            if (_onPlayerConnectCallback) {
                _onPlayerConnectCallback(packet);
            }
            return true;
        };

        case ID_DISCONNECTION_NOTIFICATION: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Disconnection from {}", MafiaNet::to_string(packet->guid));
            if (_onPlayerDisconnectCallback) {
                _onPlayerDisconnectCallback(packet, DisconnectionReason::GRACEFUL_SHUTDOWN, "");
            }
            RecordConnectionClosed(packet->guid, DisconnectionReason::GRACEFUL_SHUTDOWN);
            return true;
        };
        case ID_CONNECTION_LOST: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Connection lost for {}", MafiaNet::to_string(packet->guid));
            if (_onPlayerDisconnectCallback) {
                _onPlayerDisconnectCallback(packet, DisconnectionReason::LOST, "");
            }
            RecordConnectionClosed(packet->guid, DisconnectionReason::LOST);
            return true;
        };

        // Build gate: the client challenges us with its build token. Match -> asset phase; mismatch
        // -> drop (the version-incompatibility path).
        case ID_TWO_WAY_AUTHENTICATION_INCOMING_CHALLENGE_SUCCESS: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Build verified for {}", MafiaNet::to_string(packet->guid));
            _authenticatedClients.insert(packet->guid.g);
            if (_authenticatedConnections) {
                _authenticatedConnections->Set(static_cast<double>(_authenticatedClients.size()));
            }
            if (_onClientAuthenticatedCallback) {
                _onClientAuthenticatedCallback(packet->guid);
            }
            return true;
        };
        case ID_TWO_WAY_AUTHENTICATION_INCOMING_CHALLENGE_FAILURE: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->warn("Build mismatch from {}, dropping peer", MafiaNet::to_string(packet->guid));
            if (_connFailVersion) {
                _connFailVersion->Inc();
            }
            RecordConnectionClosed(packet->guid, DisconnectionReason::WRONG_VERSION);
            _peer->CloseConnection(packet->guid, true);
            return true;
        };

        // ReadyEvent: the server arms its half from the integration layer and takes no action on
        // completion (the avatar already exists); consumed so they don't hit the unknown-packet path.
        case ID_READY_EVENT_SET:
        case ID_READY_EVENT_UNSET:
        case ID_READY_EVENT_ALL_SET:
        case ID_READY_EVENT_QUERY:
        case ID_READY_EVENT_FORCE_ALL_SET: return true;

        default: break;
        }
        return false;
    }

    void NetworkServer::Shutdown() {
        if (!_peer) {
            return;
        }

        if (const auto it = _closedCounters.find(DisconnectionReason::GRACEFUL_SHUTDOWN); it != _closedCounters.end() && it->second && !_acceptedClients.empty()) {
            it->second->Inc(static_cast<uint64_t>(_acceptedClients.size()));
        }
        _acceptedClients.clear();
        _authenticatedClients.clear();
        _readyClients.clear();
        if (_authenticatedConnections) {
            _authenticatedConnections->Set(0.0);
        }
        if (_readyConnections) {
            _readyConnections->Set(0.0);
        }

        _peer->Shutdown(kShutdownBlockDurationMs);
        MafiaNet::RakPeerInterface::DestroyInstance(_peer);
        _peer = nullptr;
        Lifecycle::Shutdown();
    }

    int NetworkServer::GetPing(MafiaNet::RakNetGUID guid) const {
        return _peer->GetAveragePing(guid);
    }
    std::string NetworkServer::GetAddress(MafiaNet::RakNetGUID guid) const {
        const auto address = _peer->GetSystemAddressFromGuid(guid);
        if (address == MafiaNet::UNASSIGNED_SYSTEM_ADDRESS) {
            return "";
        }
        char buffer[64] = {0};
        address.ToString(false, buffer, sizeof(buffer));
        return buffer;
    }
    void NetworkServer::SignalExcept(const char *identifier, MafiaNet::BitStream &bs, MafiaNet::RakNetGUID excludeGUID, MafiaNet::Priority priority, MafiaNet::Reliability reliability) {
        // When broadcasting, the system identifier is the peer to exclude, so a single Signal reaches
        // everyone but the sender.
        _rpc.Signal(identifier, &bs, priority, reliability, 0, excludeGUID, true, false);
        RecordRpcSignal();
    }

    void NetworkServer::PushReplicationConnection(MafiaNet::RakNetGUID guid) {
        if (!_replicationManager) {
            return;
        }
        if (_replicationManager->GetConnectionByGUID(guid) != nullptr) {
            return; // already pushed (a client could send ClientIdentity twice)
        }
        const auto address = _peer->GetSystemAddressFromGuid(guid);
        if (MafiaNet::Connection_RM3 *connection = _replicationManager->AllocConnection(address, guid)) {
            _replicationManager->PushConnection(connection);
        }
    }

    void NetworkServer::MarkClientReady(MafiaNet::RakNetGUID guid) {
        if (!_acceptedClients.contains(guid.g) || !_authenticatedClients.contains(guid.g)) {
            return;
        }
        if (_readyClients.insert(guid.g).second && _connectionsReady) {
            _connectionsReady->Inc();
        }
        if (_readyConnections) {
            _readyConnections->Set(static_cast<double>(_readyClients.size()));
        }
    }

    void NetworkServer::KickPlayer(MafiaNet::RakNetGUID guid, DisconnectionReason reason, const std::string &customReason) {
        if (const auto it = _kickCounters.find(reason); it != _kickCounters.end() && it->second) {
            it->second->Inc();
        }

        DisconnectPayload payload;
        payload.reason       = static_cast<uint32_t>(reason);
        payload.customReason = customReason;

        // Reason rides the disconnect notification, so no separate message races the close.
        MafiaNet::BitStream reasonData;
        payload.Serialize(&reasonData, true);
        RecordConnectionClosed(guid, reason);
        _peer->CloseConnection(guid, true, 0, MafiaNet::Priority::Low, &reasonData);
    }

    void NetworkServer::RecordConnectionClosed(MafiaNet::RakNetGUID guid, DisconnectionReason reason) {
        if (!_acceptedClients.erase(guid.g)) {
            return;
        }
        if (_authenticatedClients.contains(guid.g) && !_readyClients.contains(guid.g) && _connFailReady) {
            _connFailReady->Inc();
        }
        if (const auto it = _closedCounters.find(reason); it != _closedCounters.end() && it->second) {
            it->second->Inc();
        }
        ClearClientState(guid);
    }

    void NetworkServer::ClearClientState(MafiaNet::RakNetGUID guid) {
        _authenticatedClients.erase(guid.g);
        _readyClients.erase(guid.g);
        _peerIdentities.erase(guid.g);
        _readyEvent.DeleteEvent(ReadyEventId(guid)); // recycle the slot for reconnects
        if (_authenticatedConnections) {
            _authenticatedConnections->Set(static_cast<double>(_authenticatedClients.size()));
        }
        if (_readyConnections) {
            _readyConnections->Set(static_cast<double>(_readyClients.size()));
        }
    }
} // namespace Framework::Networking
