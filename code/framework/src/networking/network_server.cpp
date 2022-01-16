/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "network_server.h"

#include <BitStream.h>
#include <MessageIdentifiers.h>
#include <logging/logger.h>

namespace Framework::Networking {
    ServerError NetworkServer::Init(int32_t port, const std::string &host, int32_t maxPlayers, const std::string &password) {
        SLNet::SocketDescriptor newSocketSd = SLNet::SocketDescriptor(port, host.c_str());
        SLNet::StartupResult result         = _peer->Startup(maxPlayers, &newSocketSd, 1);
        if (result != SLNet::RAKNET_STARTED) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->critical("Failed to init the networking peer. Reason: {}", GetStartupResultString(result));
            return SERVER_PEER_FAILED;
        }

        if (!password.empty()) {
            _peer->SetIncomingPassword(password.c_str(), password.length());
            Logging::GetInstance()->Get(FRAMEWORK_INNER_NETWORKING)->debug("Applying incoming password to networking peer");
        }

        _peer->SetMaximumIncomingConnections(maxPlayers);
        return SERVER_NONE;
    }

    void NetworkServer::Update() {
        NetworkPeer::Update();
    }

    bool NetworkServer::HandlePacket(uint8_t packetID, SLNet::Packet *packet) {
        switch (packetID) {
        case ID_NEW_INCOMING_CONNECTION: {
            Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_NETWORKING)->debug("Incoming connection request {}", packet->guid.ToString());
            if (_onPlayerConnectCallback) {
                _onPlayerConnectCallback(packet);
            }
            return true;
        } break;

        case ID_DISCONNECTION_NOTIFICATION: {
            Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_NETWORKING)->debug("Disconnection from {}", packet->guid.ToString());
            if (_onPlayerDisconnectCallback) {
                _onPlayerDisconnectCallback(_packet, Messages::GRACEFUL_SHUTDOWN);
            }
            return true;
        } break;
        case ID_CONNECTION_LOST: {
            Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_NETWORKING)->debug("Connection lost for {}", packet->guid.ToString());
            if (_onPlayerDisconnectCallback) {
                _onPlayerDisconnectCallback(_packet, Messages::LOST);
            }
            return true;
        } break;
        }
        return false;
    }

    ServerError NetworkServer::Shutdown() {
        if (!_peer) {
            return SERVER_PEER_NULL;
        }

        _peer->Shutdown(1000);
        SLNet::RakPeerInterface::DestroyInstance(_peer);
        return SERVER_NONE;
    }

    int NetworkServer::GetPing(SLNet::RakNetGUID guid) {
        return _peer->GetAveragePing(guid);
    }
} // namespace Framework::Networking
