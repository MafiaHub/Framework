/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
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

    bool NetworkClient::Init() {
        SLNet::SocketDescriptor sd {};
        const SLNet::StartupResult result = _peer->Startup(1, &sd, 1);
        if (result != SLNet::RAKNET_STARTED && result != SLNet::RAKNET_ALREADY_STARTED) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->critical("Failed to init the networking peer. Reason: {}", GetStartupResultString(result));
            return false;
        }

        _assetStreamer.SetFileListTransferPlugin(&_fileListTransfer);
        _peer->AttachPlugin(&_fileListTransfer);
        _peer->AttachPlugin(&_assetStreamer);

        _initialized = true;
        return true;
    }

    void NetworkClient::Shutdown() {
        if (!_peer) {
            return;
        }
        _registeredMessageCallbacks.clear();
        SLNet::RakPeerInterface::DestroyInstance(_peer);
        _peer = nullptr;

        Lifecycle::Shutdown();
    }

    ClientError NetworkClient::Connect(const std::string &host, int32_t port, const std::string &password) {
        if (_state != PeerState::DISCONNECTED) {
            Logging::GetInstance()->Get(FRAMEWORK_INNER_NETWORKING)->debug("Cannot connect an already connected instance");
            return ClientError::CLIENT_ALREADY_CONNECTED;
        }

        if (!_peer) {
            return ClientError::CLIENT_PEER_NULL;
        }

        if (!_peer->IsActive()) {
            Init();
        }

        _state = PeerState::CONNECTING;

        const SLNet::ConnectionAttemptResult result = _peer->Connect(host.c_str(), port, password.c_str(), password.length());
        if (result != SLNet::CONNECTION_ATTEMPT_STARTED) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->critical("Failed to connect to the remote host. Reason: {}", GetConnectionAttemptString(result));
            _state = PeerState::DISCONNECTED;
            return ClientError::CLIENT_CONNECT_FAILED;
        }

        return ClientError::CLIENT_NONE;
    }

    ClientError NetworkClient::Disconnect() {
        if (!_peer) {
            return ClientError::CLIENT_PEER_NULL;
        }

        if (_state == PeerState::DISCONNECTED) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->warn("Cannot disconnect, we are already disconnected.");
            return ClientError::CLIENT_NONE;
        }

        _peer->Shutdown(100, 0, IMMEDIATE_PRIORITY);
        Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Disconnecting from the server...");

        if (_onPlayerDisconnectedCallback) {
            _onPlayerDisconnectedCallback(_packet, Messages::GRACEFUL_SHUTDOWN);
        }
        _state = PeerState::DISCONNECTED;

        return ClientError::CLIENT_NONE;
    }

    void NetworkClient::Update() {
        if (_state != PeerState::CONNECTING && _state != PeerState::CONNECTED) {
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
        };

        case ID_NO_FREE_INCOMING_CONNECTIONS: {
            if (_onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, Messages::NO_FREE_SLOT);
            }
            _state = DISCONNECTED;
            return true;
        };

        case ID_DISCONNECTION_NOTIFICATION: {
            if (_onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, Messages::GRACEFUL_SHUTDOWN);
            }
            _state = DISCONNECTED;
            return true;
        };

        case ID_CONNECTION_LOST: {
            if (_onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, Messages::LOST);
            }
            _state = DISCONNECTED;
            return true;
        };

        case ID_CONNECTION_ATTEMPT_FAILED: {
            if (_onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, Messages::FAILED);
            }
            _state = DISCONNECTED;
            return true;
        };

        case ID_INVALID_PASSWORD: {
            if (_onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, Messages::INVALID_PASSWORD);
            }
            _state = DISCONNECTED;
            return true;
        };

        case ID_CONNECTION_BANNED: {
            if (_onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, Messages::BANNED);
            }
            _state = DISCONNECTED;
            return true;
        };
        }
        return false;
    }

    int NetworkClient::GetPing() const {
        if (!_peer || _state != CONNECTED) {
            return 0;
        }

        return _peer->GetAveragePing(_peer->GetSystemAddressFromIndex(0));
    }

    void AssetFileTransfer::OnClosedConnection(const SLNet::SystemAddress &systemAddress, SLNet::RakNetGUID rakNetGUID, SLNet::PI2_LostConnectionReason lostConnectionReason) {
        if (_cb) {
            _cb();
        }
    }

    void AssetFileTransfer::SetCallback(OnAssetsDownloadFailedCallback cb) {
        _cb = std::move(cb);
    }
} // namespace Framework::Networking
