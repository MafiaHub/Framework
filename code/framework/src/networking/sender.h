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
    class NetworkServer;

    // Server: the connection a packet came from, as its RPC handler is given it. Valid for the
    // duration of the handler; what is kept beyond it is the guid. Only a NetworkServer makes one.
    class Sender final {
      public:
        MafiaNet::PeerGuid Guid() const {
            return MafiaNet::ToPeerGuid(_guid);
        }

        // The same connection as MafiaNet names it, its slot on this server included.
        MafiaNet::RakNetGUID Connection() const {
            return _guid;
        }

        // Network id of the entity this connection controls (its viewer), 0 when it has none. Looked
        // up on every call, so a handler that sets the viewer sees it.
        uint64_t Avatar() const;

        // Sends to this connection only.
        template <RPC::Payload T>
        void Reply(T payload, MafiaNet::Priority priority = MafiaNet::Priority::High, MafiaNet::Reliability reliability = MafiaNet::Reliability::ReliableOrdered) const;

        // Closes this connection. The reason is sent to the client verbatim, so it says nothing the
        // client should not read. Packets the connection had already sent are still in the queue:
        // their handlers may run after this returns.
        void Drop(std::string_view reason) const;

      private:
        friend class NetworkServer;
        Sender(NetworkServer &server, MafiaNet::RakNetGUID guid): _server(&server), _guid(guid) {}

        NetworkServer *_server;
        MafiaNet::RakNetGUID _guid;
    };
} // namespace Framework::Networking
