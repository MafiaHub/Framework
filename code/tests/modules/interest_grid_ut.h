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

    IT("streams an entity in within the viewer's range and not beyond it", {
        InterestGrid grid;
        freshGrid(grid);

        NetworkEntity *viewer  = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        NetworkEntity *nearby    = make(typeA, 200.0f, 0.0f);
        NetworkEntity *distant     = make(typeA, 400.0f, 0.0f);

        grid.BeginRebuild();
        grid.Insert(viewer);
        grid.Insert(nearby);
        grid.Insert(distant);

        std::unordered_set<NetworkEntity *> out;
        grid.CollectVisible(viewer, viewerGuid, nothingBefore, out);

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

        grid.BeginRebuild();
        grid.Insert(viewer);
        grid.Insert(farCar);
        grid.Insert(farPed);

        std::unordered_set<NetworkEntity *> out;
        grid.CollectVisible(viewer, viewerGuid, nothingBefore, out);

        EQUALS(out.contains(farCar), true);
        EQUALS(out.contains(farPed), false);
    });

    IT("keeps an already-streamed entity out to range + margin, but will not admit a new one there", {
        InterestGrid grid;
        freshGrid(grid);
        grid.SetStreamOutMargin(50.0f);

        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        // In the margin band: past the stream-in radius, inside the stream-out radius.
        NetworkEntity *pacing = make(typeA, 270.0f, 0.0f);

        grid.BeginRebuild();
        grid.Insert(viewer);
        grid.Insert(pacing);

        // Not previously relevant: the margin must not pull it in early.
        std::unordered_set<NetworkEntity *> cold;
        grid.CollectVisible(viewer, viewerGuid, nothingBefore, cold);
        EQUALS(cold.contains(pacing), false);

        // Previously relevant: it holds.
        std::unordered_set<NetworkEntity *> previous;
        previous.insert(pacing);
        std::unordered_set<NetworkEntity *> warm;
        grid.CollectVisible(viewer, viewerGuid, previous, warm);
        EQUALS(warm.contains(pacing), true);

        // Past the margin it goes regardless of history.
        pacing->position = glm::vec3(310.0f, 0.0f, 0.0f);
        grid.BeginRebuild();
        grid.Insert(viewer);
        grid.Insert(pacing);
        std::unordered_set<NetworkEntity *> gone;
        grid.CollectVisible(viewer, viewerGuid, previous, gone);
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

        grid.BeginRebuild();
        grid.Insert(viewer);
        grid.Insert(ahead);
        grid.Insert(behind);

        std::unordered_set<NetworkEntity *> out;
        grid.CollectVisible(viewer, viewerGuid, nothingBefore, out);

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

        grid.BeginRebuild();
        grid.Insert(viewer);
        grid.Insert(a10);
        grid.Insert(a20);
        grid.Insert(a30);
        grid.Insert(b100);
        grid.Insert(b110);
        grid.Insert(b120);

        std::unordered_set<NetworkEntity *> out;
        grid.CollectVisible(viewer, viewerGuid, nothingBefore, out);

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

        grid.BeginRebuild();
        grid.Insert(viewer);
        grid.Insert(incumbent);
        grid.Insert(challenger);

        std::unordered_set<NetworkEntity *> previous;
        previous.insert(incumbent);
        std::unordered_set<NetworkEntity *> out;
        grid.CollectVisible(viewer, viewerGuid, previous, out);

        EQUALS(out.contains(incumbent), true);
        EQUALS(out.contains(challenger), false);

        // Decisively closer: the swap happens, so the cap cannot wedge.
        challenger->position = glm::vec3(40.0f, 0.0f, 0.0f);
        grid.BeginRebuild();
        grid.Insert(viewer);
        grid.Insert(incumbent);
        grid.Insert(challenger);
        std::unordered_set<NetworkEntity *> swapped;
        grid.CollectVisible(viewer, viewerGuid, previous, swapped);
        EQUALS(swapped.contains(challenger), true);
        EQUALS(swapped.contains(incumbent), false);
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

        grid.BeginRebuild();
        grid.Insert(viewer);
        grid.Insert(mine);
        grid.Insert(global);
        grid.Insert(stranger);

        std::unordered_set<NetworkEntity *> out;
        grid.CollectVisible(viewer, viewerGuid, nothingBefore, out);

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

        grid.BeginRebuild();
        grid.Insert(viewer);
        grid.Insert(nearCar);
        grid.Insert(farCar);
        grid.Insert(rider);

        std::unordered_set<NetworkEntity *> out;
        grid.CollectVisible(viewer, viewerGuid, nothingBefore, out);

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

        grid.BeginRebuild();
        grid.Insert(viewer);
        grid.Insert(doomed);
        grid.Remove(doomed);

        std::unordered_set<NetworkEntity *> out;
        grid.CollectVisible(viewer, viewerGuid, nothingBefore, out);
        EQUALS(out.contains(doomed), false);
    });
})
