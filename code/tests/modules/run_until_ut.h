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
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <intrin.h>
#include <windows.h>
#else
#include <time.h>
#endif

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

    // CPU time this thread has used, so a wait is measured by what it costs rather than by how long it lasts. Windows
    // charges thread time in clock ticks of 15.6 ms; its cycle count is exact, and is converted at the measured rate.
    inline std::chrono::microseconds ThreadCpuTime() {
#ifdef _WIN32
        static const double cyclesPerUs = [] {
            LARGE_INTEGER frequency, start, end;
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&start);
            const uint64_t tscStart = __rdtsc();
            do {
                QueryPerformanceCounter(&end);
            } while ((end.QuadPart - start.QuadPart) * 1000 < frequency.QuadPart * 20);
            const double us = static_cast<double>(end.QuadPart - start.QuadPart) * 1e6 / static_cast<double>(frequency.QuadPart);
            return static_cast<double>(__rdtsc() - tscStart) / us;
        }();
        ULONG64 cycles = 0;
        QueryThreadCycleTime(GetCurrentThread(), &cycles);
        return std::chrono::microseconds(static_cast<int64_t>(static_cast<double>(cycles) / cyclesPerUs));
#else
        timespec now {};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
        return std::chrono::seconds(now.tv_sec) + std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::nanoseconds(now.tv_nsec));
