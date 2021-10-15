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

            if (packetID < ID_USER_PACKET_ENUM) {
                if (_registeredInternalMessageCallbacks.find(packetID) != _registeredInternalMessageCallbacks.end()) {
                    _registeredInternalMessageCallbacks[packetID](_packet);
                    return;
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
                    return;
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
