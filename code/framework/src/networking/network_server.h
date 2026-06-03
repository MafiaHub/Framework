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

      public:
        NetworkServer(): NetworkPeer() {}

        [[nodiscard]] NetworkPeerError Init(const std::string &host, int32_t port, int32_t maxPlayers, const std::string &password = "");
        void Shutdown() override;

        bool HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) override;

        // Signal an RPC to every connected system except one (typically the originator) — the
        // server-authoritative relay primitive, since RPC4::Signal has no exclusion parameter. The
        // bitstream holds the already-written RPC arguments.
        void SignalExcept(const char *identifier, MafiaNet::BitStream &bs, MafiaNet::RakNetGUID excludeGUID, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED);

        int GetPing(MafiaNet::RakNetGUID guid) const;

        void SetOnPlayerConnectCallback(Messages::PacketCallback callback) {
            _onPlayerConnectCallback = std::move(callback);
        }

        void SetOnPlayerDisconnectCallback(Messages::DisconnectPacketCallback callback) {
            _onPlayerDisconnectCallback = std::move(callback);
        }
    };
} // namespace Framework::Networking
