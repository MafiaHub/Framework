/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>

#include "connection.h"
#include "errors.h"
#include "network_peer.h"
#include "rpc/client_identity.h"
#include "rpc/rpc.h"

#include <utils/error.h>
#include <utils/result.h>

#include <mafianet/peerinterface.h>
#include <mafianet/types.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Framework::Networking {
    using ClientGuidCallback = fu2::function<void(MafiaNet::RakNetGUID) const>;

    class NetworkServer final: public NetworkPeer {
      private:
        PacketCallback _onPlayerConnectCallback;
        DisconnectPacketCallback _onPlayerDisconnectCallback;
        ClientGuidCallback _onClientAuthenticatedCallback;
        MafiaNet::FileListTransfer _fileListTransfer;

        // Guids whose build challenge passed — the gate keeping unverified peers out of replication.
        std::unordered_set<uint64_t> _authenticatedClients;
        std::unordered_set<uint64_t> _acceptedClients;
        std::unordered_set<uint64_t> _readyClients;

        // Identities announced via ClientIdentity, kept for the connection's lifetime.
        std::unordered_map<uint64_t, RPC::ClientIdentity> _peerIdentities;

        std::unordered_map<DisconnectionReason, Metrics::Counter *> _kickCounters;
        std::unordered_map<DisconnectionReason, Metrics::Counter *> _closedCounters;
        Metrics::Counter *_connFailVersion        = nullptr;
        Metrics::Counter *_connFailReady          = nullptr;
        Metrics::Counter *_connectionsAccepted    = nullptr;
        Metrics::Counter *_connectionsReady       = nullptr;
        Metrics::Gauge *_authenticatedConnections = nullptr;
        Metrics::Gauge *_readyConnections         = nullptr;

        void InitMetrics();
        void ClearClientState(MafiaNet::RakNetGUID guid);
        void RecordConnectionClosed(MafiaNet::RakNetGUID guid, DisconnectionReason reason);

      public:
        NetworkServer(): NetworkPeer() {}

        [[nodiscard]] Utils::Result<void, Error> Init(const std::string &host, int32_t port, int32_t maxPlayers, const std::string &password = "");
        void Shutdown() override;

        bool HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) override;

        // Signal an RPC to every connected system except one (typically the originator) — the
        // server-authoritative relay primitive, since RPC4::Signal has no exclusion parameter. The
        // bitstream holds the already-written RPC arguments.
        void SignalExcept(const char *identifier, MafiaNet::BitStream &bs, MafiaNet::RakNetGUID excludeGUID, MafiaNet::Priority priority = MafiaNet::Priority::High, MafiaNet::Reliability reliability = MafiaNet::Reliability::ReliableOrdered);

        int GetPing(MafiaNet::RakNetGUID guid) const override;
        std::string GetAddress(MafiaNet::RakNetGUID guid) const override;

        bool IsAuthenticated(MafiaNet::RakNetGUID guid) const {
            return _authenticatedClients.contains(guid.g);
        }

        void MarkClientReady(MafiaNet::RakNetGUID guid);

        // Start replicating to an authenticated peer (idempotent). Replication begins for no peer
        // until this is called — connections are not auto-managed (see Init).
        void PushReplicationConnection(MafiaNet::RakNetGUID guid);

        // Send a Kick RPC then close the connection.
        void KickPlayer(MafiaNet::RakNetGUID guid, DisconnectionReason reason, const std::string &customReason = "") override;

        void SetPeerIdentity(MafiaNet::RakNetGUID guid, const RPC::ClientIdentity &identity) {
            _peerIdentities[guid.g] = identity;
        }

        const RPC::ClientIdentity *GetPeerIdentity(MafiaNet::RakNetGUID guid) const override {
            const auto it = _peerIdentities.find(guid.g);
            return it != _peerIdentities.end() ? &it->second : nullptr;
        }

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
