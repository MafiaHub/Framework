/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "network_peer.h"

#include <logging/logger.h>
#include <optick.h>

namespace Framework::Networking {
    NetworkPeer::NetworkPeer() {
        _peer = SLNet::RakPeerInterface::GetInstance();

        RegisterMessage(Messages::INTERNAL_RPC, [&](SLNet::Packet *p) {
            SLNet::BitStream bs(p->data + 1, p->length, false);
            uint32_t hashName;
            bs.Read(hashName);

            if (_registeredRPCs.find(hashName) != _registeredRPCs.end()) {
                _registeredRPCs[hashName](&bs);
            }
        });
    }

    bool NetworkPeer::Send(Messages::IMessage &msg, SLNet::RakNetGUID guid, PacketPriority priority, PacketReliability reliability) {
        if (!_peer) {
            return false;
        }

        SLNet::BitStream bsOut;
        bsOut.Write(msg.GetMessageID());
        msg.Serialize(&bsOut, true);

        if (_peer->Send(&bsOut, priority, reliability, 0, guid, guid == SLNet::UNASSIGNED_RAKNET_GUID) <= 0) {
            return false;
        }

        return true;
    }

    void NetworkPeer::RegisterMessage(uint8_t message, Messages::PacketCallback callback) {
        if (callback == nullptr) {
            return;
        }

        _registeredInternalMessageCallbacks[message] = callback;
    }

    void NetworkPeer::Update() {
        OPTICK_EVENT();

        if (!_peer) {
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

            uint8_t packetID = _packet->data[offset];

            if (!HandlePacket(packetID, _packet)) {
                if (packetID < Messages::INTERNAL_NEXT_MESSAGE_ID) {
                    if (_registeredInternalMessageCallbacks.find(packetID) != _registeredInternalMessageCallbacks.end()) {
                        _registeredInternalMessageCallbacks[packetID](_packet);
                    }
                    else {
                        Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->warn("Received unknown internal sync message {}", packetID);
                        if (_onUnknownInternalPacketCallback) {
                            _onUnknownInternalPacketCallback(_packet);
                        }
                    }
                }
                else {
                    if (_registeredMessageCallbacks.find(packetID) != _registeredMessageCallbacks.end()) {
                        SLNet::BitStream bsIn(_packet->data + 1, _packet->length, false);
                        _registeredMessageCallbacks[packetID](&bsIn);
                    }
                    else {
                        Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->warn("Received unknown game sync message {}", packetID);
                        if (_onUnknownGamePacketCallback) {
                            _onUnknownGamePacketCallback(_packet);
                        }
                    }
                }
            }
        }
    }
}
