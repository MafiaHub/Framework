/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/replication/entity_registry.h"
#include "networking/replication/interest_grid.h"
#include "networking/replication/network_entity.h"

#include <glm/glm.hpp>
#include <mafianet/types.h>

#include <initializer_list>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// The server's relevance rule: per-entity streaming range, the stream-out margin, the velocity
// look-ahead and the per-type budget. See m2o's docs/design_streaming_range.md.
// Dependency set directly, so the post-pass is testable without a manager behind ResolveSibling.
class InterestDependentEntity final : public Framework::Networking::Replication::NetworkEntity {
  public:
    Framework::Networking::Replication::NetworkEntity *dependency = nullptr;

    Framework::Networking::Replication::NetworkEntity *GetInterestDependency() override {
        return dependency;
    }
};

MODULE(interest_grid, {
    using Framework::Networking::Replication::EntityRegistry;
    using Framework::Networking::Replication::InterestGrid;
    using Framework::Networking::Replication::NetworkEntity;

    // Type ids are stamped by the registry, so entities under test are built through it.
    const uint32_t typeA      = EntityRegistry::Get().Register<NetworkEntity>("Test::InterestA");
    const uint32_t typeB      = EntityRegistry::Get().Register<NetworkEntity>("Test::InterestB");
    const uint32_t typeHolder = EntityRegistry::Get().Register<InterestDependentEntity>("Test::InterestHolder");

    // Owned by the test, not by a manager; the grid only stores raw pointers.
    std::vector<std::unique_ptr<NetworkEntity>> owned;
    auto make = [&owned](uint32_t typeId, float x, float y) {
        NetworkEntity *entity = EntityRegistry::Get().Create(typeId);
        entity->position      = glm::vec3(x, y, 0.0f);
        owned.emplace_back(entity);
        return entity;
    };

    const MafiaNet::PeerGuid viewerGuid   = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(7));
    const MafiaNet::PeerGuid strangerGuid = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(9));
    const std::unordered_set<NetworkEntity *> nothingBefore;

    // Measure on XY, leaving Z as height, as m2o does.
    auto freshGrid = [](InterestGrid &grid) {
        grid.SetGroundPlaneXY(true);
        grid.Configure(100.0f, -4096.0f, 4096.0f);
    };

    // A tick's pass over exactly these entities, as ReplicationManager::RebuildInterest makes it.
    auto rebuild = [](InterestGrid &grid, std::initializer_list<NetworkEntity *> entities) {
        grid.BeginSync();
        for (NetworkEntity *entity : entities) {
            grid.Sync(entity);
        }
        grid.EndSync();
    };
    // The viewer's set as entities, from its previous one.
    auto collect = [viewerGuid](const InterestGrid &grid, NetworkEntity *viewer, const std::unordered_set<NetworkEntity *> &before) {
        Framework::Networking::Replication::SlotSet previous;
        for (NetworkEntity *entity : before) {
            if (grid.SlotOf(entity) != InterestGrid::kNoSlot) {
                previous.Set(grid.SlotOf(entity));
            }
        }
        InterestGrid::Scratch scratch;
        std::vector<uint32_t> slots;
        grid.CollectVisible(viewer, viewerGuid, previous, scratch, slots);
        std::unordered_set<NetworkEntity *> out;
        for (const uint32_t slot : slots) {
            out.insert(grid.EntityAt(slot));
        }
        return out;
    };

    IT("streams an entity in within the viewer's range and not beyond it", {
        InterestGrid grid;
        freshGrid(grid);

        NetworkEntity *viewer  = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        NetworkEntity *nearby    = make(typeA, 200.0f, 0.0f);
        NetworkEntity *distant     = make(typeA, 400.0f, 0.0f);

        rebuild(grid, {viewer, nearby, distant});

        std::unordered_set<NetworkEntity *> out;
        out = collect(grid, viewer, nothingBefore);

        EQUALS(out.contains(nearby), true);
        EQUALS(out.contains(distant), false);
        EQUALS(out.contains(viewer), true);
    });

    IT("honours an entity's own streaming range when it reaches further than the viewer's", {
        InterestGrid grid;
        freshGrid(grid);

        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        NetworkEntity *farCar   = make(typeB, 340.0f, 0.0f);
        farCar->streaming.range = 350.0f;
        // Same distance, but content with the viewer's radius.
        NetworkEntity *farPed = make(typeB, 340.0f, 10.0f);

        rebuild(grid, {viewer, farCar, farPed});

        std::unordered_set<NetworkEntity *> out;
        out = collect(grid, viewer, nothingBefore);

        EQUALS(out.contains(farCar), true);
        EQUALS(out.contains(farPed), false);
    });

    IT("holds a type that uses its entity range to that range, even under a longer viewer range", {
        InterestGrid grid;
        freshGrid(grid);
        grid.SetStreamOutMargin(10.0f);
        grid.SetUsesEntityRange(typeB, true);

        NetworkEntity *viewer    = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range  = 250.0f;
        NetworkEntity *nearChest = make(typeB, 40.0f, 0.0f);
        nearChest->streaming.range = 50.0f;
        NetworkEntity *farChest  = make(typeB, 100.0f, 0.0f);
        farChest->streaming.range = 50.0f;
        // Its own range reaches further than the viewer's, and still counts in full.
        NetworkEntity *wideChest = make(typeB, 300.0f, 0.0f);
        wideChest->streaming.range = 320.0f;
        // Another type at the same distance keeps the viewer's range.
        NetworkEntity *otherType = make(typeA, 100.0f, 10.0f);
        otherType->streaming.range = 50.0f;
        // Already streamed and just past its range: the margin keeps it.
        NetworkEntity *leaving = make(typeB, 55.0f, 10.0f);
        leaving->streaming.range = 50.0f;

        rebuild(grid, {viewer, nearChest, farChest, wideChest, otherType, leaving});

        const std::unordered_set<NetworkEntity *> before {leaving};
        std::unordered_set<NetworkEntity *> out;
        out = collect(grid, viewer, before);

        EQUALS(out.contains(nearChest), true);
        EQUALS(out.contains(farChest), false);
        EQUALS(out.contains(wideChest), true);
        EQUALS(out.contains(otherType), true);
        EQUALS(out.contains(leaving), true);

        // Turned back off, the viewer's range widens it again from the next rebuild.
        grid.SetUsesEntityRange(typeB, false);
        rebuild(grid, {viewer, farChest});
        out.clear();
        out = collect(grid, viewer, nothingBefore);
        EQUALS(out.contains(farChest), true);
    });

    IT("keeps an already-streamed entity out to range + margin, but will not admit a new one there", {
        InterestGrid grid;
        freshGrid(grid);
        grid.SetStreamOutMargin(50.0f);

        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        // In the margin band: past the stream-in radius, inside the stream-out radius.
        NetworkEntity *pacing = make(typeA, 270.0f, 0.0f);

        rebuild(grid, {viewer, pacing});

        // Not previously relevant: the margin must not pull it in early.
        std::unordered_set<NetworkEntity *> cold;
        cold = collect(grid, viewer, nothingBefore);
        EQUALS(cold.contains(pacing), false);

        // Previously relevant: it holds.
        std::unordered_set<NetworkEntity *> previous;
        previous.insert(pacing);
        std::unordered_set<NetworkEntity *> warm;
        warm = collect(grid, viewer, previous);
        EQUALS(warm.contains(pacing), true);

        // Past the margin it goes regardless of history.
        pacing->position = glm::vec3(310.0f, 0.0f, 0.0f);
        rebuild(grid, {viewer, pacing});
        std::unordered_set<NetworkEntity *> gone;
        gone = collect(grid, viewer, previous);
        EQUALS(gone.contains(pacing), false);
    });

    IT("leans interest along the viewer's velocity without dropping what is behind", {
        InterestGrid grid;
        freshGrid(grid);
        grid.SetLookaheadSeconds(1.0f);

        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        viewer->velocity        = glm::vec3(40.0f, 0.0f, 0.0f); // driving +X
        // Past the plain radius, inside it from the look-ahead point.
        NetworkEntity *ahead = make(typeA, 280.0f, 0.0f);
        // Behind, inside the plain radius: measuring only from the look-ahead point would lose it.
        NetworkEntity *behind = make(typeA, -240.0f, 0.0f);

        rebuild(grid, {viewer, ahead, behind});

        std::unordered_set<NetworkEntity *> out;
        out = collect(grid, viewer, nothingBefore);

        EQUALS(out.contains(ahead), true);
        EQUALS(out.contains(behind), true);
    });

    IT("caps a budgeted type at the nearest N and leaves other types alone", {
        InterestGrid grid;
        freshGrid(grid);
        grid.SetBudget(typeA, 2);

        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        NetworkEntity *a10  = make(typeA, 10.0f, 0.0f);
        NetworkEntity *a20  = make(typeA, 20.0f, 0.0f);
        NetworkEntity *a30  = make(typeA, 30.0f, 0.0f);
        NetworkEntity *b100 = make(typeB, 100.0f, 0.0f);
        NetworkEntity *b110 = make(typeB, 110.0f, 0.0f);
        NetworkEntity *b120 = make(typeB, 120.0f, 0.0f);

        rebuild(grid, {viewer, a10, a20, a30, b100, b110, b120});

        std::unordered_set<NetworkEntity *> out;
        out = collect(grid, viewer, nothingBefore);

        EQUALS(out.contains(a10), true);
        EQUALS(out.contains(a20), true);
        EQUALS(out.contains(a30), false);
        // An unset budget must change nothing.
        EQUALS(out.contains(b100), true);
        EQUALS(out.contains(b110), true);
        EQUALS(out.contains(b120), true);
    });

    IT("holds a budgeted slot against a marginally closer challenger", {
        InterestGrid grid;
        freshGrid(grid);
        grid.SetBudget(typeA, 1);

        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        NetworkEntity *incumbent = make(typeA, 100.0f, 0.0f);
        // 5% nearer: not enough to justify a despawn/respawn pair.
        NetworkEntity *challenger = make(typeA, 95.0f, 0.0f);

        rebuild(grid, {viewer, incumbent, challenger});

        std::unordered_set<NetworkEntity *> previous;
        previous.insert(incumbent);
        std::unordered_set<NetworkEntity *> out;
        out = collect(grid, viewer, previous);

        EQUALS(out.contains(incumbent), true);
        EQUALS(out.contains(challenger), false);

        // Decisively closer: the swap happens, so the cap cannot wedge.
        challenger->position = glm::vec3(40.0f, 0.0f, 0.0f);
        rebuild(grid, {viewer, incumbent, challenger});
        std::unordered_set<NetworkEntity *> swapped;
        swapped = collect(grid, viewer, previous);
        EQUALS(swapped.contains(challenger), true);
        EQUALS(swapped.contains(incumbent), false);
    });

    // The defect this guards: CollectVisible skips an entity owned by the viewer from ranking and
    // re-adds it from the owned index instead. Ownership is granted by delegation *after*
    // the interest step has already filed it, and the next sync is a tick away -- so without
    // Reown the entity is in neither set until then and the peer that was just told to
    // simulate it has its replica destroyed and rebuilt. Seen live as an NPC despawning and
    // respawning every few seconds on the simulating client only.
    IT("keeps an entity relevant to the viewer it is granted to after the rebuild", {
        InterestGrid grid;
        freshGrid(grid);

        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        // Far outside any streaming range, so only ownership can keep it relevant -- which is the
        // case a delegated entity is in the moment it is handed over near the edge of the world.
        NetworkEntity *granted  = make(typeA, 5000.0f, 0.0f);

        rebuild(grid, {viewer, granted});

        std::unordered_set<NetworkEntity *> before;
        before = collect(grid, viewer, nothingBefore);
        EQUALS(before.contains(granted), false);

        // The grant, as delegation makes it: ownerGUID written, then the grid told -- with no
        // rebuild in between.
        granted->ownerGUID = viewerGuid;
        grid.Reown(granted, MafiaNet::UNASSIGNED_PEER_GUID);

        std::unordered_set<NetworkEntity *> after;
        after = collect(grid, viewer, before);
        EQUALS(after.contains(granted), true);
    });

    IT("stops treating an entity as owned once it is handed back", {
        InterestGrid grid;
        freshGrid(grid);

        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        NetworkEntity *released = make(typeA, 5000.0f, 0.0f);
        released->ownerGUID     = viewerGuid;

        rebuild(grid, {viewer, released});

        std::unordered_set<NetworkEntity *> owned;
        owned = collect(grid, viewer, nothingBefore);
        EQUALS(owned.contains(released), true);

        released->ownerGUID = MafiaNet::UNASSIGNED_PEER_GUID;
        grid.Reown(released, viewerGuid);

        std::unordered_set<NetworkEntity *> handedBack;
        handedBack = collect(grid, viewer, owned);
        EQUALS(handedBack.contains(released), false);
    });

    IT("never counts owned or always-visible entities against a budget", {
        InterestGrid grid;
        freshGrid(grid);
        grid.SetBudget(typeA, 1);

        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        NetworkEntity *mine       = make(typeA, 5.0f, 0.0f);
        mine->ownerGUID           = viewerGuid;
        NetworkEntity *global     = make(typeA, 3000.0f, 0.0f);
        global->streaming.alwaysVisible = true;
        NetworkEntity *stranger   = make(typeA, 50.0f, 0.0f);
        stranger->ownerGUID       = strangerGuid;

        rebuild(grid, {viewer, mine, global, stranger});

        std::unordered_set<NetworkEntity *> out;
        out = collect(grid, viewer, nothingBefore);

        EQUALS(out.contains(mine), true);
        EQUALS(out.contains(global), true);
        EQUALS(out.contains(stranger), true); // still gets the one budgeted slot
    });

    IT("pulls a survivor's dependency in with it, past the budget", {
        InterestGrid grid;
        freshGrid(grid);
        grid.SetBudget(typeB, 1);

        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        NetworkEntity *nearCar = make(typeB, 20.0f, 0.0f);
        NetworkEntity *farCar  = make(typeB, 200.0f, 0.0f);

        // A seated player out by the distant car: it must come with them or the ped floats.
        auto *rider       = static_cast<InterestDependentEntity *>(EntityRegistry::Get().Create(typeHolder));
        rider->position   = glm::vec3(200.0f, 0.0f, 0.0f);
        rider->dependency = farCar;
        owned.emplace_back(rider);

        rebuild(grid, {viewer, nearCar, farCar, rider});

        std::unordered_set<NetworkEntity *> out;
        out = collect(grid, viewer, nothingBefore);

        EQUALS(out.contains(rider), true);
        EQUALS(out.contains(nearCar), true); // the one budgeted slot, being nearest
        EQUALS(out.contains(farCar), true);  // uncounted, dragged in by its occupant
    });

    IT("skips an entity destroyed since the last rebuild", {
        InterestGrid grid;
        freshGrid(grid);

        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        NetworkEntity *doomed   = make(typeA, 50.0f, 0.0f);

        rebuild(grid, {viewer, doomed});
        grid.Remove(doomed);

        std::unordered_set<NetworkEntity *> out;
        out = collect(grid, viewer, nothingBefore);
        EQUALS(out.contains(doomed), false);
    });

    IT("narrows an entity to the connections its viewer filter admits", {
        using Framework::Networking::Replication::ViewerFilter;
        InterestGrid grid;
        freshGrid(grid);

        NetworkEntity *viewer           = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range         = 250.0f;
        NetworkEntity *nearby           = make(typeA, 50.0f, 0.0f);
        NetworkEntity *global           = make(typeA, 3000.0f, 0.0f);
        global->streaming.alwaysVisible = true;

        rebuild(grid, {viewer, nearby, global});

        const auto sees = [&](NetworkEntity *entity) {
            return collect(grid, viewer, nothingBefore).contains(entity);
        };

        nearby->streaming.viewers                 = std::make_unique<ViewerFilter>();
        nearby->streaming.viewers->exceptFollowed = strangerGuid;
        EQUALS(sees(nearby), true);
        nearby->streaming.viewers->exceptFollowed = viewerGuid;
        EQUALS(sees(nearby), false);
        nearby->streaming.viewers->exceptFollowed = MafiaNet::UNASSIGNED_PEER_GUID;

        nearby->streaming.viewers->only.emplace();
        EQUALS(sees(nearby), false); // an empty showTo is nobody, not everyone
        nearby->streaming.viewers->only->insert(viewerGuid);
        EQUALS(sees(nearby), true);
        nearby->streaming.viewers->except.insert(viewerGuid);
        EQUALS(sees(nearby), false);

        global->streaming.viewers = std::make_unique<ViewerFilter>();
        global->streaming.viewers->except.insert(viewerGuid);
        EQUALS(sees(global), false);

        // A closed connection leaves nothing behind: the next one to hold that guid starts clean.
        nearby->streaming.viewers->exceptFollowed = viewerGuid;
        nearby->streaming.Forget(viewerGuid);
        global->streaming.Forget(viewerGuid);
        EQUALS(nearby->streaming.viewers->exceptFollowed == MafiaNet::UNASSIGNED_PEER_GUID, true);
        EQUALS(nearby->streaming.viewers->only->empty(), true);
        EQUALS(global->streaming.viewers == nullptr, true);
        EQUALS(sees(global), true);
    });
})
