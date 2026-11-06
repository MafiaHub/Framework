/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>

namespace Framework::Scripting {

    // A runtime's heap and its collector's running totals, as NodeRuntime::Sample() reads them and the resource
    // monitor records them once a second.
    struct RuntimeSample {
        uint64_t heapUsedBytes  = 0;
        uint64_t heapTotalBytes = 0;
        uint64_t heapLimitBytes = 0;
        uint64_t externalBytes  = 0;
        uint64_t gcPauses       = 0;
        double gcPauseMs        = 0.0;
    };

} // namespace Framework::Scripting
