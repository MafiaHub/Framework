/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "rpc.h"
#include "server_resources.h" // ResourceInfo

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace Framework::Networking::RPC {
    // Server -> client, dev hot-reload only: these client resources changed on
    // the server and were reloaded. The client re-syncs their files from the
    // asset streamer (delta transfer) and restarts them in place.
    struct ResourceRefresh {
        static constexpr const char *kIdentifier = "Framework::ResourceRefresh";
        static constexpr uint16_t kMaxResources  = 1000; // bound untrusted input

        std::vector<ResourceInfo> resources;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            if (write && resources.size() > std::numeric_limits<uint16_t>::max()) {
                Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->error("ResourceRefresh holds {} resources, exceeding the wire limit; truncating", resources.size());
            }

            uint16_t count = static_cast<uint16_t>(std::min<size_t>(resources.size(), std::numeric_limits<uint16_t>::max()));
            bs->Serialize(write, count);
            if (!write) {
                resources.clear();
                resources.resize(std::min<uint16_t>(count, kMaxResources));
            }
            for (uint16_t i = 0; i < count; ++i) {
                // Entries past the sane cap are still consumed so the bitstream stays aligned.
                if (!write && i >= kMaxResources) {
                    ResourceInfo discard;
                    discard.Serialize(bs, write);
                    continue;
                }
                resources[i].Serialize(bs, write);
            }
        }
    };
} // namespace Framework::Networking::RPC
