/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "policy.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Utils::StreamedAssets {
    /**
     * The file name a pak is staged, sent and cached under: `<resource>.<lane>.<hash prefix>.pak`.
     * The hash makes a changed pak a new file rather than one rewritten under a mounted archive.
     */
    inline std::string PakFileName(std::string_view resource, std::string_view lane, std::string_view sha256) {
        return std::string(resource) + "." + std::string(lane) + "." + std::string(sha256.substr(0, 16)) + ".pak";
    }

    /** One file going into a pak: its normalised entry path and where its bytes are on disk. */
    struct PakSourceEntry {
        std::string path;
        std::string sourceFile;
    };

    /** One file a validated pak carries. The CRC tells a changed file from an unchanged one. */
    struct PakEntry {
        std::string path;
        std::uint32_t crc32 = 0;
        std::uint64_t size  = 0;
    };

    /**
     * Writes and checks streamed paks: classic ZIP, store or deflate, every entry with real sizes
     * in its local header and no extra field, no data descriptor, no archive comment. That is the
     * subset engine virtual filesystems read without surprises -- CryPak, for one, derives an
     * entry's data offset from the local header and refuses it unless the local and central
     * headers agree.
     */
    class PakArchive final {
      public:
        /**
         * Builds a deterministic pak: entries sorted by path, one fixed timestamp, store or deflate
         * per the policy. Identical input yields identical bytes, so the SHA-256 of the output is a
         * stable name for it.
         */
        static bool Write(const std::string &outputPath, std::vector<PakSourceEntry> entries, const Policy &policy, std::string &error);

        /**
         * Checks a pak against the policy's lane and lists its entries. Every entry must already be
         * in normalised form -- a pak that needs normalising was not written by a server.
         */
        static bool Validate(const std::string &pakPath, const Policy &policy, std::string_view lane, std::string_view resource, std::vector<PakEntry> &entries, std::string &error);
    };
} // namespace Framework::Utils::StreamedAssets
