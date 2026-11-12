/*
 * MafiaHub source-available license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE.txt file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/token_bucket.h"

#include <chrono>

MODULE(token_bucket, {
    using Framework::Utils::TokenBucket;
    using namespace std::chrono_literals;

    IT("lets a burst through and then no more than its rate", {
        const TokenBucket::Clock::time_point start {};
        TokenBucket bucket(10.0, 5.0, start);
        int taken = 0;
        for (int i = 0; i < 20; ++i) {
            taken += bucket.Take(start) ? 1 : 0;
        }
        EQUALS(taken, 5);
        // Half a second at ten a second is five more, not six.
        taken = 0;
        for (int i = 0; i < 20; ++i) {
            taken += bucket.Take(start + 500ms) ? 1 : 0;
        }
        EQUALS(taken, 5);
        EQUALS(bucket.Take(start + 500ms), false);
        EQUALS(bucket.Take(start + 600ms), true);
    });

    IT("saves up no more than its burst, however long it is left alone", {
        const TokenBucket::Clock::time_point start {};
        TokenBucket bucket(10.0, 5.0, start);
        int taken = 0;
        for (int i = 0; i < 20; ++i) {
            taken += bucket.Take(start + 1h) ? 1 : 0;
        }
        EQUALS(taken, 5);
    });

    IT("never runs out when it has no rate", {
        TokenBucket unlimited(0.0, 0.0);
        int taken = 0;
        for (int i = 0; i < 1000; ++i) {
            taken += unlimited.Take() ? 1 : 0;
        }
        EQUALS(taken, 1000);
        TokenBucket byDefault;
        EQUALS(byDefault.Take(), true);
    });

    IT("is not set back by a clock reading older than its last", {
        const TokenBucket::Clock::time_point start {};
        TokenBucket bucket(1.0, 1.0, start + 10s);
        EQUALS(bucket.Take(start + 10s), true);
        EQUALS(bucket.Take(start), false);
        EQUALS(bucket.Take(start + 11s), true);
    });
});
