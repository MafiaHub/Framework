/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

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
} // namespace Framework::Networking::Messages
