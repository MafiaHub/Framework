/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
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

namespace Framework::Networking {
    class NetworkClient: public NetworkPeer {
      private:
        PeerState _state;

        Messages::PacketCallback _onPlayerConnectedCallback;
        Messages::DisconnectPacketCallback _onPlayerDisconnectedCallback;

      public:
        NetworkClient();

        ~NetworkClient();

        ClientError Init();
        ClientError Shutdown();

        void Update();
        bool HandlePacket(uint8_t packetID, SLNet::Packet *packet) override;

        ClientError Connect(const std::string &host, int32_t port, const std::string &password = "");

        ClientError Disconnect();

        int GetPing();

        PeerState GetConnectionState() const {
            return _state;
        }

        void SetOnPlayerConnectedCallback(Messages::PacketCallback callback) {
            _onPlayerConnectedCallback = callback;
        }

        void SetOnPlayerDisconnectedCallback(Messages::DisconnectPacketCallback callback) {
            _onPlayerDisconnectedCallback = callback;
        }
    };
}; // namespace Framework::Networking
