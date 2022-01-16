/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "network_client.h"

#include <logging/logger.h>

namespace Framework::Networking {
    NetworkClient::NetworkClient(): NetworkPeer(), _state(PeerState::DISCONNECTED) {}

    NetworkClient::~NetworkClient() {
        Shutdown();
    }

    ClientError NetworkClient::Init() {
        SLNet::SocketDescriptor sd;
        SLNet::StartupResult result = _peer->Startup(1, &sd, 1);
        if (result != SLNet::RAKNET_STARTED && result != SLNet::RAKNET_ALREADY_STARTED) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->critical("Failed to init the networking peer. Reason: {}", GetStartupResultString(result));
            return CLIENT_PEER_FAILED;
        }
        return CLIENT_NONE;
    }

    ClientError NetworkClient::Shutdown() {
        if (!_peer) {
            return CLIENT_PEER_NULL;
        }
        _registeredMessageCallbacks.clear();
        SLNet::RakPeerInterface::DestroyInstance(_peer);
        _peer = nullptr;

        return CLIENT_NONE;
    }

    ClientError NetworkClient::Connect(const std::string &host, int32_t port, const std::string &password) {
        if (_state != DISCONNECTED) {
            Logging::GetInstance()->Get(FRAMEWORK_INNER_NETWORKING)->debug("Cannot connect an already connected instance");
            return CLIENT_ALREADY_CONNECTED;
        }

        if (!_peer) {
            return CLIENT_PEER_NULL;
        }

        _state = CONNECTING;

        SLNet::ConnectionAttemptResult result = _peer->Connect(host.c_str(), port, password.c_str(), password.length());
        if (result != SLNet::CONNECTION_ATTEMPT_STARTED) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->critical("Failed to connect to the remote host. Reason: {}", GetConnectionAttemptString(result));
            return CLIENT_CONNECT_FAILED;
        }

        return CLIENT_NONE;
    }

    ClientError NetworkClient::Disconnect() {
        if (!_peer) {
            return CLIENT_PEER_NULL;
        }
        _peer->Shutdown(100, 0, IMMEDIATE_PRIORITY);
        return CLIENT_NONE;
    }

    void NetworkClient::Update() {
        if (_state != CONNECTING && _state != CONNECTED) {
            return;
        }

        NetworkPeer::Update();
    }

    bool NetworkClient::HandlePacket(uint8_t packetID, SLNet::Packet *packet) {
        switch (packetID) {
        case ID_CONNECTION_REQUEST_ACCEPTED: {
            if (_onPlayerConnectedCallback) {
                _onPlayerConnectedCallback(_packet);
            }
            _state = CONNECTED;
            return true;
        } break;

        case ID_NO_FREE_INCOMING_CONNECTIONS: {
            if (_onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, Messages::NO_FREE_SLOT);
            }
            _state = DISCONNECTED;
            return true;
        } break;

        case ID_DISCONNECTION_NOTIFICATION: {
            if (_onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, Messages::GRACEFUL_SHUTDOWN);
            }
            _state = DISCONNECTED;
            return true;
        } break;

        case ID_CONNECTION_LOST: {
            if (_onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, Messages::LOST);
            }
            _state = DISCONNECTED;
            return true;
        } break;

        case ID_CONNECTION_ATTEMPT_FAILED: {
            if (_onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, Messages::FAILED);
            }
            _state = DISCONNECTED;
            return true;
        } break;

        case ID_INVALID_PASSWORD: {
            if (_onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, Messages::INVALID_PASSWORD);
            }
            _state = DISCONNECTED;
            return true;
        } break;

        case ID_CONNECTION_BANNED: {
            if (_onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, Messages::BANNED);
            }
            _state = DISCONNECTED;
            return true;
        } break;
        }
        return false;
    }

    int NetworkClient::GetPing() {
        if (!_peer) {
            return 0;
        }

        return _peer->GetAveragePing(SLNet::UNASSIGNED_RAKNET_GUID);
    }
} // namespace Framework::Networking
