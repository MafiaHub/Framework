/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/BitStream.h>
#include <mafianet/types.h>
#include <function2.hpp>
#include <string>

namespace Framework::Networking {
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
        UNKNOWN,
        // Serialized by DisconnectPayload: append only.
        BUILD_VERIFICATION_TIMEOUT
    };

    // Optional reason carried on a graceful disconnect (CloseConnection's reasonData).
    struct DisconnectPayload {
        uint32_t reason = static_cast<uint32_t>(DisconnectionReason::UNKNOWN);
        std::string customReason;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, reason);
            bs->Serialize(write, customReason);
        }
    };

    using PacketCallback           = fu2::function<void(MafiaNet::Packet *) const>;
    using DisconnectPacketCallback = fu2::function<void(MafiaNet::Packet *, DisconnectionReason reason, const std::string &customReason) const>;
} // namespace Framework::Networking
