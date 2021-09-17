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
        ServerError Init(int32_t, std::string &, int32_t, std::string &);
        ServerError Shutdown();

        void Update();

        int GetPing(SLNet::RakNetGUID guid);

        void SetOnPlayerDisconnectCallback(Messages::PacketCallback callback) {
            _onPlayerDisconnectCallback = callback;
        }

        void SetOnPlayerHandshakeCallback(Messages::PacketCallback callback) {
            _onPlayerHandshakeCallback = callback;
        }

        void SetOnNewIncomingConnectionCallback(Messages::PacketCallback callback) {
            _onNewIncomingConnection = callback;
        }
    };
} // namespace Framework::Networking
