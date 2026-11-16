/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// What copying a value between two resources costs, split into the copy out of the caller's isolate
// and the rebuild in the other: what every export call, event argument and return value pays twice.
//
// The payloads are what gamemodes pass around: a roster of player records, the same records' ids, and
// one large record. Build it in RelWithDebInfo or Release; a Debug number says little.

#include "logging/logger.h"
#include "scripting/builtins/builtins.h"
#include "scripting/node_engine.h"
#include "scripting/node_runtime.h"
#include "scripting/value_transfer.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

namespace {
    using Clock = std::chrono::steady_clock;
    using namespace Framework::Scripting;

    struct Payload {
        const char *name;
        const char *expression;
        int iterations;
    };

    const Payload kPayloads[] = {
        {"roster of 200 players",
            "Array.from({ length: 200 }, (_, i) => ({ id: i, name: 'player' + i, position: { x: i * 1.5, y: 0, z: -i }, health: 100, faction: i % 4, "
            "inventory: [{ item: 'wand', count: 1 }, { item: 'potion', count: i % 5 }], online: true, joinedAt: 1700000000000 + i }))",
            300},
        {"200 ids", "Array.from({ length: 200 }, (_, i) => i)", 5000},
        {"one record", "({ id: 7, name: 'player7', position: { x: 1, y: 2, z: 3 }, health: 100, faction: 2, inventory: [{ item: 'wand', count: 1 }], online: true })", 50000},
    };

    struct Runtimes {
        NodeEngine engine;
        std::unique_ptr<NodeRuntime> source;
        std::unique_ptr<NodeRuntime> target;

        Runtimes() {
            engine.Init();
            Builtins::RegisterTransferTypes();
            std::string error;
            source = engine.CreateRuntime(error);
            target = engine.CreateRuntime(error);
        }
        ~Runtimes() {
            source.reset();
            target.reset();
            engine.Shutdown();
        }
    };

    void Measure(Runtimes &runtimes, const Payload &payload) {
        v8::Isolate *from = runtimes.source->GetIsolate();
        v8::Isolate *to   = runtimes.target->GetIsolate();
        double copyMs = 0, rebuildMs = 0;

        v8::Locker fromLocker(from);
        v8::Isolate::Scope fromScope(from);
        v8::HandleScope fromHandles(from);
        v8::Local<v8::Context> fromContext = runtimes.source->GetContext();
        v8::Context::Scope fromContextScope(fromContext);
        const std::string source       = std::string("(") + payload.expression + ")";
        v8::Local<v8::Value> value     = v8::Script::Compile(fromContext, v8::String::NewFromUtf8(from, source.c_str()).ToLocalChecked()).ToLocalChecked()->Run(fromContext).ToLocalChecked();

        for (int i = 0; i < payload.iterations; ++i) {
            const auto start = Clock::now();
            auto copied      = ValueTransfer::Copy(from, fromContext, value);
            const auto mid   = Clock::now();
            {
                v8::Locker toLocker(to);
                v8::Isolate::Scope toScope(to);
                v8::HandleScope toHandles(to);
                v8::Local<v8::Context> toContext = runtimes.target->GetContext();
                v8::Context::Scope toContextScope(toContext);
                (void)ValueTransfer::Rebuild(to, toContext, copied.GetValue());
            }
            const auto end = Clock::now();
            copyMs += std::chrono::duration<double, std::milli>(mid - start).count();
            rebuildMs += std::chrono::duration<double, std::milli>(end - mid).count();
        }
        std::printf("%-24s %10.1f %12.1f %10.1f\n", payload.name, copyMs * 1000 / payload.iterations, rebuildMs * 1000 / payload.iterations, (copyMs + rebuildMs) * 1000 / payload.iterations);
    }
} // namespace

int main() {
    Framework::Logging::GetInstance()->PauseLogging(true);
    Runtimes runtimes;
    std::printf("%-24s %10s %12s %10s\n", "payload", "copy us", "rebuild us", "total us");
    for (const Payload &payload : kPayloads) {
        Measure(runtimes, payload);
    }
    return 0;
}
