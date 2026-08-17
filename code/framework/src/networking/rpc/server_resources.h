/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "rpc.h"

#include <logging/logger.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Framework::Networking::RPC {
    struct ResourceInfo {
        std::string name;
        std::string version;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, name);
            bs->Serialize(write, version);
        }
    };

    // Server -> client once the build challenge passes: opens the asset phase. readyEventId is the
    // per-connection ReadyEvent id both peers use as the spawn barrier; tickRate is the serialize
    // interval (s) the client applies once that barrier completes.
    struct ServerResources {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Framework::ServerResources");
        static constexpr uint16_t kMaxResources  = 1000; // bound untrusted input

        int32_t readyEventId = 0;
        float tickRate = 0.0f;
        std::vector<ResourceInfo> resources;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, readyEventId);
            bs->Serialize(write, tickRate);

            if (write && resources.size() > std::numeric_limits<uint16_t>::max()) {
                Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->error("ServerResources holds {} resources, exceeding the wire limit; truncating", resources.size());
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
