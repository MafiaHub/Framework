/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/types.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Framework::Networking::Replication {
    // Tunables for proximity syncer election. Distances are world units, measured on the ground plane
    // (see ElectOwner's groundXY). The acquire/drop split is the anti-thrash hysteresis: a candidate
    // must come within acquireRange to be newly elected, but the current owner is kept until it drifts
    // past the wider dropRange — so an owner hovering at the boundary is not dropped-and-reacquired
    // every pass.
    struct DelegationParams {
        float acquireRange          = 80.0f;
        float dropRange             = 130.0f;
        uint32_t electionIntervalMs = 500;
        // Soft cap on how many delegated entities one client is newly given (0 = unlimited). The load
        // balancer already prefers the least-loaded candidate; this refuses to pile more onto a client
        // already at the cap when a lighter one exists, but never revokes what it holds.
        int loadSoftCap = 0;
    };

    // One election candidate: a connected client that could simulate the entity, with its viewer
    // position, how many delegated entities it already owns (load balancing), and whether the game or
    // dimension check vetoes it (applied by the caller before handing the candidate in).
    struct DelegationCandidate {
        MafiaNet::PeerGuid guid = MafiaNet::UNASSIGNED_PEER_GUID;
        glm::vec3 position {0.0f};
        int ownedLoad = 0;
        bool eligible = true;
    };

    // Pure election decision — no networking, so it is unit-testable in isolation and the same rule
    // serves any Framework mod. Given the entity position, its current owner, and the candidate set,
    // returns the guid that should own it next:
    //   - keeps the current owner while it stays eligible and within dropRange (hysteresis),
    //   - otherwise elects the best candidate within acquireRange (fewest owned, nearest as tiebreak),
    //   - returns UNASSIGNED when none qualifies, so the server keeps the entity (frozen).
    // groundXY selects the second ground-plane axis (Y for Z-up games like Mafia 2, else Z) so height
    // never bleeds into the distance test.
    inline MafiaNet::PeerGuid ElectOwner(const glm::vec3 &entityPos, MafiaNet::PeerGuid currentOwner, const std::vector<DelegationCandidate> &candidates, const DelegationParams &params, bool groundXY) {
        const auto ground = [groundXY](const glm::vec3 &p) {
            return groundXY ? p.y : p.z;
        };
        const auto dist2 = [&](const glm::vec3 &p) {
            const float dx = p.x - entityPos.x;
            const float dv = ground(p) - ground(entityPos);
            return dx * dx + dv * dv;
        };

        // Hold the current owner while it is still eligible and inside the wider drop radius. Load is
        // not re-checked here: an owner keeps what it already simulates regardless of the soft cap.
        if (currentOwner != MafiaNet::UNASSIGNED_PEER_GUID) {
            const float dropSq = params.dropRange * params.dropRange;
            for (const auto &candidate : candidates) {
                if (candidate.guid != currentOwner) {
                    continue;
                }
                if (candidate.eligible && dist2(candidate.position) <= dropSq) {
                    return currentOwner;
                }
                break; // found the current owner but it no longer qualifies: fall through to re-elect
            }
        }

        // Elect afresh: the least-loaded eligible candidate within acquireRange, nearest breaking ties.
        const float acquireSq        = params.acquireRange * params.acquireRange;
        MafiaNet::PeerGuid best      = MafiaNet::UNASSIGNED_PEER_GUID;
        int bestLoad                 = 0;
        float bestDist               = 0.0f;
        for (const auto &candidate : candidates) {
            if (!candidate.eligible || candidate.guid == MafiaNet::UNASSIGNED_PEER_GUID) {
                continue;
            }
            const float d = dist2(candidate.position);
            if (d > acquireSq) {
                continue;
            }
            if (params.loadSoftCap > 0 && candidate.ownedLoad >= params.loadSoftCap) {
                continue;
            }
            const bool better = best == MafiaNet::UNASSIGNED_PEER_GUID || candidate.ownedLoad < bestLoad || (candidate.ownedLoad == bestLoad && d < bestDist);
            if (better) {
                best     = candidate.guid;
                bestLoad = candidate.ownedLoad;
                bestDist = d;
            }
        }
        return best;
    }
} // namespace Framework::Networking::Replication
