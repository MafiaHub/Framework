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
#include "connection.h"
#include "network_peer.h"
#include "rpc/rpc.h"

#include <mafianet/types.h>
#include <mafianet/peerinterface.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Framework::Networking {
    using ClientGuidCallback = fu2::function<void(MafiaNet::RakNetGUID) const>;

    class NetworkServer: public NetworkPeer {
      private:
        PacketCallback _onPlayerConnectCallback;
        DisconnectPacketCallback _onPlayerDisconnectCallback;
        ClientGuidCallback _onClientAuthenticatedCallback;
        MafiaNet::FileListTransfer _fileListTransfer;

        // Guids whose build challenge passed — the gate keeping unverified peers out of replication.
        std::unordered_set<uint64_t> _authenticatedClients;

        void ClearClientState(MafiaNet::RakNetGUID guid);

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

        bool IsAuthenticated(MafiaNet::RakNetGUID guid) const {
            return _authenticatedClients.contains(guid.g);
        }

        // Start replicating to an authenticated peer (idempotent). Replication begins for no peer
        // until this is called — connections are not auto-managed (see Init).
        void PushReplicationConnection(MafiaNet::RakNetGUID guid);

        // Send a Kick RPC then close the connection.
        void KickPlayer(MafiaNet::RakNetGUID guid, DisconnectionReason reason, const std::string &customReason = "") override;

        // Per-connection ReadyEvent id, derived from the slot so both ends agree without coordination.
        static int ReadyEventId(MafiaNet::RakNetGUID guid) {
            return static_cast<int>(guid.systemIndex);
        }

        void SetOnPlayerConnectCallback(PacketCallback callback) {
            _onPlayerConnectCallback = std::move(callback);
        }

        void SetOnPlayerDisconnectCallback(DisconnectPacketCallback callback) {
            _onPlayerDisconnectCallback = std::move(callback);
        }

        // Fired when a peer's build challenge succeeds (integration responds with ServerResources).
        void SetOnClientAuthenticatedCallback(ClientGuidCallback callback) {
            _onClientAuthenticatedCallback = std::move(callback);
        }
    };
} // namespace Framework::Networking
