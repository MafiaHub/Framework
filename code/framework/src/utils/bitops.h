/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace Framework::Utils {
    // Accepts any integer or enum flag field, mirroring the old macros that cast through uint64_t.
    template <typename T>
    concept BitField = std::integral<T> || std::is_enum_v<T>;

    template <BitField T = uint64_t>
    constexpr T BitFlag(uint32_t pos) {
        return static_cast<T>(uint64_t {1} << pos);
    }

    // The variable's type T drives the result; the mask's type U is deduced
    // independently so a literal (e.g. ULL) need not match T. Without this, a
    // single shared T fails deduction wherever uint64_t and unsigned long long
    // are distinct types (LP64 GCC/Clang), even though they share a width.
    template <BitField T, BitField U>
    constexpr void BitSet(T &var, U val) {
        var = static_cast<T>(static_cast<uint64_t>(var) | static_cast<uint64_t>(val));
    }

    template <BitField T, BitField U>
    constexpr void BitClear(T &var, U val) {
        var = static_cast<T>(static_cast<uint64_t>(var) & ~static_cast<uint64_t>(val));
    }

    template <BitField T, BitField U>
    constexpr bool BitHas(T var, U val) {
        return (static_cast<uint64_t>(var) & static_cast<uint64_t>(val)) != 0;
    }

    template <BitField T>
    constexpr T BitNot(T var) {
        return static_cast<T>(~static_cast<uint64_t>(var));
    }

    template <BitField T, BitField U>
    constexpr void BitXor(T &var, U val) {
        var = static_cast<T>(static_cast<uint64_t>(var) ^ static_cast<uint64_t>(val));
    }

    template <BitField T, BitField U>
    constexpr void BitAnd(T &var, U val) {
        var = static_cast<T>(static_cast<uint64_t>(var) & static_cast<uint64_t>(val));
    }
} // namespace Framework::Utils
