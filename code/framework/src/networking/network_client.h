/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "connection.h"
#include "network_peer.h"
#include "state.h"

#include <utils/error.h>
#include <utils/result.h>

#include <mafianet/types.h>
#include <mafianet/peerinterface.h>
#include <string>
#include <string_view>
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
    class NetworkClient final: public NetworkPeer {
      private:

        PeerState _state;

        PacketCallback _onPlayerConnectedCallback;
        DisconnectPacketCallback _onPlayerDisconnectedCallback;
        OnAssetsDownloadFailedCallback _onAssetsDownloadFailedCallback;
        fu2::function<void(int eventId) const> _onConnectionReadyCallback;
        AssetFileTransfer _fileListTransfer;
        bool _initialReplicationDownloadComplete {};

      public:
        NetworkClient();

        ~NetworkClient();

        [[nodiscard]] Utils::Result<void, Error> Init();
        void Shutdown() override;

        void Update() override;
        bool HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) override;

        [[nodiscard]] Utils::Result<void, Error> Connect(const std::string &host, int32_t port, const std::string &password = "");

        [[nodiscard]] Utils::Result<void, Error> Disconnect();

        int GetPing() const;

        // The server's session payload, empty when it published none. Valid from
        // ID_CONNECTION_REQUEST_ACCEPTED until the connection drops. Remote input: validate it.
        std::string_view GetRemoteSessionConfig(MafiaNet::RakNetGUID guid) const;

        PeerState GetConnectionState() const {
            return _state;
        }

        bool IsInitialReplicationDownloadComplete() const {
            return _initialReplicationDownloadComplete;
        }

        AssetFileTransfer *GetFileListTransfer() {
            return &_fileListTransfer;
        }

        void SetOnPlayerConnectedCallback(PacketCallback callback) {
            _onPlayerConnectedCallback = std::move(callback);
        }

        void SetOnPlayerDisconnectedCallback(DisconnectPacketCallback callback) {
            _onPlayerDisconnectedCallback = std::move(callback);
        }

        void SetOnAssetsDownloadFailedCallback(OnAssetsDownloadFailedCallback callback) {
            _onAssetsDownloadFailedCallback = std::move(callback);
        }

        // Fired when the spawn barrier completes — the client activates replication and finalizes.
        void SetOnConnectionReadyCallback(fu2::function<void(int eventId) const> callback) {
            _onConnectionReadyCallback = std::move(callback);
        }
    };
} // namespace Framework::Networking
