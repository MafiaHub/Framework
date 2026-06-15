/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>

namespace Framework::Utils {
    constexpr uint64_t BitFlag(uint32_t pos) {
        return 1ULL << pos;
    }

    constexpr void BitSet(uint64_t &var, uint64_t val) {
        var |= val;
    }

    constexpr void BitClear(uint64_t &var, uint64_t val) {
        var &= ~val;
    }

    constexpr bool BitHas(uint64_t var, uint64_t val) {
        return (var & val) != 0;
    }

    constexpr uint64_t BitNot(uint64_t var) {
        return ~var;
    }

    constexpr void BitXor(uint64_t &var, uint64_t mask) {
        var ^= mask;
    }

    constexpr void BitAnd(uint64_t &var, uint64_t mask) {
        var &= mask;
    }
} // namespace Framework::Utils
