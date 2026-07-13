/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "connection.h"
#include "rpc/rpc.h"

#include <mafianet/MessageIdentifiers.h>
#include <mafianet/PacketPriority.h>
#include <mafianet/peerinterface.h>
#include <mafianet/FileListTransfer.h>
#include <mafianet/DirectoryDeltaTransfer.h>
#include <mafianet/RPC4Plugin.h>
#include <mafianet/ReadyEvent.h>
#include <mafianet/StatisticsHistory.h>
#include <mafianet/TwoWayAuthentication.h>
#include <mafianet/NetworkIDManager.h>
#include <logging/logger.h>
#include <utils/lifecycle.h>
#include <memory>
#include <string>
#include <utility>
#include <utils/hashing.h>
#include <vector>

namespace Framework::Networking::Replication {
    class ReplicationManager;
} // namespace Framework::Networking::Replication

namespace Framework::Networking::RPC {
    struct ClientIdentity;
} // namespace Framework::Networking::RPC

namespace Framework::Networking {
    class NetworkPeer : public Lifecycle {
      protected:
        MafiaNet::RakPeerInterface *_peer = nullptr;
        MafiaNet::Packet *_packet         = nullptr;
        int _packetDataOffset          = 0; // Offset to skip timestamp prefix if present
        PacketCallback _onUnknownPacketCallback;
        mutable MafiaNet::DirectoryDeltaTransfer _assetStreamer;

        // RPC4 dispatches remote-procedure calls by identifier to C handlers. NetworkIDManager hands
        // out the cross-network object handles used by replicas. StatisticsHistoryPlugin tracks
        // per-connection bandwidth/RTT/loss.
        MafiaNet::RPC4 _rpc;
        MafiaNet::NetworkIDManager _networkIDManager;
        MafiaNet::StatisticsHistoryPlugin _statisticsHistory;

        // Connection gate: TwoWayAuthentication proves an identical build token without sending it;
        // ReadyEvent is the per-connection spawn barrier. Flow in network_{server,client}.cpp.
        MafiaNet::TwoWayAuthentication _twoWayAuth;
        MafiaNet::ReadyEvent _readyEvent;
        std::string _buildToken;
        // Token currently registered with TwoWayAuthentication, so an unchanged re-registration can
        // be skipped instead of clearing the plugin (which drops in-flight challenges).
        std::string _registeredToken;

        // Owns the replicated entity world. The concrete peer's Init() attaches it and sets its role.
        std::unique_ptr<Replication::ReplicationManager> _replicationManager;

        // Decoded RPC handlers registered via RegisterRPC<T>. Each is kept alive here for the peer's
        // lifetime; its address is the context RPC4 hands back to DispatchRPC, so handlers can capture.
        using RPCSlot = fu2::function<void(MafiaNet::BitStream *, MafiaNet::Packet *)>;
        std::vector<std::unique_ptr<RPCSlot>> _rpcHandlers;

        static void DispatchRPC(MafiaNet::BitStream *bs, MafiaNet::Packet *packet, void *context) {
            auto *slot = static_cast<RPCSlot *>(context);
            if (slot && *slot) {
                (*slot)(bs, packet);
            }
        }

        bool RegisterBuildToken();

      public:
        // TwoWayAuthentication identifier under which the build token is registered/challenged.
        static constexpr const char *kBuildChallengeId = "Framework::Build";

        // Fixed token both peers register when verifyBuildToken is off; challenge still passes.
        static constexpr const char *kBuildVerificationDisabledToken = "Framework::BuildVerificationDisabled";

        NetworkPeer();
        ~NetworkPeer();

        // Single source of truth for the gated build identity — must produce the same string on both
        // peers or the challenge fails.
        static std::string BuildToken(const std::string &gameName, const std::string &gameVersion, const std::string &fwVersion, const std::string &modVersion) {
            return gameName + '|' + gameVersion + '|' + fwVersion + '|' + modVersion;
        }

        // Register the local build token (see BuildToken). Call before connecting/accepting.
        void SetBuildToken(const std::string &token);

        // Register a handler for RPC payload type T (see networking/rpc/rpc.h). The handler receives
        // the already-decoded payload and the raw packet, and may capture (e.g. the owning instance).
        // A decode wrapper around RegisterRawRPC below, which owns the slot mechanics. Matches the
        // Signal() send below.
        template <RPC::Payload T>
        void RegisterRPC(fu2::function<void(const T &payload, MafiaNet::Packet *packet) const> handler) {
            RegisterRawRPC(T::kIdentifier, [cb = std::move(handler)](MafiaNet::BitStream *bs, MafiaNet::Packet *packet) {
                cb(RPC::Read<T>(bs), packet);
            });
        }

        // Send an RPC payload to every connected system, optionally excluding one (in broadcast mode
        // RakNet treats the guid as the system to SKIP — e.g. don't echo a client's own update back).
        template <RPC::Payload T>
        void BroadcastRPC(T &payload, MafiaNet::Priority priority = MafiaNet::Priority::High, MafiaNet::Reliability reliability = MafiaNet::Reliability::ReliableOrdered, MafiaNet::RakNetGUID except = MafiaNet::UNASSIGNED_RAKNET_GUID) {
            MafiaNet::BitStream bs;
            payload.Serialize(&bs, true);
            _rpc.Signal(T::kIdentifier, &bs, priority, reliability, 0, except, true, false);
        }

