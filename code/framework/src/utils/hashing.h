/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Framework::Utils::Hashing {
    namespace Detail {
        // Standard CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320), the same table the
        // previous hand-written literal held. Generated at compile time so callers can hash
        // string literals into constants.
        constexpr std::array<uint32_t, 256> MakeCRC32Table() {
            std::array<uint32_t, 256> table {};
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) {
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                }
                table[i] = c;
            }
            return table;
        }
        inline constexpr std::array<uint32_t, 256> kCRC32Table = MakeCRC32Table();
    } // namespace Detail

    constexpr uint32_t CalculateCRC32(const char *data, size_t len) {
        uint32_t result = ~static_cast<uint32_t>(0);
        for (size_t i = 0; i < len; ++i) {
            result = (result >> 8) ^ Detail::kCRC32Table[(result ^ static_cast<unsigned char>(data[i])) & 0xff];
        }
        return ~result;
    }

    constexpr uint32_t CalculateCRC32(std::string_view data) {
        return CalculateCRC32(data.data(), data.size());
    }
} // namespace Framework::Utils::Hashing

namespace Framework::Utils {

    /**
     * Transparent hash functor for string-keyed unordered containers.
     * Enables heterogeneous lookup with std::string_view without
     * constructing a temporary std::string.
     */
    struct StringHash {
        using is_transparent = void;

        size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view> {}(sv);
        }

        size_t operator()(const std::string &s) const noexcept {
            return (*this)(std::string_view(s));
        }

        size_t operator()(const char *s) const noexcept {
            return (*this)(std::string_view(s));
        }
    };

} // namespace Framework::Utils
