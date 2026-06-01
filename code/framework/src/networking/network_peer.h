/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages/messages.h"

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

        // Native MafiaNet subsystems, shared by client and server peers.
        // RPC4 dispatches remote-procedure calls by string identifier to plain C handlers (see
        // networking/rpc/rpc.h), NetworkIDManager hands out the cross-network object handles used
        // by ReplicaManager3 replicas, and StatisticsHistoryPlugin tracks per-connection
        // bandwidth/RTT/loss for diagnostics.
        MafiaNet::RPC4 _rpc;
        MafiaNet::NetworkIDManager _networkIDManager;
        MafiaNet::StatisticsHistoryPlugin _statisticsHistory;

        // Native entity replication (ReplicaManager3). Created here, attached to the peer and given
        // its server/client role by the concrete peer's Init().
        std::unique_ptr<Replication::ReplicationManager> _replicationManager;

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

        // RPCs are registered and sent natively through RPC4 (GetRPC()) using the helpers in
        // networking/rpc/rpc.h. There is no typed RegisterRPC/SendRPC layer anymore.

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
