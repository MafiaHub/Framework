#include "network_client.h"

#include <logging/logger.h>
#include <optick.h>

namespace Framework::Networking {
    NetworkClient::NetworkClient(): _state(PeerState::DISCONNECTED) {
        _peer = SLNet::RakPeerInterface::GetInstance();
    }

    NetworkClient::~NetworkClient() {
        Shutdown();
    }

    ClientError NetworkClient::Init() {
        SLNet::SocketDescriptor sd;
        SLNet::StartupResult result = _peer->Startup(1, &sd, 1);
        if (result != SLNet::RAKNET_STARTED && result != SLNet::RAKNET_ALREADY_STARTED) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Failed to init the networking peer");
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

    ClientError NetworkClient::Connect(std::string &host, int32_t port, std::string &password) {
        if (_state != DISCONNECTED) {
            Logging::GetInstance()->Get(FRAMEWORK_INNER_NETWORKING)->debug("Cannot connect an already connected instance");
            return CLIENT_ALREADY_CONNECTED;
        }

        if (!_peer) {
            return CLIENT_PEER_NULL;
        }

        _state                                = CONNECTING;
        
        SLNet::ConnectionAttemptResult result = _peer->Connect(host.c_str(), port, password.c_str(), password.length());
        if (result != SLNet::CONNECTION_ATTEMPT_STARTED) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Failed to connect to the remote host");
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
        OPTICK_EVENT();

        if (!_peer) {
            return;
        }

        if (_state != CONNECTING && _state != CONNECTED) {
            return;
        }

        for (_packet = _peer->Receive(); _packet; _peer->DeallocatePacket(_packet), _packet = _peer->Receive()) {
            int offset       = 0;
            SLNet::TimeMS TS = 0;
            if (_packet->data[0] == ID_TIMESTAMP) {
                SLNet::BitStream timestamp(_packet->data + 1, sizeof(SLNet::TimeMS) + 1, false);
                timestamp.Read(TS);
                offset = 1 + sizeof(SLNet::TimeMS);
            }

            switch (_packet->data[offset]) {
            case Messages::GAME_CONNECTION_FINALIZED: {
                _state = CONNECTED;
                if (_onPlayerConnectionFinalizedCallback) {
                    _onPlayerConnectionFinalizedCallback(_packet);
                }
            } break;

            case ID_CONNECTION_REQUEST_ACCEPTED: {
                if (_onPlayerConnectionAcceptedCallback) {
                    _onPlayerConnectionAcceptedCallback(_packet);
                }
            } break;

            case ID_NO_FREE_INCOMING_CONNECTIONS: {
                if (_onPlayerDisconnectedCallback) {
                    _onPlayerDisconnectedCallback(_packet, Messages::NO_FREE_SLOT);
                }
            } break;

            case ID_DISCONNECTION_NOTIFICATION: {
                if (_onPlayerDisconnectedCallback) {
                    _onPlayerDisconnectedCallback(_packet, Messages::GRACEFUL_SHUTDOWN);
                }
            } break;

            case ID_CONNECTION_LOST: {
                if (_onPlayerDisconnectedCallback) {
                    _onPlayerDisconnectedCallback(_packet, Messages::LOST);
                }
            } break;

            case ID_CONNECTION_ATTEMPT_FAILED: {
                if (_onPlayerDisconnectedCallback) {
                    _onPlayerDisconnectedCallback(_packet, Messages::FAILED);
                }
            } break;

            case ID_INVALID_PASSWORD: {
                if (_onPlayerDisconnectedCallback) {
                    _onPlayerDisconnectedCallback(_packet, Messages::INVALID_PASSWORD);
                }
            } break;

            case ID_CONNECTION_BANNED: {
                if (_onPlayerDisconnectedCallback) {
                    _onPlayerDisconnectedCallback(_packet, Messages::BANNED);
                }
            } break;

            case Messages::GAME_SYNC: {
                SLNet::BitStream bsIn(_packet->data, _packet->length, false);
                bsIn.IgnoreBytes(sizeof(SLNet::MessageID));

                SLNet::MessageID innerSyncMessage;
                bsIn.Read(innerSyncMessage);

                if (_registeredMessageCallbacks.find(innerSyncMessage) != _registeredMessageCallbacks.end()) {
                    _registeredMessageCallbacks[innerSyncMessage](&bsIn);
                } else {
                    Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->warn("Received unknown game sync message {}", innerSyncMessage);
                }
            } break;

            default: {
                Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Received unknown packet {}", _packet->data[offset]);
            } break;
            }
        }
    }

    int NetworkClient::GetPing() {
        if (!_peer) {
            return 0;
        }

        return _peer->GetAveragePing(SLNet::UNASSIGNED_RAKNET_GUID);
    }
} // namespace Framework::Networking
