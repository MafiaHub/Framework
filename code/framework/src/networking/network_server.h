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

#include <RakNetTypes.h>
#include <RakPeerInterface.h>
#include <string>
#include <unordered_map>

namespace Framework::Networking {
    class NetworkServer: public NetworkPeer {
      private:
        Messages::DisconnectPacketCallback _onPlayerDisconnectCallback;

      public:
        NetworkServer(): NetworkPeer() {}

        ServerError Init(int32_t port, const std::string &host, int32_t maxPlayers, const std::string &password = "");
        ServerError Shutdown();

        void Update();

        bool HandlePacket(uint8_t packetID, SLNet::Packet *packet) override;

        int GetPing(SLNet::RakNetGUID guid);

        void SetOnPlayerDisconnectCallback(Messages::DisconnectPacketCallback callback) {
            _onPlayerDisconnectCallback = callback;
        }
    };
} // namespace Framework::Networking
