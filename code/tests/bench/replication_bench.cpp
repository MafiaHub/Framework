/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// What a server tick's replication costs as players gather: interest (RebuildInterest) plus the
// ReplicaManager3 pass that constructs, destroys and serializes for every connection (Update). No
// sockets: connections are in memory and sends go nowhere, so this is the server thread's own work.
//
// Players and NPCs crowd a hub a few hundred metres across and move every tick, with the streaming
// setup of a game in centimetres (100 m cells, 500 m view, 50 m margin, 1 s look-ahead, NPCs
// budgeted). Build it in RelWithDebInfo or Release; a Debug number says little.
//
//   FrameworkReplicationBench [players...]     default: 50 100 164 250

#include "../replication_test_world.h"
#include "logging/logger.h"
#include "networking/network_server.h"
#include "networking/replication/replication_connection.h"
#include "networking/replication/replication_manager.h"

#include <mafianet/get_time.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {
    using namespace Framework::Networking::Replication;
    using Clock = std::chrono::steady_clock;

    // ReplicaManager3 serializes once its interval has passed on its clock; stepping that clock one
    // tick per tick makes every measured tick a full pass, as on a server ticking at that rate.
    constexpr MafiaNet::Time kTickMs = 16;
    std::atomic<MafiaNet::TimeUS> g_nowUS {1000000000};
    MafiaNet::TimeUS Now() {
        return g_nowUS.load();
    }

    constexpr int kWarmupTicks   = 60;
    constexpr int kMeasuredTicks = 300;
    constexpr int kNpcs          = 600;
    constexpr float kHubRadius   = 30000.0f; // 300 m

    struct Result {
        double meanMs, p50Ms, p99Ms, maxMs, interestMs, passMs;
    };

    // Builds before SetReplicationWorkerThreads existed have no workers to set, so the same file
    // measures them too.
    template <typename Manager>
    void SetWorkers(Manager *manager, int workers) {
        if constexpr (requires { manager->SetReplicationWorkerThreads(0u); }) {
            if (workers >= 0) {
                manager->SetReplicationWorkerThreads(static_cast<uint32_t>(workers));
            }
        }
    }

    double Percentile(std::vector<double> samples, double p) {
        std::sort(samples.begin(), samples.end());
        return samples[std::min(samples.size() - 1, static_cast<size_t>(p * samples.size()))];
    }

    Result Run(int players, int workers) {
        Framework::Networking::NetworkServer peer;
        ReplicationManager *manager = peer.GetReplicationManager();
        manager->Init(&peer, true);
        manager->SetAutoSerializeInterval(kTickMs);
        manager->ConfigureGrid(10000.0f, -2000000.0f, 2000000.0f);
        manager->SetInterestStreamOutMargin(5000.0f);
        manager->SetInterestLookaheadSeconds(1.0f);
        manager->SetInterestRebuildInterval(100);
        SetWorkers(manager, workers);
        const uint32_t human = EntityRegistry::Get().Register<NetworkEntity>("Bench::Human");
        const uint32_t npc   = EntityRegistry::Get().Register<NetworkEntity>("Bench::Npc");
        manager->SetInterestBudget(npc, 64);

        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> spread(-kHubRadius, kHubRadius);
        std::uniform_real_distribution<float> step(-150.0f, 150.0f);
        std::vector<NetworkEntity *> movers;
        for (int i = 0; i < kNpcs; ++i) {
            NetworkEntity *entity   = TestWorld::Make(manager, npc);
            entity->position        = glm::vec3(spread(rng), 0.0f, spread(rng));
            entity->streaming.range = 50000.0f;
            movers.push_back(entity);
        }
        for (int i = 0; i < players; ++i) {
            NetworkEntity *avatar   = TestWorld::Make(manager, human);
            avatar->position        = glm::vec3(spread(rng), 0.0f, spread(rng));
            avatar->streaming.range = 50000.0f;
            const MafiaNet::RakNetGUID guid(10000 + i);
            avatar->ownerGUID = MafiaNet::ToPeerGuid(guid);
            manager->SetViewer(avatar->ownerGUID, avatar->GetNetworkID());
            auto *connection        = manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, guid);
            connection->isValidated = true;
            manager->PushConnection(connection);
            movers.push_back(avatar);
        }

        std::vector<double> ticks, interest, pass;
        for (int tick = 0; tick < kWarmupTicks + kMeasuredTicks; ++tick) {
            for (NetworkEntity *entity : movers) {
                entity->position.x = std::clamp(entity->position.x + step(rng), -kHubRadius, kHubRadius);
                entity->position.z = std::clamp(entity->position.z + step(rng), -kHubRadius, kHubRadius);
            }
            g_nowUS += kTickMs * 1000;
            const auto start = Clock::now();
            manager->RebuildInterest();
            const auto middle = Clock::now();
            manager->Update();
            const auto end = Clock::now();
            if (tick >= kWarmupTicks) {
                ticks.push_back(std::chrono::duration<double, std::milli>(end - start).count());
                interest.push_back(std::chrono::duration<double, std::milli>(middle - start).count());
                pass.push_back(std::chrono::duration<double, std::milli>(end - middle).count());
            }
        }

        for (NetworkEntity *entity : movers) {
            TestWorld::Destroy(manager, entity);
        }
        manager->Clear();

        const auto mean = [](const std::vector<double> &v) {
            double sum = 0;
            for (double x : v) {
                sum += x;
            }
            return sum / v.size();
        };
        return {mean(ticks), Percentile(ticks, 0.5), Percentile(ticks, 0.99), Percentile(ticks, 1.0), mean(interest), mean(pass)};
    }
} // namespace

int main(int argc, char **argv) {
    Framework::Logging::GetInstance()->PauseLogging(true);
    MafiaNet::SetTimeSourceOverride(&Now);
    std::vector<int> counts;
    for (int i = 1; i < argc; ++i) {
        counts.push_back(std::atoi(argv[i]));
    }
    if (counts.empty()) {
        counts = {50, 100, 164, 250};
    }
    std::printf("%d NPCs in a %.0f m hub, %d measured ticks\n", kNpcs, kHubRadius * 2 / 100, kMeasuredTicks);
    std::printf("%8s %8s | %9s %9s %9s %9s | %11s %9s\n", "players", "workers", "mean ms", "p50 ms", "p99 ms", "max ms", "interest ms", "pass ms");
    // FW_BENCH_WORKERS=0,3,7 measures those worker counts instead of none and the default.
    std::vector<int> workerCounts;
    if (const char *list = std::getenv("FW_BENCH_WORKERS")) {
        for (const char *at = list; *at != '\0';) {
            char *end        = nullptr;
            const long count = std::strtol(at, &end, 10);
            // Nothing parsed, or something other than a comma after it: measuring a count nobody asked for
            // would pass for the one they did.
            if (end == at || (*end != '\0' && *end != ',')) {
                std::fprintf(stderr, "FW_BENCH_WORKERS: '%s' is not a list of worker counts such as 0,3,-1\n", list);
                return 1;
            }
            workerCounts.push_back(static_cast<int>(count));
            at = (*end == ',') ? end + 1 : end;
        }
    }
    if (workerCounts.empty()) {
        workerCounts = {0, -1};
    }
    for (const int players : counts) {
        for (const int workers : workerCounts) {
            const Result r = Run(players, workers);
            std::printf("%8d %8s | %9.2f %9.2f %9.2f %9.2f | %11.2f %9.2f\n", players, workers < 0 ? "default" : std::to_string(workers).c_str(), r.meanMs, r.p50Ms, r.p99Ms, r.maxMs, r.interestMs, r.passMs);
        }
    }
    return 0;
}