#endif
    }

    struct Rig {
        Framework::Scripting::NodeEngine engine;
        std::unique_ptr<Framework::Scripting::ResourceManager> manager;

        bool Start(const std::vector<std::string> &resources) {
            if (engine.Init() != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
                return false;
            }
            Framework::Scripting::ResourceManagerConfig config;
            config.resourcesPath = TestManagerHelper::GetTestResourcePath();
            manager              = std::make_unique<Framework::Scripting::ResourceManager>(&engine, config);
            TestManagerHelper::RegisterEvents(engine, *manager);
            manager->DiscoverResources();
            for (const std::string &name : resources) {
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

    // CPU a second of serving costs this thread: 60 ticks, each followed by the wait to the next, as the server runs.
    inline std::chrono::microseconds CpuOfServing(Rig &rig) {
        const auto before = ThreadCpuTime();
        auto next         = Clock::now();
        for (int tick = 0; tick < 60; ++tick) {
            next += std::chrono::microseconds(16667);
            rig.engine.Tick();
            rig.engine.RunUntil(next);
        }
        return ThreadCpuTime() - before;
    }
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

    IT("finishes socket round trips before the next tick while a timer is pending", {
        TestManagerHelper::Cleanup();
        // A timer makes the loop's wait finite, so only its socket tells the wait to keep visiting it.
        ResourceIsolationTest::WriteResource("sockets", std::string(RunUntilTest::kRoundTrips) + "setInterval(() => {}, 60000);");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"sockets"}), true);
        EQUALS(ResourceIsolationTest::TickUntilRecorded(rig.engine, "ready"), true);

        TestManagerHelper::EvalIntIn(rig.engine, "sockets", "trips(20); 0");
        rig.TickAndWait(200);
        EQUALS(TestManagerHelper::RecordedValue("trips"), 20);
    });

    IT("finishes round trips on unreferenced sockets before the next tick", {
        TestManagerHelper::Cleanup();
        // Unreferenced, the sockets do not keep the loop alive, yet their data still has to wake it; the timer is what
        // keeps a resource like this one running.
        ResourceIsolationTest::WriteResource("unref", R"(
            const net = require('node:net');
            const server = net.createServer((s) => { s.unref(); s.on('data', (d) => s.write(d)); });
            server.listen(0, '127.0.0.1', () => {
                const client = net.connect(server.address().port, '127.0.0.1', () => {
                    client.unref();
                    server.unref();
                    globalThis.trips = async (n) => {
                        for (let i = 1; i <= n; i++) {
                            await new Promise((resolve) => { client.once('data', resolve); client.write('x'); });
                            __record('trips', i);
                        }
                    };
                    __record('ready', 1);
                });
            });
            setInterval(() => {}, 60000);
        )");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"unref"}), true);
        EQUALS(ResourceIsolationTest::TickUntilRecorded(rig.engine, "ready"), true);

        TestManagerHelper::EvalIntIn(rig.engine, "unref", "trips(20); 0");
        rig.TickAndWait(200);
        EQUALS(TestManagerHelper::RecordedValue("trips"), 20);
    });

    IT("finishes file system calls before the next tick while a timer is pending", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("files", R"(
            const fs = require('node:fs/promises');
            globalThis.stats = async (n) => {
                for (let i = 1; i <= n; i++) {
                    await fs.stat('.');
                    __record('stats', i);
                }
            };
            setInterval(() => {}, 60000);
        )");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"files"}), true);

        TestManagerHelper::EvalIntIn(rig.engine, "files", "stats(20); 0");
        rig.TickAndWait(200);
        EQUALS(TestManagerHelper::RecordedValue("stats"), 20);
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

    IT("finishes another resource's exported socket calls before the next tick", {
        TestManagerHelper::Cleanup();
        // The owner holds the connection, as a database resource holds its pool.
        ResourceIsolationTest::WriteResource("owner", std::string(RunUntilTest::kRoundTrips) + "Exports.register('api', { query: (n) => trips(n) });", "\"api\"");
        ResourceIsolationTest::WriteResource("caller", R"(
            const api = Exports.get('owner', 'api');
            globalThis.calls = async (n) => {
                for (let i = 1; i <= n; i++) {
                    await api.query(1);
                    __record('calls', i);
                }
            };
        )");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"owner", "caller"}), true);
        EQUALS(ResourceIsolationTest::TickUntilRecorded(rig.engine, "ready"), true);

        TestManagerHelper::EvalIntIn(rig.engine, "caller", "calls(20); 0");
        rig.TickAndWait(200);
        EQUALS(TestManagerHelper::RecordedValue("calls"), 20);
    });

    IT("costs nothing per idle resource, between ticks or in them", {
        // Reproduces the idle server: resources whose only work is a timer that is not due, or nothing at all.
        std::chrono::microseconds bare;
        {
            TestManagerHelper::Cleanup();
            RunUntilTest::Rig rig;
            EQUALS(rig.Start({}), true);
            RunUntilTest::CpuOfServing(rig);
            bare = RunUntilTest::CpuOfServing(rig);
        }
        std::chrono::microseconds idle;
        {
            TestManagerHelper::Cleanup();
            std::vector<std::string> names;
            for (int i = 0; i < 30; ++i) {
                names.push_back("timer-" + std::to_string(i));
                ResourceIsolationTest::WriteResource(names.back(), "setInterval(() => {}, 60000);");
                names.push_back("empty-" + std::to_string(i));
                ResourceIsolationTest::WriteResource(names.back(), "// nothing");
            }
            RunUntilTest::Rig rig;
            EQUALS(rig.Start(names), true);
            RunUntilTest::CpuOfServing(rig);
            idle = RunUntilTest::CpuOfServing(rig);
        }
        // Visiting every live loop each millisecond and ticking every runtime cost 60 such resources about 90 ms a
        // second; a resource with nothing due costs nothing now, whatever it holds.
        EQUALS(idle - bare < std::chrono::milliseconds(10), true);
    });

    IT("wakes for a socket's I/O as soon as it completes", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("sockets", RunUntilTest::kRoundTrips);
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"sockets"}), true);
        EQUALS(ResourceIsolationTest::TickUntilRecorded(rig.engine, "ready"), true);

        TestManagerHelper::EvalIntIn(rig.engine, "sockets", "trips(100000); 0");
        rig.engine.Tick();
        const int before = TestManagerHelper::RecordedValue("trips");
        rig.engine.RunUntil(Clock::now() + std::chrono::milliseconds(100));
        // Polled every millisecond, a round trip took about 3 ms on Windows: 32 in this window.
        EQUALS(TestManagerHelper::RecordedValue("trips") - before >= 100, true);
    });

    IT("runs a platform task an idle resource posted without waiting for a tick", {
        TestManagerHelper::Cleanup();
        // Atomics.waitAsync times out through a delayed task V8 posts to the isolate's platform runner.
        ResourceIsolationTest::WriteResource("waiter", R"(
            globalThis.arm = () => {
                const cell = new Int32Array(new SharedArrayBuffer(4));
                Atomics.waitAsync(cell, 0, 0, 30).value.then((outcome) => __record('woke', outcome === 'timed-out' ? 1 : 2));
            };
        )");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"waiter"}), true);
        rig.engine.Tick();

        TestManagerHelper::EvalIntIn(rig.engine, "waiter", "arm(); 0");
        rig.engine.RunUntil(Clock::now() + std::chrono::milliseconds(300));
        // The platform turns a delayed task into an unreferenced timer on the resource's loop, which never ran while
        // nothing else kept the loop alive.
        EQUALS(TestManagerHelper::RecordedValue("woke"), 1);
    });

    IT("runs at the next tick what a raw call into an idle resource queued", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("raw", R"(
            globalThis.order = [];
            globalThis.queue = () => {
                process.nextTick(() => order.push('tick'));
                new Promise((resolve) => process.nextTick(resolve)).then(() => order.push('then'));
            };
        )");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"raw"}), true);
        for (int i = 0; i < 5; ++i) {
            rig.TickAndWait(5);
        }

        // Past the framework's entries: no callback scope closes behind it, so only the tick can drain the queues.
        NodeRuntime *runtime = rig.engine.GetResourceRuntime("raw");
        NEQUALS(runtime, nullptr);
        {
            v8::Isolate *isolate = runtime->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = runtime->GetContext();
            v8::Context::Scope contextScope(context);
            v8::Local<v8::Value> queue = context->Global()->Get(context, v8::String::NewFromUtf8Literal(isolate, "queue")).ToLocalChecked();
            EQUALS(queue.As<v8::Function>()->Call(context, v8::Undefined(isolate), 0, nullptr).IsEmpty(), false);
        }
        EQUALS(TestManagerHelper::EvalIntIn(rig.engine, "raw", "order.length"), 0);
        rig.engine.Tick();
        EQUALS(TestManagerHelper::EvalIntIn(rig.engine, "raw", "order.length"), 2);
    });

    IT("runs a loop that only waits on timers each time one is due", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("interval", "let fired = 0; setInterval(() => __record('fired', ++fired), 10);");
        RunUntilTest::Rig rig;
        EQUALS(rig.Start({"interval"}), true);
        rig.engine.Tick();

        const int before = TestManagerHelper::RecordedValue("fired");
        rig.engine.RunUntil(Clock::now() + std::chrono::milliseconds(500));
        const int fired = TestManagerHelper::RecordedValue("fired") - before;
        // 50 are due, and no tick runs in the window: a wait that slept past them would run none at all. The bound
        // leaves room for a loaded machine's timer slack.
        EQUALS(fired >= 20, true);
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
