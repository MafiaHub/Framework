#pragma once

#include <chrono>
#include <stdint.h>

namespace Framework::Utils::Time {
    using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;
    extern int64_t GetTime();
    extern TimePoint GetTimePoint();
    extern int64_t GetMilliseconds(TimePoint a);
    extern int64_t GetDifference(TimePoint a, TimePoint b);
    extern int32_t Compare(TimePoint a, TimePoint b);
    extern TimePoint Add(TimePoint a, int32_t b);
} // namespace Framework::Utils::Time
