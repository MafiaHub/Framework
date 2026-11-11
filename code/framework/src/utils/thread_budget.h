/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>

namespace Framework::Utils {
    // How a server shares the host's cores between its thread pools. The tick thread and MafiaNet's network thread
    // are always there; the rest scale with the machine unless the operator pins them.
    struct ThreadBudget {
        // Past this the serialize pass is bound by its serial part, not by the workers.
        static constexpr uint32_t kMaxAutoReplicationWorkers = 12;
        static constexpr uint32_t kMaxRequestedWorkers       = 64;

        // Threads that do replication work beside the tick thread, which waits for them.
        uint32_t replicationWorkers = 0;
        // Threads for blocking work the tick thread hands off (see TaskScheduler).
        uint32_t backgroundWorkers = 1;
        // Node's platform pool, shared by every resource's isolate for collection and compilation.
        uint32_t scriptPlatformThreads = 4;

        // `cores` is std::thread::hardware_concurrency(), 0 when unknown. A negative request leaves the count to
        // the machine; zero or more is taken as given.
        static ThreadBudget For(uint32_t cores, int32_t requestedReplication = -1, int32_t requestedBackground = -1);
    };
} // namespace Framework::Utils
