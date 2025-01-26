/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <BitStream.h>
#include <MessageIdentifiers.h>
#include <RakNetTypes.h>
#include <function2.hpp>

#include "utils/optional.h"

namespace Framework::Networking::Messages {
    using PacketCallback           = fu2::function<void(SLNet::Packet *) const>;
    using DisconnectPacketCallback = fu2::function<void(SLNet::Packet *, uint32_t reason) const>;

    enum DisconnectionReason {
        NO_FREE_SLOT,
        GRACEFUL_SHUTDOWN,
        LOST,
        FAILED,
        INVALID_PASSWORD,
        WRONG_VERSION,
        BANNED,
        KICKED,
        KICKED_INVALID_PACKET,
        UNKNOWN
    };

    // Internal Framework messages
    enum InternalMessages : uint8_t {
        INTERNAL_RPC = ID_USER_PACKET_ENUM + 1,
        INTERNAL_NEXT_MESSAGE_ID
    };

    // Internal game flow messages
    enum GameMessages : uint8_t {
        // Game messages handling common client connection flow
        GAME_CONNECTION_HANDSHAKE = INTERNAL_NEXT_MESSAGE_ID,
        GAME_CONNECTION_FINALIZED,
        GAME_CONNECTION_KICKED,
        GAME_INIT_PLAYER,

        // Game sync entity streamer messages
        GAME_SYNC_ENTITY_SPAWN,
        GAME_SYNC_ENTITY_UPDATE,
        GAME_SYNC_ENTITY_SELF_UPDATE,  // server sends data to streamer
        GAME_SYNC_ENTITY_OWNER_UPDATE, // server sends data about owned entity
        GAME_SYNC_ENTITY_DESPAWN,

        // Messages used by the mod
        GAME_NEXT_MESSAGE_ID
    };

    /**
     * Base interface for network message
     * \see NetworkPeer::RegisterMessage
     */
    class IMessage {
      private:
        SLNet::Packet *packet {};

      public:
        virtual ~IMessage()                  = default;
        virtual uint8_t GetMessageID() const = 0;

        virtual void Serialize(SLNet::BitStream *bs, bool write) = 0;

        /**
         * Extra serialization for middleware data
         * @param bs
         * @param write
         */
        virtual void Serialize2(SLNet::BitStream *bs, bool write) {};

        virtual bool Valid() const = 0;

        /**
         * Extra validation for middleware data
         * @return
         */
        virtual bool Valid2() const {
            return true;
        }

        void SetPacket(SLNet::Packet *p) {
            packet = p;
        }

        SLNet::Packet *GetPacket() const {
            return packet;
        }
    };
} // namespace Framework::Networking::Messages
