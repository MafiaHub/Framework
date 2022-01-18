/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <BitStream.h>
#include <MessageIdentifiers.h>
#include <RakNetTypes.h>
#include <functional>

namespace Framework::Networking::Messages {
    using MessageCallback          = std::function<void(SLNet::BitStream *)>;
    using PacketCallback           = std::function<void(SLNet::Packet *)>;
    using DisconnectPacketCallback = std::function<void(SLNet::Packet *, uint32_t reason)>;

    enum DisconnectionReason { NO_FREE_SLOT, GRACEFUL_SHUTDOWN, LOST, FAILED, INVALID_PASSWORD, WRONG_VERSION, BANNED, KICKED, KICKED_INVALID_PACKET, UNKNOWN };

    enum InternalMessages : uint8_t { INTERNAL_RPC = ID_USER_PACKET_ENUM + 1, INTERNAL_NEXT_MESSAGE_ID };
    enum GameMessages : uint8_t {
        GAME_CONNECTION_HANDSHAKE = INTERNAL_NEXT_MESSAGE_ID,
        GAME_CONNECTION_FINALIZED,
        GAME_CONNECTION_KICKED,
        GAME_SYNC_ENTITY_SPAWN,
        GAME_SYNC_ENTITY_UPDATE,
        GAME_SYNC_ENTITY_CLIENT_UPDATE, // client -> server
        GAME_SYNC_ENTITY_SELF_UPDATE, // server sends data to streamer
        GAME_SYNC_ENTITY_DESPAWN,
        GAME_NEXT_MESSAGE_ID
    };

    class IMessage {
      private:
        SLNet::Packet *packet;

      public:
        virtual uint8_t GetMessageID() const = 0;

        virtual void Serialize(SLNet::BitStream *bs, bool write) = 0;

        virtual bool Valid() = 0;

        void SetPacket(SLNet::Packet *p) {
            packet = p;
        }

        SLNet::Packet *GetPacket() const {
            return packet;
        }
    };
} // namespace Framework::Networking::Messages
