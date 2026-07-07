/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/replication/delegation_policy.h"

#include <cstdint>
#include <vector>

// Unit coverage for the pure syncer-election policy (ElectOwner). No networking is involved, so the
// hysteresis, load-balancing, proximity and veto rules are exercised in isolation.
MODULE(delegation, {
    using namespace Framework::Networking::Replication;

    const MafiaNet::PeerGuid kNone = MafiaNet::UNASSIGNED_PEER_GUID;
    const auto guid                = [](uint64_t v) {
        return static_cast<MafiaNet::PeerGuid>(v);
    };
    const auto raw = [](MafiaNet::PeerGuid g) {
        return static_cast<uint64_t>(g);
    };
    const auto candidate = [](MafiaNet::PeerGuid g, float x, float z, int load, bool eligible) {
        DelegationCandidate c;
        c.guid      = g;
        c.position  = glm::vec3(x, 0.0f, z);
        c.ownedLoad = load;
        c.eligible  = eligible;
        return c;
    };

    // Default policy: acquire 80, drop 130. Entity sits at the origin. Distances are on the XZ plane
    // (groundXY=false) unless a test overrides it.
    DelegationParams params;
    const glm::vec3 origin(0.0f, 0.0f, 0.0f);

    IT("elects the nearest eligible candidate when the entity is unowned", {
        std::vector<DelegationCandidate> c = {
            candidate(guid(1), 60.0f, 0.0f, 0, true),
            candidate(guid(2), 30.0f, 0.0f, 0, true),
        };
        const auto next = ElectOwner(origin, kNone, c, params, false);
        UEQUALS(raw(next), raw(guid(2)));
    });

    IT("prefers the least-loaded candidate over a nearer but busier one", {
        std::vector<DelegationCandidate> c = {
            candidate(guid(1), 20.0f, 0.0f, 3, true), // closest but heavily loaded
            candidate(guid(2), 70.0f, 0.0f, 0, true), // farther but idle
        };
        const auto next = ElectOwner(origin, kNone, c, params, false);
        UEQUALS(raw(next), raw(guid(2)));
    });

    IT("keeps the current owner inside dropRange even past acquireRange (hysteresis)", {
        // Owner at 100: beyond acquire (80) but within drop (130), so it must not be re-elected away
        // even though a fresh candidate sits closer.
        std::vector<DelegationCandidate> c = {
            candidate(guid(1), 100.0f, 0.0f, 0, true), // current owner, in the hysteresis band
            candidate(guid(2), 40.0f, 0.0f, 0, true),  // closer newcomer
        };
        const auto next = ElectOwner(origin, guid(1), c, params, false);
        UEQUALS(raw(next), raw(guid(1)));
    });

    IT("re-elects when the current owner drifts past dropRange", {
        std::vector<DelegationCandidate> c = {
            candidate(guid(1), 140.0f, 0.0f, 0, true), // owner, now out of drop range
            candidate(guid(2), 50.0f, 0.0f, 0, true),  // eligible taker within acquire
        };
        const auto next = ElectOwner(origin, guid(1), c, params, false);
        UEQUALS(raw(next), raw(guid(2)));
    });

    IT("returns UNASSIGNED when no candidate is within acquireRange", {
        std::vector<DelegationCandidate> c = {
            candidate(guid(1), 90.0f, 0.0f, 0, true),
            candidate(guid(2), 200.0f, 0.0f, 0, true),
        };
        const auto next = ElectOwner(origin, kNone, c, params, false);
        UEQUALS(raw(next), raw(kNone));
    });

    IT("orphans the entity when the drifted owner has no taker in range", {
        // Owner past drop and the only other candidate is out of acquire: falls back to the server.
        std::vector<DelegationCandidate> c = {
            candidate(guid(1), 140.0f, 0.0f, 0, true),
            candidate(guid(2), 120.0f, 0.0f, 0, true),
        };
        const auto next = ElectOwner(origin, guid(1), c, params, false);
        UEQUALS(raw(next), raw(kNone));
    });

    IT("ignores ineligible candidates (dimension / game veto)", {
        std::vector<DelegationCandidate> c = {
            candidate(guid(1), 20.0f, 0.0f, 0, false), // closest but vetoed
            candidate(guid(2), 70.0f, 0.0f, 0, true),
        };
        const auto next = ElectOwner(origin, kNone, c, params, false);
        UEQUALS(raw(next), raw(guid(2)));
    });

    IT("does not keep a current owner that has become ineligible", {
        // Owner still within drop range but vetoed/left the dimension: must hand off, not stick.
        std::vector<DelegationCandidate> c = {
            candidate(guid(1), 50.0f, 0.0f, 0, false),
            candidate(guid(2), 60.0f, 0.0f, 0, true),
        };
        const auto next = ElectOwner(origin, guid(1), c, params, false);
        UEQUALS(raw(next), raw(guid(2)));
    });

    IT("skips candidates at the load soft cap when a lighter one exists", {
        params.loadSoftCap = 2;
        std::vector<DelegationCandidate> c = {
            candidate(guid(1), 20.0f, 0.0f, 2, true), // closest but at the cap
            candidate(guid(2), 70.0f, 0.0f, 1, true), // under the cap
        };
        const auto next = ElectOwner(origin, kNone, c, params, false);
        UEQUALS(raw(next), raw(guid(2)));
        params.loadSoftCap = 0;
    });

    IT("keeps the current owner even when it is over the soft cap", {
        // The cap gates new grants, never revokes what a client already simulates.
        params.loadSoftCap = 1;
        std::vector<DelegationCandidate> c = {
            candidate(guid(1), 100.0f, 0.0f, 5, true), // owner in hysteresis band, over cap
            candidate(guid(2), 40.0f, 0.0f, 0, true),
        };
        const auto next = ElectOwner(origin, guid(1), c, params, false);
        UEQUALS(raw(next), raw(guid(1)));
        params.loadSoftCap = 0;
    });

    IT("measures distance on the XY plane when groundXY is set (Z-up)", {
        // A large Z separation must not count as distance on a Z-up game: the candidate is at XY
        // distance 0, so it is in range despite z=1000.
        std::vector<DelegationCandidate> c = {
            {guid(1), glm::vec3(0.0f, 0.0f, 1000.0f), 0, true},
        };
        const auto next = ElectOwner(origin, kNone, c, params, true);
        UEQUALS(raw(next), raw(guid(1)));
    });
});
