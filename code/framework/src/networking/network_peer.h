/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages/messages.h"

#include <PacketPriority.h>
#include <RakPeerInterface.h>
#include <unordered_map>

namespace Framework::Networking {
    class NetworkPeer {
      protected:
        SLNet::RakPeerInterface *_peer = nullptr;
        SLNet::Packet *_packet         = nullptr;
        std::unordered_map<uint8_t, Messages::MessageCallback> _registeredMessageCallbacks;
        std::unordered_map<uint8_t, Messages::PacketCallback> _registeredInternalMessageCallbacks;
        Messages::PacketCallback _onUnknownInternalPacketCallback;
        Messages::PacketCallback _onUnknownGamePacketCallback;

      public:
        bool Send(Messages::IMessage &msg, SLNet::RakNetGUID guid = SLNet::UNASSIGNED_RAKNET_GUID, PacketPriority priority = HIGH_PRIORITY,
            PacketReliability reliability = RELIABLE_ORDERED) {
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

        template<typename T>
        void RegisterMessage(uint8_t message, std::function<void(T*)> callback) {
            if (callback == nullptr) {
                return;
            }

            _registeredMessageCallbacks[message] = [&](SLNet::BitStream *bs) {
                T msg;
                msg.Serialize(bs, false);
                callback(&msg);
            };
        }

        void RegisterMessage(uint8_t message, Messages::PacketCallback callback) {
            if (callback == nullptr) {
                return;
            }

            _registeredInternalMessageCallbacks[message] = callback;
        }

        void Update();
        virtual bool HandlePacket(uint8_t packetID, SLNet::Packet *packet) = 0;

        void SetUnknownInternalPacketHandler(Messages::PacketCallback callback) {
            _onUnknownInternalPacketCallback = callback;
        }

        void SetUnknownGamePacketHandler(Messages::PacketCallback callback) {
            _onUnknownGamePacketCallback = callback;
        }

        SLNet::Packet *GetPacket() const {
            return _packet;
        }

        SLNet::RakPeerInterface *GetPeer() const {
            return _peer;
        }
    };
} // namespace Framework::Networking
