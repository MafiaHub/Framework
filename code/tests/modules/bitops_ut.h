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

    // Variable and mask types are deduced independently: these mixed pairs must
    // compile on every toolchain. A single shared template parameter rejected
    // them on LP64 GCC/Clang, where uint64_t (unsigned long) and unsigned long
    // long are distinct types despite sharing a width.
    static_assert(BitHas(uint64_t {0b1010}, 0b0010));     // uint64_t var, int mask
    static_assert(BitHas(uint32_t {0b1010}, 0b0010ULL));  // narrow var, ULL mask
    static_assert(!BitHas(uint16_t {0b1010}, 0b0001ULL));

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

    IT("operates on narrow integers and enum flag fields", {
        uint32_t flags = 0;
        BitSet(flags, 1u << 2);
        UEQUALS(flags, 0b0100u);
        BitClear(flags, 1u << 2);
        UEQUALS(flags, 0u);

        enum class Perm : uint32_t { Read = 1u << 0, Write = 1u << 1 };
        Perm p = Perm::Read;
        EQUALS(BitHas(p, Perm::Read), true);
        EQUALS(BitHas(p, Perm::Write), false);
        BitSet(p, Perm::Write);
        EQUALS(BitHas(p, Perm::Write), true);
    });

    IT("deduces variable and mask types independently across widths and literals", {
        // Exactly the shape that broke under a single shared template parameter:
        // a uint64_t variable combined with int and ULL literals.
        uint64_t wide = 0b1010ULL;
        BitSet(wide, 0b0100);    // int mask
        UEQUALS(wide, 0b1110ULL);
        BitClear(wide, 0b0010ULL); // ULL mask
        UEQUALS(wide, 0b1100ULL);
        EQUALS(BitHas(wide, 0b0100), true);
        BitXor(wide, 0b1100ULL);
        UEQUALS(wide, 0ULL);

        // A narrow variable driven by a wider mask keeps the variable's type.
        uint32_t narrow = 0;
        BitSet(narrow, 0b0101ULL);
        UEQUALS(narrow, 0b0101u);
        BitAnd(narrow, 0b0100ULL);
        UEQUALS(narrow, 0b0100u);

        // An enum variable accepts a plain integer mask (and the reverse).
        enum class Flag : uint32_t { A = 1u << 0, B = 1u << 1, C = 1u << 2 };
        Flag f = Flag::A;
        BitSet(f, 0b0100u); // integer mask onto an enum variable
        EQUALS(BitHas(f, Flag::C), true);
        EQUALS(BitHas(f, 0b0010u), false);
    });
});
