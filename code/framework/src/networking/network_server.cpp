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
    NetworkPeerError NetworkServer::Init(const std::string &host, int32_t port, int32_t maxPlayers, const std::string &password) {
        auto newSocketSd                  = MafiaNet::SocketDescriptor((uint16_t)port, host.c_str());
        const MafiaNet::StartupResult result = _peer->Startup(maxPlayers, &newSocketSd, 1);
        if (result != MafiaNet::RAKNET_STARTED) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->critical("Failed to init the networking peer. Reason: {}", GetStartupResultString((uint8_t)result));
            return NetworkPeerError::NETWORK_PEER_INIT_FAILED;
        }

        if (!password.empty()) {
            _peer->SetIncomingPassword(password.c_str(), (uint32_t)password.length());
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Applying incoming password to networking peer");
        }

        _peer->SetMaximumIncomingConnections((uint16_t)maxPlayers);

        _assetStreamer.SetFileListTransferPlugin(&_fileListTransfer);
        _peer->AttachPlugin(&_fileListTransfer);
        _peer->AttachPlugin(&_assetStreamer);

        // Run replication as the authoritative server.
        _replicationManager->Init(_peer, &_networkIDManager, true);

        _initialized = true;
        return NetworkPeerError::NETWORK_PEER_NONE;
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
                _onPlayerDisconnectCallback(_packet, Messages::DisconnectionReason::GRACEFUL_SHUTDOWN);
            }
            return true;
        };
        case ID_CONNECTION_LOST: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Connection lost for {}", packet->guid.ToString());
            if (_onPlayerDisconnectCallback) {
                _onPlayerDisconnectCallback(_packet, Messages::DisconnectionReason::LOST);
            }
            return true;
        };
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
        auto *replication = GetReplicationManager();
        if (!replication) {
            return;
        }
        const unsigned count = replication->GetConnectionCount();
        for (unsigned i = 0; i < count; ++i) {
            auto *connection = replication->GetConnectionAtIndex(i);
            if (!connection || connection->GetRakNetGUID().g == excludeGUID.g) {
                continue;
            }
            _rpc.Signal(identifier, &bs, priority, reliability, 0, connection->GetRakNetGUID(), false, false);
        }
    }
} // namespace Framework::Networking
