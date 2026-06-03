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
#include <vector>

namespace Framework::Networking::RPC {
    struct ResourceInfo {
        std::string name;
        std::string version;
        uint32_t hash = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, name);
            bs->Serialize(write, version);
            bs->Serialize(write, hash);
        }
    };

    // Server -> client once the build challenge passes: opens the asset phase. readyEventId is the
    // per-connection ReadyEvent id both peers use as the spawn barrier; tickRate is the serialize
    // interval (s) the client applies once that barrier completes.
    struct ServerResources {
        static constexpr const char *kIdentifier = "Framework::ServerResources";

        int32_t readyEventId = 0;
        float tickRate = 0.0f;
        std::vector<ResourceInfo> resources;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, readyEventId);
            bs->Serialize(write, tickRate);

            uint16_t count = static_cast<uint16_t>(resources.size());
            bs->Serialize(write, count);
            if (!write) {
                resources.clear();
                if (count > 1000) { // bound untrusted input
                    count = 1000;
                }
                resources.resize(count);
            }
            for (uint16_t i = 0; i < count; ++i) {
                resources[i].Serialize(bs, write);
            }
        }
    };
} // namespace Framework::Networking::RPC
