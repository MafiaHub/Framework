/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "network_client.h"

#include "replication/replication_manager.h"

#include <logging/logger.h>

namespace Framework::Networking {
    NetworkClient::NetworkClient(): NetworkPeer(), _state(PeerState::DISCONNECTED) {}

    NetworkClient::~NetworkClient() {
        Shutdown();
    }

    Utils::Result<void, Error> NetworkClient::Init() {
        MafiaNet::SocketDescriptor sd {};
        const MafiaNet::StartupResult result = _peer->Startup(1, &sd, 1);
        if (result != MafiaNet::RAKNET_STARTED && result != MafiaNet::RAKNET_ALREADY_STARTED) {
            return Error(std::string("Failed to start networking peer: ") + GetStartupResultString(result));
        }

        _assetStreamer.SetFileListTransferPlugin(&_fileListTransfer);
        _peer->AttachPlugin(&_fileListTransfer);
        _peer->AttachPlugin(&_assetStreamer);
        RegisterBuildToken();

        // Run replication as a client: receive constructions and serialize owned entities upstream.
        _replicationManager->Init(this, false);

        _initialized = true;
        return {};
    }

    void NetworkClient::Shutdown() {
        if (!_peer) {
            return;
        }
        MafiaNet::RakPeerInterface::DestroyInstance(_peer);
        _peer = nullptr;

        Lifecycle::Shutdown();
    }

    Utils::Result<void, Error> NetworkClient::Connect(const std::string &host, int32_t port, const std::string &password) {
        if (_state != PeerState::DISCONNECTED) {
            return Error("Cannot connect: the client is already connected");
        }

        if (!_peer) {
            return Error("Cannot connect: network peer is null");
        }

        if (!_peer->IsActive()) {
            if (auto initResult = Init(); !initResult) {
                return initResult;
            }
        }

        _state = PeerState::CONNECTING;

        const MafiaNet::ConnectionAttemptResult result = _peer->Connect(host.c_str(), port, password.c_str(), password.length());
        if (result != MafiaNet::CONNECTION_ATTEMPT_STARTED) {
            _state = PeerState::DISCONNECTED;
            return Error(std::string("Failed to connect to ") + host + ":" + std::to_string(port) + ": " + GetConnectionAttemptString(result));
        }

        return {};
    }

    Utils::Result<void, Error> NetworkClient::Disconnect() {
        if (!_peer) {
            return Error("Cannot disconnect: network peer is null");
        }

        if (_state == PeerState::DISCONNECTED) {
            return {};
        }

        _peer->Shutdown(100, 0, MafiaNet::Priority::Immediate);
        Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Disconnecting from the server...");

        // Peer shutdown fires TwoWayAuthentication::Clear(), wiping the registered build token;
        // drop the cache so the next Connect()'s Init() re-registers instead of no-op'ing.
        _registeredToken.clear();

        if (_onPlayerDisconnectedCallback) {
            // Locally initiated: there is no inbound packet, so pass null rather than a stale _packet.
            _onPlayerDisconnectedCallback(nullptr, DisconnectionReason::GRACEFUL_SHUTDOWN, "");
        }
        _state = PeerState::DISCONNECTED;

        return {};
    }

    void NetworkClient::Update() {
        if (_state != PeerState::CONNECTING && _state != PeerState::CONNECTED) {
            return;
        }

        NetworkPeer::Update();
    }

