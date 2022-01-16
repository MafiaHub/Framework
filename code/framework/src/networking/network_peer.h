/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages/messages.h"

#include <PacketPriority.h>
#include <RakPeerInterface.h>
#include <unordered_map>
#include <utils/hashing.h>

namespace Framework::Networking {
    class NetworkPeer {
      protected:
        SLNet::RakPeerInterface *_peer = nullptr;
        SLNet::Packet *_packet         = nullptr;
        std::unordered_map<uint32_t, Messages::PacketCallback> _registeredRPCs;
        std::unordered_map<uint8_t, Messages::PacketCallback> _registeredMessageCallbacks;
        Messages::PacketCallback _onUnknownPacketCallback;

      public:
        NetworkPeer();

        bool Send(Messages::IMessage &msg, SLNet::RakNetGUID guid = SLNet::UNASSIGNED_RAKNET_GUID, PacketPriority priority = HIGH_PRIORITY,
            PacketReliability reliability = RELIABLE_ORDERED);

        bool Send(Messages::IMessage &msg, uint64_t guid = (uint64_t)-1, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED);

        void RegisterMessage(uint8_t message, Messages::PacketCallback callback);

        template <typename T>
        void RegisterMessage(uint8_t message, std::function<void(SLNet::RakNetGUID, T *)> callback) {
            if (callback == nullptr) {
                return;
            }

            _registeredMessageCallbacks[message] = [callback](SLNet::Packet *p) {
                SLNet::BitStream bs(p->data + 1, p->length, false);
                T msg = {};
                msg.SetPacket(p);
                msg.Serialize(&bs, false);
                callback(p->guid, &msg);
            };
        }

        template <typename T>
        void RegisterRPC(std::function<void(T *)> callback) {
            T _rpc = {};

            if (callback == nullptr) {
                return;
            }

            _registeredRPCs[_rpc.GetHashName()] = [callback](SLNet::Packet *p) {
                SLNet::BitStream bs(p->data + 5, p->length, false);
                T rpc = {};
                rpc.SetPacket(p);
                rpc.Serialize(&bs, false);
                callback(&rpc);
            };
        }

        template <typename T>
        bool SendRPC(T &rpc, SLNet::RakNetGUID guid = SLNet::UNASSIGNED_RAKNET_GUID, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED) {
            SLNet::BitStream bs;
            bs.Write(Messages::INTERNAL_RPC);
            bs.Write(rpc.GetHashName());
            rpc.Serialize(&bs, true);

            if (_peer->Send(&bs, priority, reliability, 0, guid, guid == SLNet::UNASSIGNED_RAKNET_GUID) <= 0) {
                return false;
            }

            return true;
        }

        void Update();
        virtual bool HandlePacket(uint8_t packetID, SLNet::Packet *packet) = 0;

        void SetUnknownPacketHandler(Messages::PacketCallback callback) {
            _onUnknownPacketCallback = callback;
        }

        SLNet::Packet *GetPacket() const {
            return _packet;
        }

        SLNet::RakPeerInterface *GetPeer() const {
            return _peer;
        }

        static const char *GetStartupResultString(uint8_t id);
        static const char *GetConnectionAttemptString(uint8_t id);
    };
} // namespace Framework::Networking
