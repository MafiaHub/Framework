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

#include <algorithm>
#include <initializer_list>
#include <random>
#include <tuple>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// The server's relevance rule: per-entity streaming range, the stream-out margin, the velocity
// look-ahead and the per-type budget. See m2o's docs/design_streaming_range.md.
// Names its dependency by network id, as a game entity does.
class InterestDependentEntity final : public Framework::Networking::Replication::NetworkEntity {
  public:
    uint64_t dependency = 0;

    uint64_t GetInterestDependency() override {
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
    // No manager hands out ids here; a dependency is named by one.
    uint64_t nextId = 0;
    auto make       = [&owned, &nextId](uint32_t typeId, float x, float y) {
        NetworkEntity *entity = EntityRegistry::Get().Create(typeId);
        entity->SetNetworkID(++nextId);
        entity->position = glm::vec3(x, y, 0.0f);
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
        auto *rider       = static_cast<InterestDependentEntity *>(make(typeHolder, 200.0f, 0.0f));
        rider->dependency = farCar->GetNetworkID();

        rebuild(grid, {viewer, nearCar, farCar, rider});

        std::unordered_set<NetworkEntity *> out;
        out = collect(grid, viewer, nothingBefore);

        EQUALS(out.contains(rider), true);
        EQUALS(out.contains(nearCar), true); // the one budgeted slot, being nearest
        EQUALS(out.contains(farCar), true);  // uncounted, dragged in by its occupant
    });