    bool NetworkClient::HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) {
        switch (packetID) {
        case ID_CONNECTION_REQUEST_ACCEPTED: {
            _state = PeerState::CONNECTED;
            if (_onPlayerConnectedCallback) {
                _onPlayerConnectedCallback(_packet);
            }
            // Prove our build matches before anything else; the server sends ServerResources only
            // after this passes. A mismatch is handled by the auth-failure cases below.
            if (!_twoWayAuth.Challenge(NetworkPeer::kBuildChallengeId, _packet->guid)) {
                Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->error("Cannot verify server build: local build token is not registered");
                if (_onPlayerDisconnectedCallback) {
                    _onPlayerDisconnectedCallback(_packet, DisconnectionReason::WRONG_VERSION, "");
                }
                _peer->CloseConnection(_packet->guid, true);
                _state = PeerState::DISCONNECTED;
            }
            return true;
        };

        case ID_NO_FREE_INCOMING_CONNECTIONS: {
            if (_state != PeerState::DISCONNECTED && _onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, DisconnectionReason::NO_FREE_SLOT, "");
            }
            _state = PeerState::DISCONNECTED;
            return true;
        };

        case ID_DISCONNECTION_NOTIFICATION: {
            if (_state != PeerState::DISCONNECTED && _onPlayerDisconnectedCallback) {
                // A kick carries a reason payload after the id; a plain disconnect has none.
                DisconnectionReason reason = DisconnectionReason::GRACEFUL_SHUTDOWN;
                std::string customReason;
                if (_packet->length - _packetDataOffset > sizeof(MafiaNet::MessageID)) {
                    MafiaNet::BitStream bs(_packet->data + _packetDataOffset, _packet->length - _packetDataOffset, false);
                    bs.IgnoreBytes(sizeof(MafiaNet::MessageID));
                    DisconnectPayload payload;
                    payload.Serialize(&bs, false);
                    reason       = static_cast<DisconnectionReason>(payload.reason);
                    customReason = payload.customReason;
                }
                _onPlayerDisconnectedCallback(_packet, reason, customReason);
            }
            _state = PeerState::DISCONNECTED;
            return true;
        };

        case ID_CONNECTION_LOST: {
            if (_state != PeerState::DISCONNECTED && _onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, DisconnectionReason::LOST, "");
            }
            _state = PeerState::DISCONNECTED;
            return true;
        };

        case ID_CONNECTION_ATTEMPT_FAILED: {
            if (_state != PeerState::DISCONNECTED && _onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, DisconnectionReason::FAILED, "");
            }
            _state = PeerState::DISCONNECTED;
            return true;
        };

        case ID_INVALID_PASSWORD: {
            if (_state != PeerState::DISCONNECTED && _onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, DisconnectionReason::INVALID_PASSWORD, "");
            }
            _state = PeerState::DISCONNECTED;
            return true;
        };

        case ID_CONNECTION_BANNED: {
            if (_state != PeerState::DISCONNECTED && _onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, DisconnectionReason::BANNED, "");
            }
            _state = PeerState::DISCONNECTED;
            return true;
        };

        // Build gate: verified -> wait for the server's ServerResources RPC; mismatch -> disconnect.
        case ID_TWO_WAY_AUTHENTICATION_OUTGOING_CHALLENGE_SUCCESS: {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Build verified by server");
            return true;
        };
        case ID_TWO_WAY_AUTHENTICATION_OUTGOING_CHALLENGE_FAILURE:
        case ID_TWO_WAY_AUTHENTICATION_OUTGOING_CHALLENGE_TIMEOUT: {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->error("Build mismatch with server, disconnecting");
            if (_state != PeerState::DISCONNECTED && _onPlayerDisconnectedCallback) {
                _onPlayerDisconnectedCallback(_packet, DisconnectionReason::WRONG_VERSION, "");
            }
            _state = PeerState::DISCONNECTED;
            return true;
        };

        case ID_READY_EVENT_ALL_SET: {
            // Payload: message id then int eventId (ReadyEvent::PushCompletionPacket).
            MafiaNet::BitStream bs(_packet->data + _packetDataOffset, _packet->length - _packetDataOffset, false);
            bs.IgnoreBytes(sizeof(MafiaNet::MessageID));
            int eventId = 0;
            bs.Read(eventId);
            if (_onConnectionReadyCallback) {
                _onConnectionReadyCallback(eventId);
            }
            return true;
        };
        case ID_READY_EVENT_SET:
        case ID_READY_EVENT_UNSET:
        case ID_READY_EVENT_QUERY:
        case ID_READY_EVENT_FORCE_ALL_SET:
            return true;
        }
        return false;
    }

    int NetworkClient::GetPing() const {
        if (!_peer || _state != PeerState::CONNECTED) {
            return 0;
        }

        return _peer->GetAveragePing(_peer->GetSystemAddressFromIndex(0));
    }

    void AssetFileTransfer::OnClosedConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID rakNetGUID, MafiaNet::PI2_LostConnectionReason lostConnectionReason) {
        if (_cb) {
            _cb();
        }
    }

    void AssetFileTransfer::SetCallback(OnAssetsDownloadFailedCallback cb) {
        _cb = std::move(cb);
    }
} // namespace Framework::Networking
