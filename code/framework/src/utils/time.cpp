/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "time.h"

namespace Framework::Utils {
    int64_t Time::GetTime() {
        return GetMilliseconds(GetTimePoint());
    }
    int64_t Time::GetDifference(Time::TimePoint a, Time::TimePoint b) {
        return (GetMilliseconds(a) - GetMilliseconds(b));
    }
    int64_t Time::GetMilliseconds(Time::TimePoint a) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(a.time_since_epoch()).count();
    }
    int32_t Time::Compare(Time::TimePoint a, Time::TimePoint b) {
        if (a == b)
            return 0;
        else
            return (GetDifference(a, b) > 0 ? 1 : -1);
    }
    Time::TimePoint Time::Add(TimePoint a, int32_t b) {
        return a + std::chrono::milliseconds(b);
    }
    Time::TimePoint Time::GetTimePoint() {
        return std::chrono::high_resolution_clock::now();
    }
} // namespace Framework::Utils
