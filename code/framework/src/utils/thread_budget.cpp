/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "thread_budget.h"

#include <algorithm>

namespace Framework::Utils {
    ThreadBudget ThreadBudget::For(uint32_t cores, int32_t requestedReplication, int32_t requestedBackground) {
        ThreadBudget budget;
        // Two cores are spoken for: the tick thread and the network thread.
        budget.replicationWorkers    = std::min(cores > 2 ? cores - 2 : 0, kMaxAutoReplicationWorkers);
        budget.backgroundWorkers     = std::clamp<uint32_t>(cores / 4, 1, 4);
        budget.scriptPlatformThreads = std::clamp<uint32_t>(cores / 2, 4, 8);
        if (requestedReplication >= 0) {
            budget.replicationWorkers = std::min(static_cast<uint32_t>(requestedReplication), kMaxRequestedWorkers);
        }
        if (requestedBackground >= 0) {
            budget.backgroundWorkers = std::min(static_cast<uint32_t>(requestedBackground), kMaxRequestedWorkers);
        }
        return budget;
    }
} // namespace Framework::Utils
