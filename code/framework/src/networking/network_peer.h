/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages/message.h"
#include "messages/messages.h"

#include <PacketPriority.h>
#include <RakPeerInterface.h>
#include <unordered_map>

namespace Framework::Networking {
    class NetworkPeer {
      protected:
        SLNet::RakPeerInterface *_peer = nullptr;
        SLNet::Packet *_packet         = nullptr;
        std::unordered_map<uint32_t, Messages::MessageCallback> _registeredMessageCallbacks;

      public:
        bool Send(Messages::IMessage &msg, SLNet::RakNetGUID guid = SLNet::UNASSIGNED_RAKNET_GUID, PacketPriority priority = HIGH_PRIORITY,
            PacketReliability reliability = RELIABLE_ORDERED) {
            if (!_peer) {
                return false;
            }

            SLNet::BitStream bsOut;
            bsOut.Write((SLNet::MessageID)msg.GetMessageID());

            msg.ToBitStream(&bsOut);

            if (_peer->Send(&bsOut, priority, reliability, 0, guid, guid == SLNet::UNASSIGNED_RAKNET_GUID) <= 0) {
                return false;
            }

            return true;
        }

        void RegisterMessage(uint32_t message, Messages::MessageCallback callback) {
            if (callback == nullptr) {
                return;
            }

            _registeredMessageCallbacks[message] = callback;
        }

        SLNet::Packet *GetPacket() const {
            return _packet;
        }

        SLNet::RakPeerInterface *GetPeer() const {
            return _peer;
        }
    };
} // namespace Framework::Networking