    IT("resolves an interest dependency by id", {
        InterestGrid grid;
        freshGrid(grid);

        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 250.0f;
        // Out of the viewer's reach on its own; in it only as what the rider depends on.
        NetworkEntity *car      = make(typeA, 1000.0f, 0.0f);
        auto *rider             = static_cast<InterestDependentEntity *>(make(typeHolder, 100.0f, 0.0f));
        rider->dependency       = car->GetNetworkID();

        rebuild(grid, {viewer, car, rider});
        EQUALS(grid.DependencyOf(grid.SlotOf(rider)), grid.SlotOf(car));
        std::unordered_set<NetworkEntity *> out;
        out = collect(grid, viewer, nothingBefore);
        EQUALS(out.contains(rider), true);
        EQUALS(out.contains(car), true);

        // The rider itself out of reach: the car is shown only with it.
        rider->position = glm::vec3(1000.0f, 0.0f, 0.0f);
        rebuild(grid, {viewer, car, rider});
        out = collect(grid, viewer, nothingBefore);
        EQUALS(out.contains(rider), false);
        EQUALS(out.contains(car), false);

        // An id nothing answers to is no dependency.
        rider->position   = glm::vec3(100.0f, 0.0f, 0.0f);
        rider->dependency = 987654;
        rebuild(grid, {viewer, car, rider});
        EQUALS(grid.DependencyOf(grid.SlotOf(rider)), InterestGrid::kNoSlot);
        out = collect(grid, viewer, nothingBefore);
        EQUALS(out.contains(rider), true);
        EQUALS(out.contains(car), false);

        // Nor is the id of an entity that has left.
        rider->dependency = car->GetNetworkID();
        rebuild(grid, {viewer, car, rider});
        EQUALS(grid.DependencyOf(grid.SlotOf(rider)), grid.SlotOf(car));
        grid.Remove(car);
        rebuild(grid, {viewer, rider});
        EQUALS(grid.DependencyOf(grid.SlotOf(rider)), InterestGrid::kNoSlot);
        out = collect(grid, viewer, nothingBefore);
        EQUALS(out.contains(rider), true);
        EQUALS(out.contains(car), false);
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

    // The rule checked against itself computed by brute force, over randomized worlds: ranges, own
    // ranges, the stream-out margin, the look-ahead, budgets with sticky ranking, owners, targets,
    // always-visible, worlds, removals and dependencies.
    IT("returns what the relevance rule gives by brute force, for a set and for one entity", {
        const uint32_t typeOwnRange = EntityRegistry::Get().Register<NetworkEntity>("Test::InterestOwnRange");
        constexpr float kMargin     = 20.0f;
        constexpr uint32_t kBudget  = 3;
        InterestGrid grid;
        freshGrid(grid);
        grid.SetBudget(typeB, kBudget);
        grid.SetUsesEntityRange(typeOwnRange, true);
        grid.SetStreamOutMargin(kMargin);
        grid.SetLookaheadSeconds(1.0f);

        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> coord(-600.0f, 600.0f);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        NetworkEntity *viewer   = make(typeA, 0.0f, 0.0f);
        viewer->streaming.range = 200.0f;
        std::vector<NetworkEntity *> world;
        for (int i = 0; i < 400; ++i) {
            const float kind           = unit(rng);
            NetworkEntity *e           = make(kind < 0.1f ? typeHolder : kind < 0.4f ? typeA : kind < 0.7f ? typeB : typeOwnRange, coord(rng), coord(rng));
            e->streaming.range         = unit(rng) < 0.5f ? 0.0f : 50.0f + unit(rng) * 250.0f;
            e->streaming.visible       = unit(rng) > 0.05f;
            e->streaming.alwaysVisible = unit(rng) < 0.03f;
            const float owner          = unit(rng);
            e->ownerGUID               = owner < 0.05f ? viewerGuid : owner < 0.1f ? strangerGuid : MafiaNet::UNASSIGNED_PEER_GUID;
            if (unit(rng) < 0.05f) {
                e->streaming.targetGUID = unit(rng) < 0.5f ? viewerGuid : strangerGuid;
            }
            if (unit(rng) < 0.1f) {
                e->SetVirtualWorld(1);
            }
            world.push_back(e);
        }
        for (NetworkEntity *e : world) {
            if (e->GetTypeId() == typeHolder) {
                static_cast<InterestDependentEntity *>(e)->dependency = world[rng() % world.size()]->GetNetworkID();
            }
        }

        const auto admits = [&](const NetworkEntity *e) {
            return e->streaming.Admits(viewerGuid);
        };
        const auto byId = [&](uint64_t id) -> NetworkEntity * {
            for (NetworkEntity *e : world) {
                if (e->GetNetworkID() == id) {
                    return e;
                }
            }
            return nullptr;
        };
        const auto targeted = [](const NetworkEntity *e) {
            return e->streaming.targetGUID != MafiaNet::UNASSIGNED_PEER_GUID;
        };
        const auto sameWorld = [&](const NetworkEntity *e) {
            return MafiaNet::VirtualWorldsCanSee(e->GetVirtualWorld(), viewer->GetVirtualWorld());
        };
        const auto bruteForce = [&](const std::unordered_set<NetworkEntity *> &live, const std::unordered_set<NetworkEntity *> &previous) {
            std::vector<glm::vec2> focus {glm::vec2(viewer->position)};
            const glm::vec2 lean = glm::vec2(viewer->velocity);
            if (glm::dot(lean, lean) > 25.0f) {
                focus.push_back(glm::vec2(viewer->position) + lean);
            }
            const auto distSq = [&](const NetworkEntity *e) {
                float best = 1e30f;
                for (const glm::vec2 &f : focus) {
                    const glm::vec2 d = glm::vec2(e->position) - f;
                    best              = std::min(best, glm::dot(d, d));
                }
                return best;
            };
            std::unordered_set<NetworkEntity *> out;
            std::unordered_set<NetworkEntity *> inRange;
            std::vector<std::pair<float, NetworkEntity *>> ranked;
            for (NetworkEntity *e : live) {
                if (e == viewer || e->ownerGUID == viewerGuid || e->streaming.alwaysVisible || !e->streaming.visible || !admits(e)) {
                    continue;
                }
                if (!targeted(e) && !sameWorld(e)) {
                    continue;
                }
                const bool was = previous.contains(e);
                float range    = e->GetTypeId() == typeOwnRange ? e->streaming.range : std::max(viewer->streaming.range, e->streaming.range);
                range += was ? kMargin : 0.0f;
                const float d = distSq(e);
                if (d > range * range) {
                    continue;
                }
                inRange.insert(e);
                if (e->GetTypeId() == typeB) {
                    ranked.push_back({was ? d * 0.72f : d, e});
                }
                else {
                    out.insert(e);
                }
            }
            std::sort(ranked.begin(), ranked.end());
            for (size_t i = 0; i < ranked.size() && i < kBudget; ++i) {
                out.insert(ranked[i].second);
            }
            for (NetworkEntity *e : live) {
                const bool always = e->streaming.alwaysVisible && (targeted(e) || sameWorld(e));
                if ((e->ownerGUID == viewerGuid || always) && e->streaming.visible && admits(e)) {
                    out.insert(e);
                }
            }
            out.insert(viewer);
            std::unordered_set<NetworkEntity *> direct = out;
            for (NetworkEntity *e : direct) {
                NetworkEntity *dependency = byId(e->GetInterestDependency());
                if (dependency && live.contains(dependency) && dependency->streaming.visible && admits(dependency)) {
                    out.insert(dependency);
                }
            }
            return std::make_tuple(std::move(direct), std::move(out), std::move(inRange));
        };

        std::unordered_set<NetworkEntity *> previous;
        int mismatches = 0;
        int admitted   = 0;
        int recomputes = 0;
        size_t seen    = 0;
        for (int step = 0; step < 30; ++step) {
            viewer->velocity = glm::vec3(coord(rng) * 0.1f, coord(rng) * 0.1f, 0.0f);
            viewer->position += glm::vec3(coord(rng) * 0.05f, coord(rng) * 0.05f, 0.0f);
            for (NetworkEntity *e : world) {
                e->position += glm::vec3(coord(rng) * 0.02f, coord(rng) * 0.02f, 0.0f);
            }
            std::unordered_set<NetworkEntity *> live {viewer};
            grid.BeginSync();
            grid.Sync(viewer);
            for (NetworkEntity *e : world) {
                grid.Sync(e);
                live.insert(e);
            }
            grid.EndSync();
            for (NetworkEntity *e : world) {
                if (unit(rng) < 0.03f) {
                    grid.Remove(e);
                    live.erase(e);
                }
            }
            const std::unordered_set<NetworkEntity *> out = collect(grid, viewer, previous);
            const auto [direct, expected, inRange]        = bruteForce(live, previous);
            mismatches += out.size() != expected.size();
            for (NetworkEntity *e : expected) {
                mismatches += !out.contains(e);
            }

            // One entity at a time, as a creation is decided: in on its own right, a recompute for a
            // budgeted one that came into reach, out otherwise.
            for (NetworkEntity *e : world) {
                const uint32_t slot = grid.SlotOf(e);
                if (slot == InterestGrid::kNoSlot || previous.contains(e)) {
                    continue;
                }
                const bool budgeted = e->GetTypeId() == typeB && e->ownerGUID != viewerGuid && !e->streaming.alwaysVisible;
                const auto want     = budgeted && inRange.contains(e) ? InterestGrid::Verdict::Recompute : (direct.contains(e) && !budgeted ? InterestGrid::Verdict::In : InterestGrid::Verdict::Out);
                const auto verdict  = grid.Evaluate(viewer, viewerGuid, slot, false);
                mismatches += verdict != want;
                admitted += verdict == InterestGrid::Verdict::In;
                recomputes += verdict == InterestGrid::Verdict::Recompute;
            }
            seen += out.size();
            previous = out;
        }
        EQUALS(mismatches, 0);
        GREATER(seen, size_t {30 * 40});
        GREATER(admitted, 30);
        GREATER(recomputes, 0);
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
