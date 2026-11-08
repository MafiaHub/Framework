/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

namespace Framework::Integrations::Server {
    // Where one server tick's time went. The slow-tick warning prints it, so an operator can tell a
    // script that ran long from a replication pass that grew with the player count.
    struct TickBreakdown {
        double totalMs = 0.0;
        // Resources' tick work: timers, ticks and events the scripting module runs.
        double scriptsMs = 0.0;
        // ReplicationManager::RebuildInterest: the spatial index and the viewers due this tick.
        double replicationInterestMs = 0.0;
        // The ReplicaManager3 pass: construction, destruction and serialization for every connection.
        double replicationPassMs = 0.0;
        // The rest of the packet pump: receiving, and the handlers of what arrived, client events'
        // script handlers included.
        double packetsMs = 0.0;
        // The game's own services (Instance::PostUpdate).
        double gameMs = 0.0;
        // Everything else the tick ran.
        double otherMs = 0.0;

        // "scripts 280 ms, replication 95 ms (interest 12, pass 83), ...": the phases that took a
        // millisecond or more, largest first.
        std::string Describe() const;
    };
} // namespace Framework::Integrations::Server
