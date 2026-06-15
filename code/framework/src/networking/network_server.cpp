/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "network_server.h"

#include "replication/replication_manager.h"

#include <mafianet/BitStream.h>
#include <mafianet/MessageIdentifiers.h>
#include <logging/logger.h>

namespace Framework::Networking {
    Utils::Result<void, Error> NetworkServer::Init(const std::string &host, int32_t port, int32_t maxPlayers, const std::string &password) {
        if (port <= 0 || port > 65535) {
            return Error("Invalid server port: " + std::to_string(port));
        }
        if (maxPlayers <= 0 || maxPlayers > 65535) {
            return Error("Invalid maxPlayers: " + std::to_string(maxPlayers));
        }
        auto newSocketSd                  = MafiaNet::SocketDescriptor((uint16_t)port, host.c_str());
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

        _initialized = true;
        return {};
    }

    bool NetworkServer::HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) {
        switch (packetID) {
        case ID_NEW_INCOMING_CONNECTION: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Incoming connection request {}", packet->guid.ToString());
            if (_onPlayerConnectCallback) {
                _onPlayerConnectCallback(packet);
            }
            return true;
        };

        case ID_DISCONNECTION_NOTIFICATION: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Disconnection from {}", packet->guid.ToString());
            if (_onPlayerDisconnectCallback) {
                _onPlayerDisconnectCallback(_packet, DisconnectionReason::GRACEFUL_SHUTDOWN, "");
            }
            ClearClientState(packet->guid);
            return true;
        };
        case ID_CONNECTION_LOST: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Connection lost for {}", packet->guid.ToString());
            if (_onPlayerDisconnectCallback) {
                _onPlayerDisconnectCallback(_packet, DisconnectionReason::LOST, "");
            }
            ClearClientState(packet->guid);
            return true;
        };

        // Build gate: the client challenges us with its build token. Match -> asset phase; mismatch
        // -> drop (the version-incompatibility path).
        case ID_TWO_WAY_AUTHENTICATION_INCOMING_CHALLENGE_SUCCESS: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Build verified for {}", packet->guid.ToString());
            _authenticatedClients.insert(packet->guid.g);
            if (_onClientAuthenticatedCallback) {
                _onClientAuthenticatedCallback(packet->guid);
            }
            return true;
        };
        case ID_TWO_WAY_AUTHENTICATION_INCOMING_CHALLENGE_FAILURE: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->warn("Build mismatch from {}, dropping peer", packet->guid.ToString());
            _peer->CloseConnection(packet->guid, true);
            return true;
        };

        // ReadyEvent: the server arms its half from the integration layer and takes no action on
        // completion (the avatar already exists); consumed so they don't hit the unknown-packet path.
        case ID_READY_EVENT_SET:
        case ID_READY_EVENT_UNSET:
        case ID_READY_EVENT_ALL_SET:
        case ID_READY_EVENT_QUERY:
        case ID_READY_EVENT_FORCE_ALL_SET:
            return true;

        default: break;
        }
        return false;
    }

    void NetworkServer::Shutdown() {
        if (!_peer) {
            return;
        }

        _peer->Shutdown(1000);
        MafiaNet::RakPeerInterface::DestroyInstance(_peer);
        _peer = nullptr;
        Lifecycle::Shutdown();
    }

    int NetworkServer::GetPing(MafiaNet::RakNetGUID guid) const {
        return _peer->GetAveragePing(guid);
    }
    void NetworkServer::SignalExcept(const char *identifier, MafiaNet::BitStream &bs, MafiaNet::RakNetGUID excludeGUID, PacketPriority priority, PacketReliability reliability) {
        // When broadcasting, the system identifier is the peer to exclude, so a single Signal reaches
        // everyone but the sender.
        _rpc.Signal(identifier, &bs, priority, reliability, 0, excludeGUID, true, false);
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

    void NetworkServer::KickPlayer(MafiaNet::RakNetGUID guid, DisconnectionReason reason, const std::string &customReason) {
        DisconnectPayload payload;
        payload.reason       = static_cast<uint32_t>(reason);
        payload.customReason = customReason;

        // Reason rides the disconnect notification, so no separate message races the close.
        MafiaNet::BitStream reasonData;
        payload.Serialize(&reasonData, true);
        _peer->CloseConnection(guid, true, 0, LOW_PRIORITY, &reasonData);
        ClearClientState(guid);
    }

    void NetworkServer::ClearClientState(MafiaNet::RakNetGUID guid) {
        _authenticatedClients.erase(guid.g);
        _readyEvent.DeleteEvent(ReadyEventId(guid)); // recycle the slot for reconnects
    }
} // namespace Framework::Networking
