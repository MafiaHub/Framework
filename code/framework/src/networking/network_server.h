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
#include "sender.h"

#include <utils/error.h>
#include <utils/result.h>

#include <atomic>
#include <chrono>
#include <mafianet/peer_interface.h>
#include <mafianet/types.h>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Framework::Networking::Replication {
    class World;
} // namespace Framework::Networking::Replication

namespace Framework::Networking {
    using ClientGuidCallback      = fu2::function<void(MafiaNet::RakNetGUID) const>;
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

        SessionRequestCallback _onSessionRequestCallback;
        ClientGuidCallback _onSessionAbandonedCallback;

        // What is kept per connection. Packets write it where they are handled, and any thread asks
        // (a script reading a player's identity, a kick): all of it under this mutex, which is taken
        // last and never held across a call out of this class.
        mutable std::mutex _clientsMutex;

        // Guids whose build challenge passed — the gate keeping unverified peers out of replication.
        std::unordered_set<uint64_t> _authenticatedClients;

        // Identities from the session request, kept for the connection's lifetime.
        std::unordered_map<uint64_t, RPC::ClientIdentity> _peerIdentities;

        // Ready events of connections that were kicked, for the thread that handles packets to
        // delete before its next one: ReadyEvent belongs to that thread.
        std::vector<int> _staleReadyEvents;
        std::atomic<bool> _anyStaleReadyEvents {false};
        void DeleteStaleReadyEvents();
        void BeforeReceive() override {
            DeleteStaleReadyEvents();
        }

        bool _replicationRPCsRegistered = false;

        std::atomic<int32_t> _slowHandlerWarnMs {10};
        // The draining thread's own: slow handlers since the last warning, and when the next may be written.
        uint64_t _slowHandlers = 0;
        std::chrono::steady_clock::time_point _nextSlowHandlerWarning {};
        void NoteHandlerTime(RPC::Identifier identifier, std::chrono::steady_clock::duration elapsed);
        // RPCs dropped for a payload that did not read whole since the last warning, which is written
        // at most once a second: a peer can send them as fast as it likes. The draining thread's own.
        uint64_t _shortPayloads = 0;
        std::chrono::steady_clock::time_point _nextShortPayloadWarning {};
        void NoteShortPayload(RPC::Identifier identifier, MafiaNet::RakNetGUID guid);

        void ClearClientState(MafiaNet::RakNetGUID guid);

      public:
        NetworkServer(): NetworkPeer() {}

        [[nodiscard]] Utils::Result<void, Error> Init(const std::string &host, int32_t port, int32_t maxPlayers, const std::string &password = "", const AdmissionSettings &admission = {});
        void Shutdown() override;

        // The tick's share of networking: interest, delegation, the state-bag flush and the
        // replication pass. It receives nothing: packets are handled by whoever calls DrainPackets,
        // a PacketThread on a running server.
        void Update() override;

        // A handler that takes at least this long is logged, at most once a second, with the digest
        // of its RPC identifier (the name is not in the binary). 0 turns the warning off.
        void SetSlowHandlerWarnMs(int32_t ms) {
            _slowHandlerWarnMs.store(ms, std::memory_order_relaxed);
        }

        bool HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) override;

        // Register a handler for RPC payload type T. It receives the decoded payload, the connection
        // that sent it and the world, through which it reaches entities by handle (see
        // replication/world.h). A payload the client did not send whole (truncated, or not what
        // T::Serialize writes: see RPC::ReadWhole) is dropped before the handler, counted and logged at
        // most once a second. Handlers run where packets are drained, on a server its packet
        // thread, one at a time; register them before it starts (an instance's PostInit or
        // PostScriptInit), since the table they go in is read there without a lock.
        template <RPC::Payload T>
        void RegisterRPC(fu2::function<void(const T &payload, Sender sender, Replication::World &world) const> handler) {
            RegisterRawRPC(T::kIdentifier, [this, cb = std::move(handler)](MafiaNet::BitStream *bs, Sender sender, Replication::World &world) {
                T payload {};
                if (!RPC::ReadWhole(bs, payload)) {
                    NoteShortPayload(T::kIdentifier, sender.Connection());
                    return;
                }
                cb(payload, sender, world);
            });
        }

        // Raw variant for handlers that decode the bitstream themselves, and so answer for what a short
        // or hostile one decodes to: every BitStream read says whether it got its bits.
        void RegisterRawRPC(RPC::Identifier identifier, fu2::function<void(MafiaNet::BitStream *, Sender, Replication::World &)> handler);

        // Signal an RPC to every connected system except one (typically the originator) — the
        // server-authoritative relay primitive, since RPC4::Signal has no exclusion parameter. The
        // bitstream holds the already-written RPC arguments.
        void SignalExcept(RPC::Identifier identifier, MafiaNet::BitStream &bs, MafiaNet::RakNetGUID excludeGUID, MafiaNet::Priority priority = MafiaNet::Priority::High, MafiaNet::Reliability reliability = MafiaNet::Reliability::ReliableOrdered, Channel channel = Channel::Events);

        int GetPing(MafiaNet::RakNetGUID guid) const override;
        std::string GetAddress(MafiaNet::RakNetGUID guid) const override;

        bool IsAuthenticated(MafiaNet::RakNetGUID guid) const {
            const std::lock_guard guard(_clientsMutex);
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
            const std::lock_guard guard(_clientsMutex);
            _peerIdentities[guid.g] = identity;
        }

        std::optional<RPC::ClientIdentity> GetPeerIdentity(MafiaNet::RakNetGUID guid) const override {
            const std::lock_guard guard(_clientsMutex);
            const auto it = _peerIdentities.find(guid.g);
            return it != _peerIdentities.end() ? std::optional<RPC::ClientIdentity>(it->second) : std::nullopt;
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

    template <RPC::Payload T>
    void Sender::Reply(T payload, MafiaNet::Priority priority, MafiaNet::Reliability reliability) const {
        _server->SendRPC(payload, _guid, priority, reliability);
    }
} // namespace Framework::Networking
