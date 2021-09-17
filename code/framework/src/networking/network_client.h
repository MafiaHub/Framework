/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
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

        Messages::PacketCallback _onPlayerConnectionAcceptedCallback;
        Messages::PacketCallback _onPlayerConnectionFinalizedCallback;
        Messages::DisconnectPacketCallback _onPlayerDisconnectedCallback;

      public:
        NetworkClient();

        ~NetworkClient();

        ClientError Init();
        ClientError Shutdown();

        void Update();

        ClientError Connect(std::string &, int32_t, std::string &);

        ClientError Disconnect();

        int GetPing();

        void SetOnPlayerConnectionAcceptedCallback(Messages::PacketCallback callback) {
            _onPlayerConnectionAcceptedCallback = callback;
        }

        void SetOnPlayerConnectionFinalizedCallback(Messages::PacketCallback callback) {
            _onPlayerConnectionFinalizedCallback = callback;
        }

        void SetOnPlayerDisconnectedCallback(Messages::DisconnectPacketCallback callback) {
            _onPlayerDisconnectedCallback = callback;
        }
    };
}; // namespace Framework::Networking
