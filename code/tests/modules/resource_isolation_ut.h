/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// Uses TestManagerHelper from resource_manager_ut.h, included before this file.
#include "scripting/builtins/messages.h"
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace ResourceIsolationTest {
    static void WriteResource(const std::string &name, const std::string &script, const std::string &exports = "") {
        TestManagerHelper::CreateTestResource(name, "{\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"mafiahub\":{\"server\":\"main.js\",\"exports\":[" + exports + "]}}");
        TestManagerHelper::CreateTestScript(name, "main.js", script);
    }

    // Ticks the engine the way the server does (message replies included) until `key` is recorded or time runs out.
    static bool TickUntilRecorded(Framework::Scripting::NodeEngine &engine, const std::string &key, int timeoutMs = 2000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            engine.Tick();
            {
                v8::Isolate *isolate = engine.GetIsolate();
                v8::Locker locker(isolate);
                v8::Isolate::Scope isolateScope(isolate);
                v8::HandleScope handleScope(isolate);
                v8::Local<v8::Context> context = engine.GetContext();
                v8::Context::Scope contextScope(context);
                Framework::Scripting::Builtins::Messages::ProcessPendingResponses(isolate, context);
            }
            if (TestManagerHelper::Recorded().contains(key)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    static std::uintmax_t FileSize(const std::filesystem::path &path) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        return ec ? 0 : size;
    }
} // namespace ResourceIsolationTest

MODULE(resource_isolation, {
    using namespace Framework::Scripting;

    IT("stops a resource's worker threads when the resource stops", {
        TestManagerHelper::Cleanup();
        const auto heartbeat = std::filesystem::temp_directory_path() / "fw_resource_worker_heartbeat.txt";
        std::filesystem::remove(heartbeat);
        const std::string path = heartbeat.generic_string();
        ResourceIsolationTest::WriteResource("worker-owner", "const { Worker } = require('node:worker_threads');"
                                                             "globalThis.worker = new Worker(\"const fs = require('node:fs'); setInterval(() => fs.appendFileSync('" + path + "', '.'), 5);\", { eval: true });");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 1u);
        EQUALS((bool)manager.StartResource("worker-owner"), true);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (ResourceIsolationTest::FileSize(heartbeat) < 5 && std::chrono::steady_clock::now() < deadline) {
            engine.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        EQUALS(ResourceIsolationTest::FileSize(heartbeat) >= 5, true);

        // No resourceStop handler terminates it: stopping the resource is enough.
        EQUALS((bool)manager.StopResource("worker-owner"), true);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const auto sizeAfterStop = ResourceIsolationTest::FileSize(heartbeat);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        EQUALS(ResourceIsolationTest::FileSize(heartbeat), sizeAfterStop);

        std::filesystem::remove(heartbeat);
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("stops a resource's timers and closes its servers when the resource stops", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("ticker", R"(
            let ticks = 0;
            setInterval(() => __record('ticks', ++ticks), 2);
            const server = require('node:http').createServer();
            server.on('error', (error) => __record('listenError', 1));
            server.listen(47311, '127.0.0.1', () => __record('listening', __recorded('listening') + 1));
        )");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        TestManagerHelper::Recorded()["listening"] = 0;
        EQUALS(manager.DiscoverResources(), 1u);
        EQUALS((bool)manager.StartResource("ticker"), true);
        EQUALS(ResourceIsolationTest::TickUntilRecorded(engine, "ticks"), true);
        const auto listenDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (TestManagerHelper::RecordedValue("listening") < 1 && std::chrono::steady_clock::now() < listenDeadline) {
            engine.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EQUALS(TestManagerHelper::RecordedValue("listening"), 1);

        EQUALS((bool)manager.StopResource("ticker"), true);
        const int32_t ticksAtStop = TestManagerHelper::RecordedValue("ticks");
        for (int i = 0; i < 20; ++i) {
            engine.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        EQUALS(TestManagerHelper::RecordedValue("ticks"), ticksAtStop);

        // The port came back with the runtime: a fresh start listens on it again.
        EQUALS((bool)manager.StartResource("ticker"), true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (TestManagerHelper::RecordedValue("listening") < 2 && std::chrono::steady_clock::now() < deadline) {
            engine.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EQUALS(TestManagerHelper::RecordedValue("listening"), 2);
        EQUALS(TestManagerHelper::RecordedValue("listenError"), -1);

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("tells listeners about a runtime before it is disposed", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("short-lived", "");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);

        v8::Isolate *disposed  = nullptr;
        bool aliveWhenNotified = false;
        engine.AddRuntimeDisposingListener([&](v8::Isolate *isolate) {
            disposed          = isolate;
            aliveWhenNotified = isolate->IsInUse();
        });
        EQUALS(manager.DiscoverResources(), 1u);
        EQUALS((bool)manager.StartResource("short-lived"), true);
        v8::Isolate *resourceIsolate = engine.GetResourceRuntime("short-lived")->GetIsolate();

        EQUALS((bool)manager.StopResource("short-lived"), true);
        EQUALS(disposed == resourceIsolate, true);
        EQUALS(aliveWhenNotified, true);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("gives every resource its own globals", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("globals-first", "globalThis.shared = 'first';");
        ResourceIsolationTest::WriteResource("globals-second", "__record('sawShared', typeof globalThis.shared === 'undefined' ? 0 : 1);");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 2u);
        EQUALS((bool)manager.StartResource("globals-first"), true);
        EQUALS((bool)manager.StartResource("globals-second"), true);
        EQUALS(TestManagerHelper::RecordedValue("sawShared"), 0);

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("delivers events across resources, with callbacks and async handlers", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("shop", R"(
            Events.on('price', (item, answer) => answer(item.cost * 2));
            Events.on('restock', async () => { await new Promise((resolve) => setTimeout(resolve, 10)); __record('restocked', 1); });
        )");
        ResourceIsolationTest::WriteResource("customer", R"(
            Events.on('resourceStart', (name) => {
                if (name !== 'customer') return;
                Events.emit('price', { cost: 21 }, (value) => __record('price', value));
                Events.emit('restock').then(() => __record('restockSettled', __recorded('restocked')));
            });
        )");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 2u);
        EQUALS((bool)manager.StartResource("shop"), true);
        EQUALS((bool)manager.StartResource("customer"), true);

        EQUALS(TestManagerHelper::RecordedValue("price"), 42);
        EQUALS(ResourceIsolationTest::TickUntilRecorded(engine, "restockSettled"), true);
        EQUALS(TestManagerHelper::RecordedValue("restockSettled"), 1);

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("hands out exports as copies whose functions call home", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("bank", R"(
            const state = { balance: 10 };
            Exports.register('api', { state, deposit(amount) { state.balance += amount; return state.balance; } });
            Events.on('audit', () => __record('balance', state.balance));
        )", "\"api\"");
        ResourceIsolationTest::WriteResource("teller", R"(
            const api = Exports.get('bank', 'api');
            __record('deposited', api.deposit(5));
            api.state.balance = 1000;
            Events.emit('audit');
        )");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 2u);
        EQUALS((bool)manager.StartResource("bank"), true);
        EQUALS((bool)manager.StartResource("teller"), true);

        EQUALS(TestManagerHelper::RecordedValue("deposited"), 15);
        // The teller changed its copy; the bank's own state only moved through deposit().
        EQUALS(TestManagerHelper::RecordedValue("balance"), 15);

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("answers a message request from another resource", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("echo", "Messages.handle('next', (payload, reply) => reply({ n: payload.n + 1 }));");
        ResourceIsolationTest::WriteResource("asker", "Messages.request('echo', 'next', { n: 1 }).then((answer) => __record('answer', answer.n), () => __record('answer', -100));");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 2u);
        EQUALS((bool)manager.StartResource("echo"), true);
        EQUALS((bool)manager.StartResource("asker"), true);

        EQUALS(ResourceIsolationTest::TickUntilRecorded(engine, "answer"), true);
        EQUALS(TestManagerHelper::RecordedValue("answer"), 2);

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("attributes an uncaught error to the resource it came from", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("faulty", "setTimeout(() => { throw new Error('boom'); }, 1);");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 1u);
        EQUALS((bool)manager.StartResource("faulty"), true);

        std::vector<NodeEngine::PendingUncaughtError> errors;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (errors.empty() && std::chrono::steady_clock::now() < deadline) {
            engine.Tick();
            errors = engine.DrainPendingErrors();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        EQUALS(errors.size(), 1u);
        STREQUALS(errors[0].resourceName.c_str(), "faulty");
        EQUALS(errors[0].errorMessage.find("boom") != std::string::npos, true);

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });
});
