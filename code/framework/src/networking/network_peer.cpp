/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "network_peer.h"

#include "errors.h"

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
                _registeredRPCs[hashName](p);
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

    bool NetworkPeer::Send(Messages::IMessage &msg, uint64_t guid, PacketPriority priority, PacketReliability reliability) {
        return Send(msg, SLNet::RakNetGUID(guid), priority, reliability);
    }

    void NetworkPeer::RegisterMessage(uint8_t message, Messages::PacketCallback callback) {
        if (callback == nullptr) {
            return;
        }

        _registeredMessageCallbacks[message] = callback;
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
                if (_registeredMessageCallbacks.find(packetID) != _registeredMessageCallbacks.end()) {
                    _registeredMessageCallbacks[packetID](_packet);
                }
                else {
                    Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->trace("Received unknown packet {}", packetID);
                    if (_onUnknownPacketCallback) {
                        _onUnknownPacketCallback(_packet);
                    }
                }
            }
        }
    }

    const char *NetworkPeer::GetStartupResultString(uint8_t id) {
        return StartupResultString[id];
    }

    const char *NetworkPeer::GetConnectionAttemptString(uint8_t id) {
        return ConnectionAttemptString[id];
    }
} // namespace Framework::Networking
