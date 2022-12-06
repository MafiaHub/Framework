/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>

#include "errors.h"
#include "messages/messages.h"
#include "network_peer.h"
#include "world/server.h"

#include <RakNetTypes.h>
#include <RakPeerInterface.h>
#include <string>
#include <unordered_map>
#include <utility>

namespace Framework::Networking {
    class NetworkServer: public NetworkPeer {
      private:
        Messages::PacketCallback _onPlayerConnectCallback;
        Messages::DisconnectPacketCallback _onPlayerDisconnectCallback;
        bool SendGameRPCInternal(SLNet::BitStream &bs, Framework::World::ServerEngine *world, flecs::entity_t ent, SLNet::RakNetGUID guid = SLNet::UNASSIGNED_RAKNET_GUID, SLNet::RakNetGUID excludeGUID = SLNet::UNASSIGNED_RAKNET_GUID, PacketPriority priority = HIGH_PRIORITY,
            PacketReliability reliability = RELIABLE_ORDERED);

      public:
        NetworkServer(): NetworkPeer() {}

        ServerError Init(int32_t port, const std::string &host, int32_t maxPlayers, const std::string &password = "");
        ServerError Shutdown();

        bool HandlePacket(uint8_t packetID, SLNet::Packet *packet) override;

        template <typename T>
        bool SendGameRPC(Framework::World::ServerEngine *world, T &rpc, SLNet::RakNetGUID guid = SLNet::UNASSIGNED_RAKNET_GUID, SLNet::RakNetGUID excludeGUID = SLNet::UNASSIGNED_RAKNET_GUID, PacketPriority priority = HIGH_PRIORITY,
            PacketReliability reliability = RELIABLE_ORDERED) {
            SLNet::BitStream bs;
            bs.Write(Messages::INTERNAL_RPC);
            bs.Write(rpc.GetHashName());
            rpc.Serialize(&bs, true);
            rpc.Serialize2(&bs, true);
            assert(rpc.IsGameRPC() && "Regular RPCs cannot be sent via SendGameRPC()");

            return SendGameRPCInternal(bs, world, rpc.GetServerID(), guid, excludeGUID, priority, reliability);
        }

        int GetPing(SLNet::RakNetGUID guid);

        void SetOnPlayerConnectCallback(Messages::PacketCallback callback) {
            _onPlayerConnectCallback = std::move(callback);
        }

        void SetOnPlayerDisconnectCallback(Messages::DisconnectPacketCallback callback) {
            _onPlayerDisconnectCallback = std::move(callback);
        }
    };
} // namespace Framework::Networking
