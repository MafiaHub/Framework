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

#include <mafianet/types.h>
#include <mafianet/peerinterface.h>
#include <string>
#include <unordered_map>
#include <utility>

namespace Framework::Networking {
    using OnAssetsDownloadFailedCallback = fu2::function<void() const>;
    
    class AssetFileTransfer final: public MafiaNet::FileListTransfer {
      private:
        OnAssetsDownloadFailedCallback _cb {};

      public:
        void SetCallback(OnAssetsDownloadFailedCallback cb);
        void OnClosedConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID rakNetGUID, MafiaNet::PI2_LostConnectionReason lostConnectionReason) override;
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

        [[nodiscard]] NetworkPeerError Init();
        void Shutdown() override;

        void Update() override;
        bool HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) override;

        ConnectionError Connect(const std::string &host, int32_t port, const std::string &password = "");

        ConnectionError Disconnect();

        int GetPing() const;

        PeerState GetConnectionState() const {
            return _state;
        }

        AssetFileTransfer* GetFileListTransfer() {
            return &_fileListTransfer;
        }

        void SetOnPlayerConnectedCallback(Messages::PacketCallback callback) {
            _onPlayerConnectedCallback = std::move(callback);
        }

        void SetOnPlayerDisconnectedCallback(Messages::DisconnectPacketCallback callback) {
            _onPlayerDisconnectedCallback = std::move(callback);
        }

        void SetOnAssetsDownloadFailedCallback(OnAssetsDownloadFailedCallback callback) {
            _onAssetsDownloadFailedCallback = std::move(callback);
        }
    };
} // namespace Framework::Networking
