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
#include "rpc/client_identity.h"
#include "rpc/rpc.h"

#include <utils/error.h>
#include <utils/result.h>

#include <mafianet/IncrementalReadInterface.h>
#include <mafianet/types.h>
#include <mafianet/peerinterface.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Framework::Networking {
    using ClientGuidCallback = fu2::function<void(MafiaNet::RakNetGUID) const>;
    using ConnectionReadyCallback = fu2::function<void(int, MafiaNet::RakNetGUID) const>;
    // A connection request awaiting a decision. identity is nullopt when the request carried no
    // framework identity (another build, or not a framework client); the request is still the
    // callee's to answer.
    using SessionRequestCallback = fu2::function<void(MafiaNet::RakNetGUID, const std::optional<RPC::ClientIdentity> &) const>;

    // How the server holds connections while the admission gate decides on them. Waiting
    // connections live in a pool of their own (MafiaNet's pending sessions): they take no player
    // slot and are not counted as connected, so a flood of them fills the pool, never the server.
    struct AdmissionSettings {
        // Connection requests that may wait for a decision at once, on top of maxPlayers.
        uint16_t pendingConnections = 32;
        // Of those, how many one IP address may hold. 0: no per-address bound.
        uint16_t pendingConnectionsPerAddress = 4;
        // How long MafiaNet keeps an undecided request before dropping it. Every status sent to the
        // client restarts it. The gate decides well within this; it is the backstop for a gate that
        // never answers.
        uint32_t sessionTimeoutMs = 60000;
    };

    class NetworkServer final: public NetworkPeer {
      private:
        PacketCallback _onPlayerConnectCallback;
        std::string _sessionConfig;
        DisconnectPacketCallback _onPlayerDisconnectCallback;
        ClientGuidCallback _onClientAuthenticatedCallback;
        ConnectionReadyCallback _onConnectionReadyCallback;
        MafiaNet::FileListTransfer _fileListTransfer;

        // Without this the streamer pushes whole files and the client only gets OnFile at completion.
        MafiaNet::IncrementalReadInterface _assetReader;

        SessionRequestCallback _onSessionRequestCallback;
        ClientGuidCallback _onSessionAbandonedCallback;

        // Guids whose build challenge passed — the gate keeping unverified peers out of replication.
        std::unordered_set<uint64_t> _authenticatedClients;

        // Identities from the session request, kept for the connection's lifetime.
        std::unordered_map<uint64_t, RPC::ClientIdentity> _peerIdentities;

        void ClearClientState(MafiaNet::RakNetGUID guid);

      public:
        NetworkServer(): NetworkPeer() {}

        [[nodiscard]] Utils::Result<void, Error> Init(const std::string &host, int32_t port, int32_t maxPlayers, const std::string &password = "", const AdmissionSettings &admission = {});
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

        // Answer a connection request. Accepting sends the session payload (SetSessionConfig) and
        // makes the connection real on both sides; refusing ends the attempt, and the client reads
        // the reason as written. Either is ignored for a request that is no longer waiting.
        void AcceptSession(MafiaNet::RakNetGUID guid);
        void RejectSession(MafiaNet::RakNetGUID guid, const std::string &reason);

        // A line for a client whose request is still waiting; restarts its session timeout on both
        // ends. Ignored once the request is decided or gone.
        void SendSessionStatus(MafiaNet::RakNetGUID guid, const std::string &status);

        // Connected players, which is what maxPlayers bounds. Waiting requests are not among them.
        uint32_t GetPlayerCount() const;

        // Start replicating to an authenticated peer (idempotent). Replication begins for no peer
        // until this is called — connections are not auto-managed (see Init).
        void PushReplicationConnection(MafiaNet::RakNetGUID guid);

        // Payload MafiaNet answers every connection request with, before either side reports a
        // connection. Opaque bytes; set it before the first client connects. False (and unpublished)
        // if it exceeds MAXIMUM_SESSION_CONFIG_SIZE.
        bool SetSessionConfig(std::string payload);

        const std::string &GetSessionConfig() const noexcept {
            return _sessionConfig;
        }

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

        // A client asked to join. The request waits, holding no player slot, until AcceptSession or
        // RejectSession answers it -- or until it is abandoned.
        void SetOnSessionRequestCallback(SessionRequestCallback callback) {
            _onSessionRequestCallback = std::move(callback);
        }

        // A request that was waiting went away unanswered: the client left, or the session timed out.
        void SetOnSessionAbandonedCallback(ClientGuidCallback callback) {
            _onSessionAbandonedCallback = std::move(callback);
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

        void SetOnConnectionReadyCallback(ConnectionReadyCallback callback) {
            _onConnectionReadyCallback = std::move(callback);
        }
    };
} // namespace Framework::Networking
