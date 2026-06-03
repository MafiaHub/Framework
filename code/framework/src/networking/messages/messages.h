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

    // Custom message IDs. The connection handshake no longer uses any: build verification is
    // TwoWayAuthentication, the resource list / client identity / kick are RPC4 payloads, the spawn
    // barrier is ReadyEvent, and entity sync is ReplicaManager3 — all of which use their own message
    // IDs. Mods extend their own messages from GAME_NEXT_MESSAGE_ID.
    enum GameMessages : uint8_t {
        GAME_INIT_PLAYER = ID_USER_PACKET_ENUM + 1,

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
