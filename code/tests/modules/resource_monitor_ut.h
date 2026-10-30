/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// Uses TestManagerHelper from resource_manager_ut.h and WriteResource from resource_isolation_ut.h, included before.
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/resource_monitor.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace ResourceMonitorTest {
    using Framework::Scripting::ResourceMonitor;
    using Framework::Scripting::RuntimeStats;
    using namespace std::chrono_literals;

    inline const RuntimeStats *Find(const std::vector<RuntimeStats> &stats, const std::string &name) {
        for (const auto &runtime : stats) {
            if (runtime.name == name) {
                return &runtime;
            }
        }
        return nullptr;
    }
} // namespace ResourceMonitorTest

MODULE(resource_monitor, {
    using namespace ResourceMonitorTest;
    // Keys stand in for isolates: the monitor only compares them.
    const int a       = 0;
    const int b       = 0;
    const int unknown = 0;

    IT("charges a runtime only its own time when it calls another", {
        ResourceMonitor monitor;
        monitor.Track(&a, "a");
        monitor.Track(&b, "b");
        const auto t0 = ResourceMonitor::Clock::now();
        monitor.Enter(&a, t0);
        monitor.Enter(&b, t0 + 10ms);
        monitor.Leave(t0 + 25ms);
        monitor.Leave(t0 + 30ms);
        monitor.EndTick(t0 + 30ms);

        const auto stats = monitor.Snapshot();
        EQUALS(Find(stats, "a")->scriptMsPerTick, 15.0);
        EQUALS(Find(stats, "b")->scriptMsPerTick, 15.0);
    });

    IT("pauses the caller while an untracked runtime runs", {
        ResourceMonitor monitor;
        monitor.Track(&a, "a");
        const auto t0 = ResourceMonitor::Clock::now();
        monitor.Enter(&a, t0);
        monitor.Enter(&unknown, t0 + 10ms);
        monitor.Leave(t0 + 40ms);
        monitor.Leave(t0 + 50ms);
        monitor.EndTick(t0 + 50ms);

        EQUALS(Find(monitor.Snapshot(), "a")->scriptMsPerTick, 20.0);
    });

    IT("averages over ticks and keeps the worst one", {
        ResourceMonitor monitor;
        monitor.Track(&a, "a");
        const auto t0 = ResourceMonitor::Clock::now();
        monitor.Enter(&a, t0);
        monitor.Leave(t0 + 10ms);
        monitor.EndTick(t0 + 16ms);
        monitor.Enter(&a, t0 + 16ms);
        monitor.Leave(t0 + 46ms);
        monitor.EndTick(t0 + 50ms);

        const auto snapshot = monitor.Snapshot();
        const auto *stats   = Find(snapshot, "a");
        EQUALS(stats->scriptMsPerTick, 20.0);
        EQUALS(stats->scriptMsMaxTick, 30.0);
        EQUALS(stats->scriptSeconds, 0.04);
    });

    IT("keeps a runtime that is still executing in the tick its time ran in", {
        ResourceMonitor monitor;
        monitor.Track(&a, "a");
        const auto t0 = ResourceMonitor::Clock::now();
        monitor.Enter(&a, t0);
        monitor.EndTick(t0 + 10ms);
        monitor.Leave(t0 + 15ms);
        monitor.EndTick(t0 + 20ms);

        const auto snapshot = monitor.Snapshot();
        const auto *stats   = Find(snapshot, "a");
        EQUALS(stats->scriptMsPerTick, 7.5);
        EQUALS(stats->scriptMsMaxTick, 10.0);
    });

    IT("stops charging a runtime untracked while it runs", {
        ResourceMonitor monitor;
        monitor.Track(&a, "a");
        monitor.Track(&b, "b");
        const auto t0 = ResourceMonitor::Clock::now();
        monitor.Enter(&a, t0);
        monitor.Enter(&b, t0 + 10ms);
        monitor.Untrack(&b);
        monitor.Leave(t0 + 20ms);
        monitor.Leave(t0 + 30ms);
        monitor.EndTick(t0 + 30ms);

        const auto stats = monitor.Snapshot();
        EQUALS(stats.size(), static_cast<size_t>(1));
        EQUALS(Find(stats, "a")->scriptMsPerTick, 20.0);
    });

    IT("takes collector time per second from the recorded totals", {
        ResourceMonitor monitor;
        monitor.Track(&a, "a");
        const auto t0 = ResourceMonitor::Clock::now();

        // The first closed second only sets the baseline.
        EQUALS(monitor.SampleDue(t0 + 1s), true);
        monitor.Record(&a, {.heapUsedBytes = 1000, .gcPauses = 3, .gcPauseMs = 4.0});
        monitor.EndTick(t0 + 1s);
        EQUALS(monitor.SampleDue(t0 + 1500ms), false);
        monitor.Record(&a, {.heapUsedBytes = 2000, .heapLimitBytes = 4000, .gcPauses = 5, .gcPauseMs = 16.0});
        monitor.EndTick(t0 + 2s);

        // Only the second that had a figure counts.
        const auto snapshot = monitor.Snapshot();
        const auto *stats   = Find(snapshot, "a");
        EQUALS(stats->gcMsPerSecond, 12.0);
        EQUALS(stats->gcPauses, static_cast<uint64_t>(5));
        EQUALS(stats->heapUsedBytes, static_cast<uint64_t>(2000));
        EQUALS(stats->heapLimitBytes, static_cast<uint64_t>(4000));
    });

    IT("measures a runtime's own time in a call from what it was charged", {
        ResourceMonitor monitor;
        monitor.Track(&a, "a");
        monitor.Track(&b, "b");
        monitor.SetSlowThresholdMs(15.0);
        const auto t0 = ResourceMonitor::Clock::now();
        monitor.Enter(&a, t0);
        const double before = monitor.ChargedMs(&a, t0 + 5ms);
        EQUALS(before, 5.0);
        monitor.Enter(&b, t0 + 10ms);
        monitor.Leave(t0 + 50ms);

        // 55 ms of wall time, 40 of them in b.
        EQUALS(monitor.ChargedMs(&a, t0 + 60ms) - before, 15.0);
        EQUALS(monitor.ReportOwnTime(&a, "handler for", "x", before, t0 + 60ms), ResourceMonitor::SlowOutcome::Logged);
        EQUALS(monitor.ReportOwnTime(&a, "handler for", "y", monitor.ChargedMs(&a, t0 + 60ms), t0 + 70ms), ResourceMonitor::SlowOutcome::Under);
        EQUALS(monitor.ChargedMs(&unknown, t0 + 70ms), 0.0);
    });

    IT("logs slow work once per period and suppresses the repeats in between", {
        using Outcome = ResourceMonitor::SlowOutcome;
        ResourceMonitor monitor;
        monitor.Track(&a, "a");
        monitor.SetSlowThresholdMs(50.0);
        const auto t0 = ResourceMonitor::Clock::now();

        EQUALS(monitor.ReportIfSlow(&a, "handler for", "playerConnect", 49.0, t0), Outcome::Under);
        EQUALS(monitor.ReportIfSlow(&a, "handler for", "playerConnect", 60.0, t0), Outcome::Logged);
        EQUALS(monitor.ReportIfSlow(&a, "handler for", "playerConnect", 70.0, t0 + 1s), Outcome::Suppressed);
        EQUALS(monitor.ReportIfSlow(&a, "handler for", "playerDisconnect", 70.0, t0 + 1s), Outcome::Logged);
        EQUALS(monitor.ReportIfSlow(&a, "handler for", "playerConnect", 80.0, t0 + 11s), Outcome::Logged);
        EQUALS(monitor.ReportIfSlow(&unknown, "handler for", "playerConnect", 70.0, t0), Outcome::Under);
        EQUALS(Find(monitor.Snapshot(), "a")->slowReports, static_cast<uint64_t>(4));

        monitor.SetSlowThresholdMs(0.0);
        EQUALS(monitor.ReportIfSlow(&a, "handler for", "playerConnect", 1000.0, t0 + 30s), Outcome::Under);
    });

    IT("charges each resource for its own script when one calls another", {
        using namespace Framework::Scripting;
        TestManagerHelper::Cleanup();
        // Busy-waits, so each burn is exactly as long as asked whatever the build.
        ResourceIsolationTest::WriteResource("monitor-bank", R"(
            const burn = (ms) => { const end = Date.now() + ms; while (Date.now() < end) {} };
            Exports.register('api', { work(ms) { burn(ms); return ms; } });
            Events.on('audit', () => burn(200));
        )",
            "\"api\"");
        ResourceIsolationTest::WriteResource("monitor-teller", R"(
            const burn = (ms) => { const end = Date.now() + ms; while (Date.now() < end) {} };
            const api = Exports.get('monitor-bank', 'api');
            setTimeout(() => { burn(20); api.work(30); Events.emit('audit'); __record('done', 1); }, 500);
        )");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        // Every burn at least 70 ms from the threshold, so a scheduler pause on a busy runner cannot cross it.
        engine.GetMonitor().SetSlowThresholdMs(100.0);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 2u);
        EQUALS((bool)manager.StartResource("monitor-bank"), true);
        EQUALS((bool)manager.StartResource("monitor-teller"), true);
        // Frames as the server's scripting module runs them.
        const auto frame = [&engine] {
            engine.CloseMonitorTick();
            engine.Tick();
        };
        frame();
        const auto before         = engine.GetRuntimeStats();
        const auto *tellerAtStart = Find(before, "monitor-teller");
        const auto *bankAtStart   = Find(before, "monitor-bank");
        EQUALS((tellerAtStart != nullptr && bankAtStart != nullptr), true);
        EQUALS(TestManagerHelper::RecordedValue("done"), -1);

        // Past a second, so the heaps have been sampled too.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
        while (std::chrono::steady_clock::now() < deadline) {
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        EQUALS(TestManagerHelper::RecordedValue("done"), 1);

        const auto after        = engine.GetRuntimeStats();
        const auto *tellerStats = Find(after, "monitor-teller");
        const auto *bankStats   = Find(after, "monitor-bank");
        EQUALS((tellerStats != nullptr && bankStats != nullptr), true);
        const double tellerMs = (tellerStats->scriptSeconds - tellerAtStart->scriptSeconds) * 1000.0;
        const double bankMs   = (bankStats->scriptSeconds - bankAtStart->scriptSeconds) * 1000.0;
        GREATEREQ(tellerMs, 20.0);
        LESSER(tellerMs, 70.0);
        GREATEREQ(bankMs, 230.0);
        LESSER(bankMs, 300.0);
        // Only the bank's 200 ms handler is over the threshold: its 30 ms export call is not, and the teller is not
        // blamed for the bank's time inside its own timer.
        EQUALS(bankStats->slowReports, static_cast<uint64_t>(1));
        EQUALS(tellerStats->slowReports, static_cast<uint64_t>(0));
        GREATER(bankStats->heapUsedBytes, static_cast<uint64_t>(0));
        GREATER(bankStats->heapLimitBytes, bankStats->heapUsedBytes);

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });
})
