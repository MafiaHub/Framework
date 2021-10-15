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
        Messages::PacketCallback _onPlayerDisconnectCallback;
        Messages::PacketCallback _onPlayerHandshakeCallback;
        Messages::PacketCallback _onNewIncomingConnection;

      public:
        ServerError Init(int32_t port, const std::string &host, int32_t maxPlayers, const std::string &password = "");
        ServerError Shutdown();

        void Update();

        int GetPing(SLNet::RakNetGUID guid);
    };
} // namespace Framework::Networking
