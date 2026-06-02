/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages/messages.h"
#include "rpc/rpc.h"

#include <mafianet/PacketPriority.h>
#include <mafianet/peerinterface.h>
#include <mafianet/FileListTransfer.h>
#include <mafianet/DirectoryDeltaTransfer.h>
#include <mafianet/RPC4Plugin.h>
#include <mafianet/StatisticsHistory.h>
#include <mafianet/NetworkIDManager.h>
#include <logging/logger.h>
#include <utils/lifecycle.h>
#include <memory>
#include <unordered_map>
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
        std::unordered_map<uint8_t, Messages::PacketCallback> _registeredMessageCallbacks;
        Messages::PacketCallback _onUnknownPacketCallback;
        mutable MafiaNet::DirectoryDeltaTransfer _assetStreamer;

        // RPC4 dispatches remote-procedure calls by identifier to C handlers. NetworkIDManager hands
        // out the cross-network object handles used by replicas. StatisticsHistoryPlugin tracks
        // per-connection bandwidth/RTT/loss.
        MafiaNet::RPC4 _rpc;
        MafiaNet::NetworkIDManager _networkIDManager;
        MafiaNet::StatisticsHistoryPlugin _statisticsHistory;

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
        NetworkPeer();
        ~NetworkPeer();

        bool Send(Messages::IMessage &msg, MafiaNet::RakNetGUID guid = MafiaNet::UNASSIGNED_RAKNET_GUID, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED) const;

        bool Send(Messages::IMessage &msg, uint64_t guid = (uint64_t)-1, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED);

        void RegisterMessage(uint8_t message, Messages::PacketCallback callback);

        template <typename T>
        void RegisterMessage(uint8_t message, fu2::function<void(MafiaNet::RakNetGUID, T *) const> callback) {
            if (callback == nullptr) {
                return;
            }

            _registeredMessageCallbacks[message] = [this, callback, message](MafiaNet::Packet *p) {
                MafiaNet::BitStream bs(p->data + _packetDataOffset + 1, p->length - _packetDataOffset - 1, false);
                T msg = {};
                msg.SetPacket(p);
                msg.Serialize(&bs, false);
                msg.Serialize2(&bs, false);
                if (msg.Valid2()) {
                    if (msg.Valid()) {
                        callback(p->guid, &msg);
                    }
                    else {
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Message {} has failed to pass Valid() check, skipping!", message);
                    }
                }
                else {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Message {} has failed to pass Valid2() check, skipping!", message);
                }
            };
        }

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

        void SetUnknownPacketHandler(Messages::PacketCallback callback) {
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

        Replication::ReplicationManager *GetReplicationManager() const noexcept {
            return _replicationManager.get();
        }
    };
} // namespace Framework::Networking
