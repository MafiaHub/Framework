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
    /**
     * One asset pak a resource ships for the game engine to mount: a plain ZIP built from the
     * resource's `<lane>/` folder, staged and downloaded beside its .fwpak as
     * Utils::StreamedAssets::PakFileName(resource, lane, sha256). What a lane may carry is the
     * mod's policy, checked by both ends.
     */
    struct AssetPakInfo {
        static constexpr size_t kMaxLaneLength = 32;

        std::string lane;
        std::string sha256;
        uint64_t size = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, lane);
            bs->Serialize(write, sha256);
            bs->Serialize(write, size);
        }

        bool IsSane() const {
            return !lane.empty() && lane.size() <= kMaxLaneLength && sha256.size() == 64 && size > 0;
        }
    };

    struct ResourceInfo {
        static constexpr uint8_t kMaxAssetPaks = 8;

        std::string name;
        std::string version;

        // Hex SHA-256 of the resource's .fwpak container; the client refuses a mismatch. Empty
        // when the resource has no client entry point.
        std::string packageHash;

        // The resource's asset paks, one per lane folder it ships. A resource with paks and no
        // packageHash ships assets only: there is nothing of it to run on the client.
        std::vector<AssetPakInfo> assetPaks;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, name);
            bs->Serialize(write, version);
            bs->Serialize(write, packageHash);

            uint8_t count = static_cast<uint8_t>(std::min<size_t>(assetPaks.size(), kMaxAssetPaks));
            bs->Serialize(write, count);
            if (!write) {
                assetPaks.clear();
                assetPaks.resize(std::min<uint8_t>(count, kMaxAssetPaks));
            }
            for (uint8_t i = 0; i < count; ++i) {
                assetPaks[i].Serialize(bs, write);
            }
        }
    };

    // Server -> client once the build challenge passes and the admission gate (playerConnecting)
    // lets the connection in: it is the admission, and opens the asset phase. readyEventId is the
    // per-connection ReadyEvent id both peers use as the spawn barrier; tickRate is the serialize
    // interval (s) the client applies once that barrier completes.
    struct ServerResources {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Framework::ServerResources");
        static constexpr uint16_t kMaxResources  = 1000; // bound untrusted input

        int32_t readyEventId = 0;
        float tickRate = 0.0f;

        // Hex AES-256 key for the resource packages. See
        // docs/research/client_resource_protection.md for what this does and does not protect.
        std::string packageKey;

        std::vector<ResourceInfo> resources;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, readyEventId);
            bs->Serialize(write, tickRate);
            bs->Serialize(write, packageKey);

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
