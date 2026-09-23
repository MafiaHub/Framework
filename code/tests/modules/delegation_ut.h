/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/replication/delegation.h"

#include <glm/glm.hpp>
#include <mafianet/VirtualWorld.h>
#include <mafianet/types.h>

#include <vector>

// The election rule behind delegated simulation: which client is asked to run an entity the server
// owns but cannot simulate. Exercised through DelegationManager::Elect, which is the rule with the
// manager, the clock and the entities taken out of it.
MODULE(delegation, {
    using Framework::Networking::Replication::DelegationCandidate;
    using Framework::Networking::Replication::DelegationManager;
    using Framework::Networking::Replication::DelegationPolicy;

    const MafiaNet::PeerGuid nobody = MafiaNet::UNASSIGNED_PEER_GUID;
    const MafiaNet::PeerGuid alice  = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(11));
    const MafiaNet::PeerGuid bob    = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(22));
    const MafiaNet::PeerGuid carol  = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(33));

    // Every case measures on XY, as a Z-up game does, and puts the entity at the origin so a
    // candidate's X is its distance.
    constexpr bool kGroundXY = true;
    const glm::vec3 entityAt(0.0f, 0.0f, 0.0f);

    DelegationPolicy policy;
    policy.acquireRange = 100.0f;
    policy.releaseRange = 140.0f;
    policy.weight       = 1;

    auto at = [](MafiaNet::PeerGuid guid, float x, uint32_t load = 0, uint32_t capacity = 0, MafiaNet::VirtualWorldId world = MafiaNet::VIRTUAL_WORLD_GLOBAL) {
        DelegationCandidate candidate;
        candidate.guid         = guid;
        candidate.position     = glm::vec3(x, 0.0f, 0.0f);
        candidate.virtualWorld = world;
        candidate.load         = load;
        candidate.capacity     = capacity;
        return candidate;
    };

    IT("leaves an entity dormant when nobody is near enough", {
        const std::vector<DelegationCandidate> candidates {at(alice, 150.0f), at(bob, 400.0f)};
        EQUALS(DelegationManager::Elect(policy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, nobody, candidates, kGroundXY), nobody);
    });

    IT("hands a dormant entity to the nearest client inside the acquire range", {
        const std::vector<DelegationCandidate> candidates {at(alice, 90.0f), at(bob, 30.0f), at(carol, 60.0f)};
        EQUALS(DelegationManager::Elect(policy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, nobody, candidates, kGroundXY), bob);
    });

    IT("keeps an entity with a simulator that has passed the acquire range but not the release range", {
        // 120 is outside acquire (100) and inside release (140): nobody may take it, and the client
        // that has it keeps it. This is the hysteresis, and without it a player standing at the
        // boundary would trade the entity on every pass.
        const std::vector<DelegationCandidate> candidates {at(alice, 120.0f)};
        EQUALS(DelegationManager::Elect(policy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, alice, candidates, kGroundXY), alice);
    });

    IT("takes an entity away from a simulator past the release range", {
        const std::vector<DelegationCandidate> candidates {at(alice, 160.0f)};
        EQUALS(DelegationManager::Elect(policy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, alice, candidates, kGroundXY), nobody);
    });

    IT("gives an entity leaving its simulator's range to another client in range", {
        const std::vector<DelegationCandidate> candidates {at(alice, 160.0f), at(bob, 40.0f)};
        EQUALS(DelegationManager::Elect(policy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, alice, candidates, kGroundXY), bob);
    });

    IT("does not hand an entity to a challenger that is barely nearer", {
        // Sticky ranking: the incumbent is compared at 72% of its squared distance, so a challenger
        // needs to be about 15% nearer. 50 against 48 is not enough.
        const std::vector<DelegationCandidate> candidates {at(alice, 50.0f), at(bob, 48.0f)};
        EQUALS(DelegationManager::Elect(policy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, alice, candidates, kGroundXY), alice);
    });

    IT("hands an entity to a challenger that is clearly nearer", {
        const std::vector<DelegationCandidate> candidates {at(alice, 50.0f), at(bob, 20.0f)};
        EQUALS(DelegationManager::Elect(policy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, alice, candidates, kGroundXY), bob);
    });

    IT("skips a client that is already at its simulation budget", {
        // Alice is nearer but full; the work goes to the next nearest rather than being dropped.
        const std::vector<DelegationCandidate> candidates {at(alice, 20.0f, 4, 4), at(bob, 60.0f, 0, 4)};
        EQUALS(DelegationManager::Elect(policy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, nobody, candidates, kGroundXY), bob);
    });

    IT("counts an entity's weight against the budget, not just its number", {
        DelegationPolicy heavy = policy;
        heavy.weight           = 3;
        // Two of four slots are free, which is not enough for a weight of three.
        const std::vector<DelegationCandidate> candidates {at(alice, 20.0f, 2, 4), at(bob, 60.0f, 0, 4)};
        EQUALS(DelegationManager::Elect(heavy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, nobody, candidates, kGroundXY), bob);
    });

    IT("leaves an entity with a simulator that is over budget", {
        // A budget lowered at runtime stops new work arriving; it does not stutter what is already
        // running by taking a body off the client that is mid-simulation.
        const std::vector<DelegationCandidate> candidates {at(alice, 30.0f, 9, 4)};
        EQUALS(DelegationManager::Elect(policy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, alice, candidates, kGroundXY), alice);
    });

    IT("ignores a client in another virtual world", {
        const std::vector<DelegationCandidate> candidates {at(alice, 20.0f, 0, 0, 7)};
        EQUALS(DelegationManager::Elect(policy, entityAt, 9, nobody, candidates, kGroundXY), nobody);
    });

    IT("accepts a client whose virtual world can see the entity's", {
        // The global world sees every other one, which is the same test replication streams by.
        const std::vector<DelegationCandidate> candidates {at(alice, 20.0f, 0, 0, 7)};
        EQUALS(DelegationManager::Elect(policy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, nobody, candidates, kGroundXY), alice);
    });

    IT("ignores virtual worlds when the policy says to", {
        DelegationPolicy crossWorld           = policy;
        crossWorld.requireSameVirtualWorld    = false;
        const std::vector<DelegationCandidate> candidates {at(alice, 20.0f, 0, 0, 7)};
        EQUALS(DelegationManager::Elect(crossWorld, entityAt, 9, nobody, candidates, kGroundXY), alice);
    });

    IT("measures on the ground plane rather than in three dimensions", {
        // A player 200 up a tower, 10 away on the ground, is near enough: height must not decide
        // who simulates what, or a body under a bridge loses its simulator to the bridge.
        DelegationCandidate high;
        high.guid         = alice;
        high.position     = glm::vec3(10.0f, 0.0f, 200.0f);
        high.virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
        EQUALS(DelegationManager::Elect(policy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, nobody, {high}, kGroundXY), alice);
    });

    IT("treats a release range below the acquire range as equal to it", {
        DelegationPolicy inverted = policy;
        inverted.acquireRange     = 100.0f;
        inverted.releaseRange     = 10.0f; // nonsense; Normalized() clamps it up to acquire
        const std::vector<DelegationCandidate> candidates {at(alice, 90.0f)};
        EQUALS(DelegationManager::Elect(inverted, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, alice, candidates, kGroundXY), alice);
    });

    IT("does not elect a client that is not a candidate at all", {
        // An entity whose simulator has disconnected: the guid is still on the entity, but the
        // viewer is gone, so it is not in the list and the entity goes dormant.
        const std::vector<DelegationCandidate> candidates {};
        EQUALS(DelegationManager::Elect(policy, entityAt, MafiaNet::VIRTUAL_WORLD_GLOBAL, alice, candidates, kGroundXY), nobody);
    });
})
