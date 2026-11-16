/*
 * MafiaHub source-available license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE.txt file in the source repository for information regarding licensing.
 */

#pragma once

#include <algorithm>
#include <chrono>

namespace Framework::Utils {
    // A rate limit that allows bursts: `burst` tokens to start with and at most, refilled at `rate`
    // a second; each thing let through takes one. Not thread-safe: one per thing limited, used where
    // that thing is handled. A rate of zero or less never runs out.
    class TokenBucket final {
      public:
        using Clock = std::chrono::steady_clock;

        TokenBucket() = default;
        TokenBucket(double rate, double burst, Clock::time_point now = Clock::now()): _rate(rate), _burst(burst), _tokens(burst), _last(now) {}

        // Takes a token if there is one.
        bool Take(Clock::time_point now = Clock::now()) {
            if (_rate <= 0.0) {
                return true;
            }
            if (now > _last) {
                _tokens = std::min(_burst, _tokens + std::chrono::duration<double>(now - _last).count() * _rate);
                _last   = now;
            }
            if (_tokens < 1.0) {
                return false;
            }
            _tokens -= 1.0;
            return true;
        }

      private:
        double _rate   = 0.0;
        double _burst  = 0.0;
        double _tokens = 0.0;
        Clock::time_point _last {};
    };
} // namespace Framework::Utils
