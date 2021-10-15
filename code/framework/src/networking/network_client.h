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

      public:
        NetworkClient();

        ~NetworkClient();

        ClientError Init();
        ClientError Shutdown();

        void Update();

        ClientError Connect(const std::string &host, int32_t port, const std::string &password = "");

        ClientError Disconnect();

        int GetPing();
    };
}; // namespace Framework::Networking
