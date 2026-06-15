/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/bitops.h"

#include <cstdint>

MODULE(bitops, {
    using namespace Framework::Utils;

    // Usable in constant expressions.
    static_assert(BitFlag(0) == 1ULL);
    static_assert(BitFlag(63) == (1ULL << 63));
    static_assert(BitHas(0b1010ULL, 0b0010ULL));
    static_assert(!BitHas(0b1010ULL, 0b0001ULL));
    static_assert(BitNot(0ULL) == ~0ULL);

    IT("BitFlag builds a single-bit mask at the given position", {
        UEQUALS(BitFlag(0), 1ULL);
        UEQUALS(BitFlag(3), 8ULL);
        UEQUALS(BitFlag(31), (1ULL << 31));
        UEQUALS(BitFlag(63), (1ULL << 63)); // would overflow a 32-bit shift
    });

    IT("BitSet turns on the requested bits and leaves others intact", {
        uint64_t v = 0;
        BitSet(v, BitFlag(2));
        UEQUALS(v, 0b0100ULL);
        BitSet(v, BitFlag(0));
        UEQUALS(v, 0b0101ULL);
        BitSet(v, BitFlag(0)); // idempotent
        UEQUALS(v, 0b0101ULL);
    });

    IT("BitClear turns off only the requested bits", {
        uint64_t v = 0b1111ULL;
        BitClear(v, BitFlag(1));
        UEQUALS(v, 0b1101ULL);
        BitClear(v, BitFlag(1)); // idempotent
        UEQUALS(v, 0b1101ULL);
    });

    IT("BitHas reports whether any of the masked bits are set", {
        const uint64_t v = 0b1010ULL;
        EQUALS(BitHas(v, BitFlag(1)), true);
        EQUALS(BitHas(v, BitFlag(0)), false);
        EQUALS(BitHas(v, 0b1111ULL), true); // any overlap counts
        EQUALS(BitHas(v, 0ULL), false);
    });

    IT("BitNot inverts every bit", {
        UEQUALS(BitNot(0ULL), ~0ULL);
        UEQUALS(BitNot(~0ULL), 0ULL);
        UEQUALS(BitNot(0b1010ULL), ~0b1010ULL);
    });

    IT("BitXor toggles the masked bits and is self-inverse", {
        uint64_t v = 0b1010ULL;
        BitXor(v, 0b0110ULL);
        UEQUALS(v, 0b1100ULL);
        BitXor(v, 0b0110ULL);
        UEQUALS(v, 0b1010ULL);
    });

    IT("BitAnd keeps only the masked bits", {
        uint64_t v = 0b1011ULL;
        BitAnd(v, 0b0110ULL);
        UEQUALS(v, 0b0010ULL);
        BitAnd(v, 0ULL);
        UEQUALS(v, 0ULL);
    });
});
