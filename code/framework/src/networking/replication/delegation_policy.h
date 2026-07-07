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
    // Tunables for proximity syncer election. Distances are ground-plane world units. acquireRange <
    // dropRange gives hysteresis: a candidate must reach acquireRange to be elected, but the current
    // owner is held until it drifts past dropRange, so a boundary owner doesn't thrash.
    struct DelegationParams {
        float acquireRange          = 80.0f;
        float dropRange             = 130.0f;
        uint32_t electionIntervalMs = 500;
        int loadSoftCap             = 0; // max delegated entities newly granted per client (0 = off)
    };

    struct DelegationCandidate {
        MafiaNet::PeerGuid guid = MafiaNet::UNASSIGNED_PEER_GUID;
        glm::vec3 position {0.0f};
        int ownedLoad = 0;
        bool eligible = true; // caller-applied dimension + game veto
    };

    // Pure, networking-free election. Keeps the current owner within dropRange, else elects the
    // least-loaded eligible candidate within acquireRange (nearest breaking ties), else UNASSIGNED.
    // groundXY picks the second ground axis (Y for Z-up, else Z).
    inline MafiaNet::PeerGuid ElectOwner(const glm::vec3 &entityPos, MafiaNet::PeerGuid currentOwner, const std::vector<DelegationCandidate> &candidates, const DelegationParams &params, bool groundXY) {
        const auto ground = [groundXY](const glm::vec3 &p) {
            return groundXY ? p.y : p.z;
        };
        const auto dist2 = [&](const glm::vec3 &p) {
            const float dx = p.x - entityPos.x;
            const float dv = ground(p) - ground(entityPos);
            return dx * dx + dv * dv;
        };

        // Hysteresis: hold the current owner while eligible and within dropRange (soft cap ignored).
        if (currentOwner != MafiaNet::UNASSIGNED_PEER_GUID) {
            const float dropSq = params.dropRange * params.dropRange;
            for (const auto &candidate : candidates) {
                if (candidate.guid != currentOwner) {
                    continue;
                }
                if (candidate.eligible && dist2(candidate.position) <= dropSq) {
                    return currentOwner;
                }
                break;
            }
        }

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
