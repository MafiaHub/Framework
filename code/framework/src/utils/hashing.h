/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Framework::Utils::Hashing {
    uint32_t CalculateCRC32(const char *data, size_t len);
    uint32_t CalculateCRC32(std::string data);
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
            return std::hash<std::string_view>{}(sv);
        }

        size_t operator()(const std::string &s) const noexcept {
            return (*this)(std::string_view(s));
        }

        size_t operator()(const char *s) const noexcept {
            return (*this)(std::string_view(s));
        }
    };

} // namespace Framework::Utils
