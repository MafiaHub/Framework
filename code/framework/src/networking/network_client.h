/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "connection.h"
#include "errors.h"
#include "network_peer.h"
#include "state.h"

#include <utils/error.h>
#include <utils/result.h>

#include <mafianet/peer_interface.h>
#include <mafianet/types.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Framework::Networking {
    class NetworkClient final: public NetworkPeer {
      private:
        std::string _bindHost;

        PeerState _state;

        PacketCallback _onPlayerConnectedCallback;
        DisconnectPacketCallback _onPlayerDisconnectedCallback;
        fu2::function<void(int eventId) const> _onConnectionReadyCallback;
        fu2::function<void() const> _onInitialReplicationDownloadedCallback;
        fu2::function<void(const std::string &status) const> _onSessionStatusCallback;
        bool _initialReplicationDownloadComplete {};

        // Single teardown for every terminal disconnect: drops the connection and clears the flags.
        void ResetConnectionState();

        // Empties the peer's receive queue without dispatching.
        void DrainStalePackets();

      public:
        NetworkClient();

        // The local address the peer binds. The default binds every interface, which reaching a server on
        // another machine needs; one that only ever talks to a server on this machine binds "127.0.0.1" and
        // opens nothing on the network.
        explicit NetworkClient(std::string bindHost);

        ~NetworkClient();

        [[nodiscard]] Utils::Result<void, Error> Init();
        void Shutdown() override;

        void Update() override;
        bool HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) override;

        // sessionPayload rides the connection request (MafiaNet's session handshake): it is what the
        // server's admission gate decides on before either side reports a connection.
        [[nodiscard]] Utils::Result<void, Error> Connect(const std::string &host, int32_t port, const std::string &password = "", const std::string &sessionPayload = "");

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

        void SetOnPlayerConnectedCallback(PacketCallback callback) {
            _onPlayerConnectedCallback = std::move(callback);
        }

        void SetOnPlayerDisconnectedCallback(DisconnectPacketCallback callback) {
            _onPlayerDisconnectedCallback = std::move(callback);
        }

        // Fired when the spawn barrier completes — the client activates replication and finalizes.
        // A line from the server's admission gate while the connection request waits for its answer.
        // Remote text, bounded by MafiaNet's session payload size and nothing else.
        void SetOnSessionStatusCallback(fu2::function<void(const std::string &status) const> callback) {
            _onSessionStatusCallback = std::move(callback);
        }

        void SetOnConnectionReadyCallback(fu2::function<void(int eventId) const> callback) {
            _onConnectionReadyCallback = std::move(callback);
        }

        void SetOnInitialReplicationDownloadedCallback(fu2::function<void() const> callback) {
            _onInitialReplicationDownloadedCallback = std::move(callback);
        }
    };
} // namespace Framework::Networking
