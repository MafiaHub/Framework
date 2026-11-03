/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// Uses TestManagerHelper from resource_manager_ut.h and ResourceIsolationTest from resource_isolation_ut.h, included
// before this file.
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"

#include <chrono>
#include <memory>
#include <string>

namespace RunUntilTest {
    using Clock = std::chrono::steady_clock;

    // Script that echoes over a local TCP connection to itself and counts the round trips of a chain started on demand.
    constexpr const char *kRoundTrips = R"(
        const net = require('node:net');
        const server = net.createServer((s) => s.on('data', (d) => s.write(d)));
        server.listen(0, '127.0.0.1', () => {
            const client = net.connect(server.address().port, '127.0.0.1', () => {
                globalThis.trips = async (n) => {
                    for (let i = 1; i <= n; i++) {
                        await new Promise((resolve) => { client.once('data', resolve); client.write('x'); });
                        __record('trips', i);
                    }
                };
                __record('ready', 1);
            });
        });
    )";

    struct Rig {
        Framework::Scripting::NodeEngine engine;
        std::unique_ptr<Framework::Scripting::ResourceManager> manager;

        bool Start(std::initializer_list<const char *> resources) {
            if (engine.Init() != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
                return false;
            }
            Framework::Scripting::ResourceManagerConfig config;
            config.resourcesPath = TestManagerHelper::GetTestResourcePath();
            manager              = std::make_unique<Framework::Scripting::ResourceManager>(&engine, config);
            TestManagerHelper::RegisterEvents(engine, *manager);
            manager->DiscoverResources();
            for (const char *name : resources) {
                if (!manager->StartResource(name)) {
                    return false;
                }
            }
            return true;
        }

        // One tick, then the wait the server does until the next, and only that.
        void TickAndWait(int waitMs) {
            engine.Tick();
            engine.RunUntil(Clock::now() + std::chrono::milliseconds(waitMs));
        }

        ~Rig() {
            if (manager) {
                manager->StopAll();
            }
            engine.Shutdown();
            TestManagerHelper::Cleanup();
        }
    };
} // namespace RunUntilTest

MODULE(run_until, {
    using namespace Framework::Scripting;
    using RunUntilTest::Clock;

    IT("finishes a chain of socket round trips before the next tick", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("sockets", RunUntilTest::kRoundTrips);
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"sockets"}), true);
        EQUALS(ResourceIsolationTest::TickUntilRecorded(rig.engine, "ready"), true);

        TestManagerHelper::EvalIntIn(rig.engine, "sockets", "trips(20); 0");
        rig.TickAndWait(200);
        EQUALS(TestManagerHelper::RecordedValue("trips"), 20);
    });

    IT("runs immediates and timers as they come due between ticks", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("timing", R"(
            globalThis.immediates = async (n) => {
                for (let i = 1; i <= n; i++) {
                    await new Promise((resolve) => setImmediate(resolve));
                    __record('immediates', i);
                }
            };
            globalThis.timer = () => {
                const start = Date.now();
                setTimeout(() => __record('timerMs', Date.now() - start), 5);
            };
        )");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"timing"}), true);

        TestManagerHelper::EvalIntIn(rig.engine, "timing", "immediates(100); timer(); 0");
        rig.TickAndWait(200);
        EQUALS(TestManagerHelper::RecordedValue("immediates"), 100);
        EQUALS(TestManagerHelper::RecordedValue("timerMs") >= 4, true);
        EQUALS(TestManagerHelper::RecordedValue("timerMs") < 150, true);
    });

    IT("settles calls to another resource's async export before the next tick", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("owner", "Exports.register('api', { async ping(x) { return x; } });", "\"api\"");
        ResourceIsolationTest::WriteResource("caller", R"(
            const api = Exports.get('owner', 'api');
            globalThis.calls = async (n) => {
                for (let i = 1; i <= n; i++) {
                    __record('calls', await api.ping(i));
                }
                process.nextTick(() => __record('nextTick', 1));
            };
        )");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"owner", "caller"}), true);

        TestManagerHelper::EvalIntIn(rig.engine, "caller", "calls(50); 0");
        rig.TickAndWait(200);
        EQUALS(TestManagerHelper::RecordedValue("calls"), 50);
        // Queued by code a settlement resumed: it runs without waiting for an unrelated callback in that runtime.
        EQUALS(TestManagerHelper::RecordedValue("nextTick"), 1);
    });

    IT("returns at the deadline while a resource keeps itself busy", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("spinner", "let spins = 0; const spin = () => { __record('spins', ++spins); setImmediate(spin); }; spin();");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"spinner"}), true);
        rig.engine.Tick();

        const int before    = TestManagerHelper::RecordedValue("spins");
        const auto deadline = Clock::now() + std::chrono::milliseconds(20);
        rig.engine.RunUntil(deadline);
        const auto returned = Clock::now();
        EQUALS(returned >= deadline, true);
        EQUALS(returned - deadline < std::chrono::milliseconds(20), true);
        EQUALS(TestManagerHelper::RecordedValue("spins") - before > 10, true);
    });

    IT("keeps other resources running while two call each other without end", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("owner", "Exports.register('api', { async ping(x) { return x; } });", "\"api\"");
        ResourceIsolationTest::WriteResource("caller", R"(
            const api = Exports.get('owner', 'api');
            globalThis.forever = async () => { for (let i = 0; ; i++) __record('calls', await api.ping(i)); };
        )");
        ResourceIsolationTest::WriteResource("bystander", "globalThis.arm = () => setTimeout(() => __record('fired', 1), 5);");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"owner", "caller", "bystander"}), true);

        TestManagerHelper::EvalIntIn(rig.engine, "bystander", "arm(); 0");
        TestManagerHelper::EvalIntIn(rig.engine, "caller", "forever(); 0");
        rig.TickAndWait(200);
        EQUALS(TestManagerHelper::RecordedValue("fired"), 1);
        EQUALS(TestManagerHelper::RecordedValue("calls") > 100, true);
    });

    IT("returns early with an uncaught error waiting", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("thrower", "globalThis.arm = () => setTimeout(() => { throw new Error('boom'); }, 5);");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"thrower"}), true);

        TestManagerHelper::EvalIntIn(rig.engine, "thrower", "arm(); 0");
        rig.engine.Tick();
        const auto deadline = Clock::now() + std::chrono::milliseconds(500);
        rig.engine.RunUntil(deadline);
        EQUALS(Clock::now() < deadline - std::chrono::milliseconds(100), true);
        const auto errors = rig.engine.DrainPendingErrors();
        EQUALS(errors.size(), 1u);
        STREQUALS(errors[0].resourceName.c_str(), "thrower");
    });

    IT("stops a resource that exits while it runs between ticks", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("leaver", "globalThis.arm = () => setTimeout(() => process.exit(3), 20);");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"leaver"}), true);

        TestManagerHelper::EvalIntIn(rig.engine, "leaver", "arm(); 0");
        rig.TickAndWait(200);
        EQUALS(rig.manager->IsResourceRunning("leaver"), false);
        EQUALS(rig.engine.GetResourceRuntime("leaver") == nullptr, true);
    });
});