        // Send an RPC payload to a single system.
        template <RPC::Payload T>
        void SendRPC(T &payload, MafiaNet::RakNetGUID guid, MafiaNet::Priority priority = MafiaNet::Priority::High, MafiaNet::Reliability reliability = MafiaNet::Reliability::ReliableOrdered) {
            MafiaNet::BitStream bs;
            payload.Serialize(&bs, true);
            _rpc.Signal(T::kIdentifier, &bs, priority, reliability, 0, guid, false, false);
        }

        // Raw variant of RegisterRPC for handlers that decode the bitstream themselves (e.g. a
        // polymorphic tail). Prefer the typed RegisterRPC<T> for fixed-shape payloads. The callable
        // is stored for the peer's lifetime and reached through RPC4's per-slot context, so no
        // file-static handler pointers are needed.
        void RegisterRawRPC(const char *identifier, fu2::function<void(MafiaNet::BitStream *, MafiaNet::Packet *)> handler) {
            auto slot     = std::make_unique<RPCSlot>(std::move(handler));
            void *context = slot.get();
            _rpcHandlers.push_back(std::move(slot));
            _rpc.RegisterSlot(identifier, &NetworkPeer::DispatchRPC, context, 0);
        }

        // Send a pre-encoded bitstream under a raw identifier (pairs with RegisterRawRPC).
        void SendRawRPC(const char *identifier, MafiaNet::BitStream &bs, MafiaNet::RakNetGUID guid, MafiaNet::Priority priority = MafiaNet::Priority::High, MafiaNet::Reliability reliability = MafiaNet::Reliability::ReliableOrdered) {
            _rpc.Signal(identifier, &bs, priority, reliability, 0, guid, false, false);
        }

        void Update() override;
        virtual bool HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) = 0;

        // Byte offset of the packet id in a datagram, skipping an optional ID_TIMESTAMP + 8-byte
        // MafiaNet::Time prefix. Returns -1 if too short. Pure + tested so the skip width can't drift
        // from what RakNet writes (a wrong width reads the id mid-timestamp — phantom control packets).
        static int ResolvePacketDataOffset(const uint8_t *data, uint32_t length);

        // True for ReplicaManager3 message ids. The plugin fully processes these but intentionally
        // returns RR_CONTINUE_PROCESSING so applications can observe replication traffic; Update()
        // uses this to keep them off the unknown-packet path. A subclass that wants one (e.g.
        // ID_REPLICA_MANAGER_DOWNLOAD_COMPLETE) can still claim it in its HandlePacket switch.
        static bool IsReplicationPacket(uint8_t packetID);

        // Server-only; base no-op lets shared code kick through a NetworkPeer* without a cast.
        virtual void KickPlayer(MafiaNet::RakNetGUID, DisconnectionReason, const std::string & = "") {}

        // Server-only; the identity the peer announced via ClientIdentity (unverified), else nullptr.
        virtual const RPC::ClientIdentity *GetPeerIdentity(MafiaNet::RakNetGUID) const {
            return nullptr;
        }

        // Server-only; the connection's average round-trip time in ms, or -1 if unavailable.
        virtual int GetPing(MafiaNet::RakNetGUID) const {
            return -1;
        }

        // Server-only; the connection's remote IP address (no port), or empty if unavailable.
        virtual std::string GetAddress(MafiaNet::RakNetGUID) const {
            return "";
        }

        void SetUnknownPacketHandler(PacketCallback callback) {
            _onUnknownPacketCallback = std::move(callback);
        }

        MafiaNet::Packet *GetPacket() const noexcept {
            return _packet;
        }

        int GetPacketDataOffset() const noexcept {
            return _packetDataOffset;
        }

        // --- Escape hatches ---
        // The accessors below hand out the raw MafiaNet objects for features the framework doesn't
        // wrap. Prefer the typed RPC API above (RegisterRPC<T>/BroadcastRPC<T>/SendRPC<T>) and
        // ReplicationManager's typed entity accessors; reach for these only when those don't cover it.
        MafiaNet::RakPeerInterface *GetPeer() const noexcept {
            return _peer;
        }

        static const char *GetStartupResultString(uint8_t id);
        static const char *GetConnectionAttemptString(uint8_t id);

        MafiaNet::DirectoryDeltaTransfer* GetAssetStreamer() const noexcept {
            return &_assetStreamer;
        }

        MafiaNet::RPC4 *GetRPC() noexcept {
            return &_rpc;
        }

        MafiaNet::NetworkIDManager *GetNetworkIDManager() noexcept {
            return &_networkIDManager;
        }

        MafiaNet::StatisticsHistoryPlugin *GetStatisticsHistory() noexcept {
            return &_statisticsHistory;
        }

        MafiaNet::TwoWayAuthentication *GetTwoWayAuth() noexcept {
            return &_twoWayAuth;
        }

        MafiaNet::ReadyEvent *GetReadyEvent() noexcept {
            return &_readyEvent;
        }

        Replication::ReplicationManager *GetReplicationManager() const noexcept {
            return _replicationManager.get();
        }
    };
} // namespace Framework::Networking
