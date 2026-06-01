/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/BitStream.h>
#include <mafianet/MessageIdentifiers.h>
#include <mafianet/types.h>
#include <function2.hpp>

namespace Framework::Networking::Messages {
    enum class DisconnectionReason : uint32_t {
        NO_FREE_SLOT,
        GRACEFUL_SHUTDOWN,
        LOST,
        FAILED,
        INVALID_PASSWORD,
        WRONG_VERSION,
        BANNED,
        KICKED,
        KICKED_CUSTOM,
        KICKED_INVALID_PACKET,
        UNKNOWN
    };

    using PacketCallback           = fu2::function<void(MafiaNet::Packet *) const>;
    using DisconnectPacketCallback = fu2::function<void(MafiaNet::Packet *, DisconnectionReason reason) const>;

    // Connection-handshake flow messages. (RPCs are dispatched by RPC4 and entity sync by
    // ReplicaManager3, both of which use their own message IDs.)
    enum GameMessages : uint8_t {
        // Game messages handling common client connection flow
        GAME_CONNECTION_HANDSHAKE = ID_USER_PACKET_ENUM + 1,
        GAME_CONNECTION_ACKNOWLEDGE_CLIENT,
        GAME_CONNECTION_READY_ASSETS,
        GAME_CONNECTION_REQUEST_STREAMER,
        GAME_CONNECTION_FINALIZED,
        GAME_CONNECTION_KICKED,
        GAME_INIT_PLAYER,

        // Messages used by the mod
        GAME_NEXT_MESSAGE_ID
    };

    /**
     * Base interface for network message
     * \see NetworkPeer::RegisterMessage
     */
    class IMessage {
      private:
        MafiaNet::Packet *packet {};

      public:
        virtual ~IMessage()                  = default;
        virtual uint8_t GetMessageID() const = 0;

        virtual void Serialize(MafiaNet::BitStream *bs, bool write) = 0;

        /**
         * Extra serialization for middleware data
         * @param bs
         * @param write
         */
        virtual void Serialize2(MafiaNet::BitStream *bs, bool write) {};

        virtual bool Valid() const = 0;

        /**
         * Extra validation for middleware data
         * @return
         */
        virtual bool Valid2() const {
            return true;
        }

        void SetPacket(MafiaNet::Packet *p) {
            packet = p;
        }

        MafiaNet::Packet *GetPacket() const {
            return packet;
        }
    };
} // namespace Framework::Networking::Messages
