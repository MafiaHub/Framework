#include "network_server.h"

#include <BitStream.h>
#include <MessageIdentifiers.h>
#include <logging/logger.h>

namespace Framework::Networking {
    ServerError NetworkServer::Init(int32_t port, std::string &host, int32_t maxPlayers, std::string &password) {
        _peer = SLNet::RakPeerInterface::GetInstance();

        SLNet::SocketDescriptor newSocketSd = SLNet::SocketDescriptor(port, host.c_str());
        if (_peer->Startup(maxPlayers, &newSocketSd, 1) != SLNet::RAKNET_STARTED) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Failed to init the networking peer");
            return SERVER_PEER_FAILED;
        }

        if (password.length() > 0) {
            _peer->SetIncomingPassword(password.c_str(), password.length());
            Logging::GetInstance()->Get(FRAMEWORK_INNER_NETWORKING)->debug("Applying incoming password to networking peer");
        }

        _peer->SetMaximumIncomingConnections(maxPlayers);
        return SERVER_NONE;
    }

    void NetworkServer::Update() {
        for (_packet = _peer->Receive(); _packet; _peer->DeallocatePacket(_packet), _packet = _peer->Receive()) {
            int offset       = 0;
            SLNet::TimeMS TS = 0;

            // If it's a timestamp, fix the offset
            if (_packet->data[0] == ID_TIMESTAMP) {
                SLNet::BitStream timestamp(_packet->data + 1, sizeof(SLNet::TimeMS) + 1, false);
                timestamp.Read(TS);
                offset = 1 + sizeof(SLNet::TimeMS);
            }

            // Then break on every single message, and call registered callbacks if required
            switch (_packet->data[offset]) {
            case ID_NEW_INCOMING_CONNECTION: {
                if (_onNewIncomingConnection) {
                    _onNewIncomingConnection(_packet);
                }
            } break;

            case ID_DISCONNECTION_NOTIFICATION:
            case ID_CONNECTION_LOST: {
                if (_onPlayerDisconnectCallback) {
                    _onPlayerDisconnectCallback(_packet);
                }
            } break;

            case Messages::GAME_CONNECTION_HANDSHAKE: {
                if (_onPlayerHandshakeCallback) {
                    _onPlayerHandshakeCallback(_packet);
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

            default: Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Received unknown packet {}", _packet->data[offset]);
            }
        }
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
