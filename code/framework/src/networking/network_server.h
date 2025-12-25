/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
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
        SLNet::FileListTransfer _fileListTransfer;

        bool SendGameRPCInternal(SLNet::BitStream &bs, Framework::World::ServerEngine *world, flecs::entity_t ent, SLNet::RakNetGUID guid = SLNet::UNASSIGNED_RAKNET_GUID, SLNet::RakNetGUID excludeGUID = SLNet::UNASSIGNED_RAKNET_GUID, PacketPriority priority = HIGH_PRIORITY,
            PacketReliability reliability = RELIABLE_ORDERED) const;

      public:
        NetworkServer(): NetworkPeer() {}

        ServerError Init(int32_t port, const std::string &host, int32_t maxPlayers, const std::string &password = "");
        ServerError Shutdown() const;

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

        // Fluent send methods for Game RPCs
        template <typename T, typename... Args>
        bool sendGameRPC(Framework::World::ServerEngine* world, flecs::entity ent, Args&&... args) {
            T rpc(std::forward<Args>(args)...);
            rpc.SetServerID(ent.id());
            return SendGameRPC(world, rpc);
        }

        template <typename T, typename... Args>
        bool sendGameRPCTo(Framework::World::ServerEngine* world, SLNet::RakNetGUID guid,
                          flecs::entity ent, Args&&... args) {
            T rpc(std::forward<Args>(args)...);
            rpc.SetServerID(ent.id());
            return SendGameRPC(world, rpc, guid);
        }

        template <typename T, typename... Args>
        bool sendGameRPCExcept(Framework::World::ServerEngine* world, SLNet::RakNetGUID exceptGuid,
                              flecs::entity ent, Args&&... args) {
            T rpc(std::forward<Args>(args)...);
            rpc.SetServerID(ent.id());
            return SendGameRPC(world, rpc, SLNet::UNASSIGNED_RAKNET_GUID, exceptGuid);
        }

        int GetPing(SLNet::RakNetGUID guid) const;

        void SetOnPlayerConnectCallback(Messages::PacketCallback callback) {
            _onPlayerConnectCallback = std::move(callback);
        }

        void SetOnPlayerDisconnectCallback(Messages::DisconnectPacketCallback callback) {
            _onPlayerDisconnectCallback = std::move(callback);
        }
    };
} // namespace Framework::Networking
