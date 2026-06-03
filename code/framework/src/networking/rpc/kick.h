/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "rpc.h"

#include <cstdint>
#include <string>

namespace Framework::Networking::RPC {
    // Server -> client explicit kick with a reason, sent just before CloseConnection. Version
    // mismatches don't use this (they fail the build challenge). reason is a
    // Framework::Networking::Messages::DisconnectionReason.
    struct Kick {
        static constexpr const char *kIdentifier = "Framework::Kick";

        uint32_t reason = 0;
        std::string customReason;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, reason);
            bs->Serialize(write, customReason);
        }
    };
} // namespace Framework::Networking::RPC
