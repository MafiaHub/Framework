/*
 * MafiaHub source-available license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE.txt file in the source repository for information regarding licensing.
 */

#pragma once

#include "network_peer.h"

#include <mafianet/packet_priority.h>
#include <mafianet/types.h>

#include <cstdint>
#include <string_view>

namespace Framework::Networking {
    // Server: the connection a packet came from, as its RPC handler is given it. Valid for the
    // duration of the handler; what is kept beyond it is the guid.
    class Sender final {
      public:
        Sender(NetworkPeer &server, MafiaNet::PeerGuid guid): _server(&server), _guid(guid) {}

        MafiaNet::PeerGuid Guid() const {
            return _guid;
        }

        // Network id of the entity this connection controls (its viewer), 0 when it has none.
        uint64_t Avatar() const;

        // Sends to this connection only.
        template <RPC::Payload T>
        void Reply(T payload, MafiaNet::Priority priority = MafiaNet::Priority::High, MafiaNet::Reliability reliability = MafiaNet::Reliability::ReliableOrdered) const {
            _server->SendRPC(payload, MafiaNet::ToGuid(_guid), priority, reliability);
        }

        // Closes this connection; the client reads the reason as written.
        void Drop(std::string_view reason) const;

      private:
        NetworkPeer *_server;
        MafiaNet::PeerGuid _guid;
    };
} // namespace Framework::Networking
