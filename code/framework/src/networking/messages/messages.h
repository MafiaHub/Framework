/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <BitStream.h>
#include <MessageIdentifiers.h>
#include <functional>

namespace Framework::Networking::Messages {
    using MessageCallback          = std::function<void(SLNet::BitStream *)>;
    using PacketCallback           = std::function<void(SLNet::Packet *)>;
    using DisconnectPacketCallback = std::function<void(SLNet::Packet *, uint32_t reason)>;

    enum DisconnectionReason { NO_FREE_SLOT, GRACEFUL_SHUTDOWN, LOST, FAILED, INVALID_PASSWORD, BANNED, UNKNOWN };

    enum GameMessages : uint32_t { GAME_CONNECTION_HANDSHAKE = ID_USER_PACKET_ENUM + 1, GAME_CONNECTION_FINALIZED, GAME_SYNC };
} // namespace Framework::Networking::Messages
