/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/client/spsc_ring.h"

MODULE(spsc_ring, {
    using namespace Framework::Voice;

    IT("returns what was pushed, in order", {
        SpscRing<int16_t, 64> ring;
        const int16_t in[4] = {1, 2, 3, 4};
        EQUALS(ring.Push(in, 4), true);

        int16_t out[4] = {0, 0, 0, 0};
        EQUALS(ring.Pop(out, 4), true);
        EQUALS(out[0], static_cast<int16_t>(1));
        EQUALS(out[3], static_cast<int16_t>(4));
    });

    IT("reports how much is readable", {
        SpscRing<int16_t, 64> ring;
        const int16_t in[3] = {7, 8, 9};
        ring.Push(in, 3);
        EQUALS(ring.Available(), static_cast<size_t>(3));
    });

    IT("refuses a pop larger than what is buffered", {
        SpscRing<int16_t, 64> ring;
        const int16_t in[2] = {1, 2};
        ring.Push(in, 2);

        int16_t out[4] = {0, 0, 0, 0};
        EQUALS(ring.Pop(out, 4), false);
        EQUALS(ring.Available(), static_cast<size_t>(2));
    });

    IT("refuses a push that would overflow", {
        SpscRing<int16_t, 8> ring;
        const int16_t in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        EQUALS(ring.Push(in, 8), false);
    });

    IT("wraps around the end of the buffer", {
        SpscRing<int16_t, 8> ring;
        const int16_t first[5] = {1, 2, 3, 4, 5};
        int16_t scratch[5]     = {0, 0, 0, 0, 0};

        ring.Push(first, 5);
        ring.Pop(scratch, 5);

        const int16_t second[5] = {6, 7, 8, 9, 10};
        EQUALS(ring.Push(second, 5), true);
        EQUALS(ring.Pop(scratch, 5), true);
        EQUALS(scratch[0], static_cast<int16_t>(6));
        EQUALS(scratch[4], static_cast<int16_t>(10));
    });

    IT("is empty after being cleared", {
        SpscRing<int16_t, 64> ring;
        const int16_t in[3] = {1, 2, 3};
        ring.Push(in, 3);
        ring.Clear();
        EQUALS(ring.Available(), static_cast<size_t>(0));
    });
});
