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
#include "rpc/rpc.h"
#include "world/server.h"

#include <mafianet/types.h>
#include <mafianet/peerinterface.h>
#include <string>
#include <unordered_map>
#include <utility>

namespace Framework::Networking {
    class NetworkServer: public NetworkPeer {
      private:
        Messages::PacketCallback _onPlayerConnectCallback;
        Messages::DisconnectPacketCallback _onPlayerDisconnectCallback;
        MafiaNet::FileListTransfer _fileListTransfer;

        // Signal a serialized RPC payload to every connected system except one, by targeting each
        // connection individually (RPC4::Signal has no exclusion parameter).
        void SignalExcept(const char *identifier, MafiaNet::BitStream &bs, MafiaNet::RakNetGUID excludeGUID, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED);

      public:
        NetworkServer(): NetworkPeer() {}

        [[nodiscard]] NetworkPeerError Init(const std::string &host, int32_t port, int32_t maxPlayers, const std::string &password = "");
        void Shutdown() override;

        bool HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) override;

        // Send an RPC payload to everyone except one system (typically the originator).
        template <typename T>
        void BroadcastRPCExcept(T &payload, MafiaNet::RakNetGUID excludeGUID, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED) {
            MafiaNet::BitStream bs;
            payload.Serialize(&bs, true);
            SignalExcept(T::kIdentifier, bs, excludeGUID, priority, reliability);
        }

        int GetPing(MafiaNet::RakNetGUID guid) const;

        void SetOnPlayerConnectCallback(Messages::PacketCallback callback) {
            _onPlayerConnectCallback = std::move(callback);
        }

        void SetOnPlayerDisconnectCallback(Messages::DisconnectPacketCallback callback) {
            _onPlayerDisconnectCallback = std::move(callback);
        }
    };
} // namespace Framework::Networking
