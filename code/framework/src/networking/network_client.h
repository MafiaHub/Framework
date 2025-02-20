/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "messages/messages.h"
#include "network_peer.h"
#include "state.h"

#include <RakNetTypes.h>
#include <RakPeerInterface.h>
#include <string>
#include <unordered_map>
#include <utility>

namespace Framework::Networking {
    using OnAssetsDownloadFailedCallback = fu2::function<void()>;
    
    class AssetFileTransfer final: public SLNet::FileListTransfer {
      private:
        OnAssetsDownloadFailedCallback *_cb = nullptr;

      public:
        void SetCallback(OnAssetsDownloadFailedCallback *cb);
        void OnClosedConnection(const SLNet::SystemAddress &systemAddress, SLNet::RakNetGUID rakNetGUID, SLNet::PI2_LostConnectionReason lostConnectionReason) override;
    };
    class NetworkClient: public NetworkPeer {
      private:

        PeerState _state;

        Messages::PacketCallback _onPlayerConnectedCallback;
        Messages::DisconnectPacketCallback _onPlayerDisconnectedCallback;
        OnAssetsDownloadFailedCallback _onAssetsDownloadFailedCallback;
        AssetFileTransfer _fileListTransfer;
      public:
        
        NetworkClient();

        ~NetworkClient();

        ClientError Init();
        ClientError Shutdown();

        void Update() override;
        bool HandlePacket(uint8_t packetID, SLNet::Packet *packet) override;

        ClientError Connect(const std::string &host, int32_t port, const std::string &password = "");

        ClientError Disconnect();

        int GetPing() const;

        PeerState GetConnectionState() const {
            return _state;
        }

        void SetOnPlayerConnectedCallback(Messages::PacketCallback callback) {
            _onPlayerConnectedCallback = std::move(callback);
        }

        void SetOnPlayerDisconnectedCallback(Messages::DisconnectPacketCallback callback) {
            _onPlayerDisconnectedCallback = std::move(callback);
        }

        void SetOnAssetsDownloadFailedCallback(OnAssetsDownloadFailedCallback callback) {
            _onAssetsDownloadFailedCallback = callback;
        }

        template <typename T>
        bool SendGameRPC(T &rpc, SLNet::RakNetGUID guid = SLNet::UNASSIGNED_RAKNET_GUID, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED) {
            SLNet::BitStream bs;
            bs.Write(Messages::INTERNAL_RPC);
            bs.Write(rpc.GetHashName());
            rpc.Serialize(&bs, true);
            rpc.Serialize2(&bs, true);

            if (_peer->Send(&bs, priority, reliability, 0, guid, guid == SLNet::UNASSIGNED_RAKNET_GUID) <= 0) {
                return false;
            }
            return true;
        }
    };
}; // namespace Framework::Networking
