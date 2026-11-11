/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/thread_budget.h"

MODULE(thread_budget, {
    using Framework::Utils::ThreadBudget;

    IT("keeps everything on the tick thread of a machine with no core to spare", {
        for (const uint32_t cores : {0u, 1u, 2u}) {
            const ThreadBudget budget = ThreadBudget::For(cores);
            EQUALS(budget.replicationWorkers, 0u);
            // Blocking work still must not run on the tick thread.
            EQUALS(budget.backgroundWorkers, 1u);
        }
    });

    IT("gives replication every core but the tick and network threads", {
        EQUALS(ThreadBudget::For(4).replicationWorkers, 2u);
        EQUALS(ThreadBudget::For(8).replicationWorkers, 6u);
        EQUALS(ThreadBudget::For(12).replicationWorkers, 10u);
    });

    IT("stops adding replication workers where they stop paying", {
        EQUALS(ThreadBudget::For(16).replicationWorkers, ThreadBudget::kMaxAutoReplicationWorkers);
        EQUALS(ThreadBudget::For(128).replicationWorkers, ThreadBudget::kMaxAutoReplicationWorkers);
    });

    IT("grows the background and script platform pools with the machine, within bounds", {
        EQUALS(ThreadBudget::For(4).backgroundWorkers, 1u);
        EQUALS(ThreadBudget::For(8).backgroundWorkers, 2u);
        EQUALS(ThreadBudget::For(64).backgroundWorkers, 4u);
        EQUALS(ThreadBudget::For(2).scriptPlatformThreads, 4u);
        EQUALS(ThreadBudget::For(12).scriptPlatformThreads, 6u);
        EQUALS(ThreadBudget::For(64).scriptPlatformThreads, 8u);
    });

    IT("takes the operator's counts over its own, zero included", {
        const ThreadBudget pinned = ThreadBudget::For(16, 3, 0);
        EQUALS(pinned.replicationWorkers, 3u);
        EQUALS(pinned.backgroundWorkers, 0u);
        const ThreadBudget serial = ThreadBudget::For(16, 0, -1);
        EQUALS(serial.replicationWorkers, 0u);
        EQUALS(serial.backgroundWorkers, 4u);
    });

    IT("refuses an absurd request instead of starting hundreds of threads", {
        const ThreadBudget budget = ThreadBudget::For(8, 100000, 100000);
        EQUALS(budget.replicationWorkers, ThreadBudget::kMaxRequestedWorkers);
        EQUALS(budget.backgroundWorkers, ThreadBudget::kMaxRequestedWorkers);
    });
});
