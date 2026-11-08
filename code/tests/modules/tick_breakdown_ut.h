/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "integrations/server/tick_breakdown.h"

// What the slow-tick warning says about where the tick went: the part an operator acts on.
MODULE(tick_breakdown, {
    using Framework::Integrations::Server::TickBreakdown;

    IT("names the phases that took time, largest first, with replication split in two", {
        TickBreakdown tick;
        tick.totalMs               = 412.0;
        tick.scriptsMs             = 280.4;
        tick.replicationInterestMs = 12.2;
        tick.replicationPassMs     = 83.1;
        tick.packetsMs             = 20.0;
        tick.gameMs                = 9.6;
        tick.otherMs               = 6.7;
        STREQUALS(tick.Describe().c_str(), "scripts 280 ms, replication 95 ms (interest 12, pass 83), packets 20 ms, game 10 ms, other 7 ms");
    });

    IT("leaves out phases that took less than a millisecond", {
        TickBreakdown tick;
        tick.totalMs               = 150.0;
        tick.scriptsMs             = 148.0;
        tick.replicationInterestMs = 0.3;
        tick.replicationPassMs     = 0.4;
        tick.packetsMs             = 1.0;
        tick.gameMs                = 0.2;
        STREQUALS(tick.Describe().c_str(), "scripts 148 ms, packets 1 ms");
    });
});
