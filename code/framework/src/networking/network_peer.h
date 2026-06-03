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
#include <utility>
#include <utils/hashing.h>
#include <vector>

namespace Framework::Networking::Replication {
    class ReplicationManager;
} // namespace Framework::Networking::Replication

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

      public:
        // TwoWayAuthentication identifier under which the build token is registered/challenged.
        static constexpr const char *kBuildChallengeId = "Framework::Build";

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
        // The callable is stored for the peer's lifetime and reached through RPC4's per-slot context,
        // so no file-static handler pointers are needed. Matches the Signal() send below.
        template <typename T>
        void RegisterRPC(fu2::function<void(const T &payload, MafiaNet::Packet *packet) const> handler) {
            auto slot = std::make_unique<RPCSlot>([cb = std::move(handler)](MafiaNet::BitStream *bs, MafiaNet::Packet *packet) {
                cb(RPC::Read<T>(bs), packet);
            });
            void *context = slot.get();
            _rpcHandlers.push_back(std::move(slot));
            _rpc.RegisterSlot(T::kIdentifier, &NetworkPeer::DispatchRPC, context, 0);
        }

        // Send an RPC payload to every connected system.
        template <typename T>
        void BroadcastRPC(T &payload, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED) {
            MafiaNet::BitStream bs;
            payload.Serialize(&bs, true);
            _rpc.Signal(T::kIdentifier, &bs, priority, reliability, 0, MafiaNet::UNASSIGNED_RAKNET_GUID, true, false);
        }

        // Send an RPC payload to a single system.
        template <typename T>
        void SendRPC(T &payload, MafiaNet::RakNetGUID guid, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED) {
            MafiaNet::BitStream bs;
            payload.Serialize(&bs, true);
            _rpc.Signal(T::kIdentifier, &bs, priority, reliability, 0, guid, false, false);
        }

        void Update() override;
        virtual bool HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) = 0;

        void SetUnknownPacketHandler(PacketCallback callback) {
            _onUnknownPacketCallback = std::move(callback);
        }

        MafiaNet::Packet *GetPacket() const noexcept {
            return _packet;
        }

        int GetPacketDataOffset() const noexcept {
            return _packetDataOffset;
        }

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
