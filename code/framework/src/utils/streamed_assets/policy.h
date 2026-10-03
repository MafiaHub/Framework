/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Utils::StreamedAssets {
    // Bounds every project shares. Archives stay classic ZIP: no ZIP64 and under 2 GB, which is
    // what engine-side virtual filesystems such as CryPak accept.
    inline constexpr std::uint64_t kMaxPakSize    = 0x7FFFFFFFull;
    inline constexpr std::size_t kMaxPakEntries   = 65535;
    inline constexpr std::uint64_t kMaxEntrySize  = 512ull * 1024 * 1024;
    inline constexpr std::size_t kMaxPathLength   = 240;
    inline constexpr std::size_t kMaxResourceName = 64;

    /**
     * A project's statement of what a streamed pak may carry and how it is laid out.
     *
     * The framework builds, transfers, caches and verifies paks; the project says which folders of
     * a resource are lanes, which entry paths each lane accepts, and how each is compressed. The
     * same policy is applied by the server when it builds a pak and by the client before it uses
     * one -- the server is the threat model, so the client's check is the one that counts.
     */
    class Policy {
      public:
        virtual ~Policy() = default;

        /** Lane folder names a resource may carry, in the order their paks are announced. */
        virtual const std::vector<std::string> &GetLanes() const = 0;

        /**
         * Normalises an entry path and checks it against the lane. `out` changes only on success;
         * `reason` says what refused it.
         */
        virtual bool NormalizeEntry(std::string_view lane, std::string_view resource, std::string_view raw, std::string &out, std::string &reason) const = 0;

        /** Whether an entry is stored rather than deflated. */
        virtual bool PrefersStore(std::string_view normalized) const = 0;

        bool HasLane(std::string_view lane) const;
    };

    /** Lowercase ASCII letters, digits, `-` and `_`, 1 to 64 characters. */
    bool IsValidResourceName(std::string_view name);

    /** A lowercase 64-digit hex SHA-256. */
    bool IsSha256(std::string_view text);
} // namespace Framework::Utils::StreamedAssets
