/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "rpc.h"

#include <cstdint>
#include <string>

namespace Framework::Networking::RPC {
    // Server->owner: scripted nametag state for the owner's own avatar, which it applies and
    // replicates upstream (the owner is authoritative for its avatar's fields).
    struct SetNametagState {
        static constexpr const char *kIdentifier = "Framework::SetNametagState";

        uint64_t networkId = 0;
        uint8_t components = 0;
        uint32_t color     = 0xFFFFFFFF;
        std::string text;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, networkId);
            bs->Serialize(write, components);
            bs->Serialize(write, color);
            bs->Serialize(write, text);
        }
    };
} // namespace Framework::Networking::RPC
