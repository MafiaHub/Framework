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

#include "node_test_helpers.h"

#include <v8pp/class.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
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

    // A native type a resource wraps, counting the live instances.
    struct Probe {
        static inline int alive = 0;
        Probe() {
            ++alive;
        }
        ~Probe() {
            --alive;
        }
    };

    static std::map<v8::Isolate *, std::unique_ptr<v8pp::class_<Probe>>> &ProbeClasses() {
        static std::map<v8::Isolate *, std::unique_ptr<v8pp::class_<Probe>>> classes;
        return classes;
    }

    static void InstallProbe(v8::Isolate *isolate, v8::Local<v8::Context> context) {
        auto &cls = ProbeClasses()[isolate];
        cls       = std::make_unique<v8pp::class_<Probe>>(isolate);
        cls->ctor<>();
        context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "Probe"), cls->js_function_template()->GetFunction(context).ToLocalChecked()).Check();
    }

    // The manager __stop(name) stops resources through, as a project binding might.
    static Framework::Scripting::ResourceManager *&StopTarget() {
        static Framework::Scripting::ResourceManager *manager = nullptr;
        return manager;
    }

    static void InstallStop(v8::Isolate *isolate, v8::Local<v8::Context> context) {
        auto stop = [](const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::String::Utf8Value name(info.GetIsolate(), info[0]);
            StopTarget()->StopResource(*name ? *name : "");
        };
        context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "__stop"), v8::Function::New(context, stop).ToLocalChecked()).Check();
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

    IT("frees the native objects a resource wrapped when it stops", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("prober", "globalThis.kept = [new Probe(), new Probe(), new Probe()];");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager, ResourceIsolationTest::InstallProbe);
        engine.AddRuntimeDisposingListener([](v8::Isolate *isolate) { ResourceIsolationTest::ProbeClasses().erase(isolate); });
        ResourceIsolationTest::Probe::alive = 0;
        EQUALS(manager.DiscoverResources(), 1u);
        EQUALS((bool)manager.StartResource("prober"), true);
        EQUALS(ResourceIsolationTest::Probe::alive, 3);

        // Still referenced from the resource's globals, so no collection frees them: only the teardown can.
        EQUALS((bool)manager.StopResource("prober"), true);
        EQUALS(ResourceIsolationTest::Probe::alive, 0);

        engine.Shutdown();
        ResourceIsolationTest::ProbeClasses().clear();
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
    IT("stops only the resource that calls process.exit(), not the server", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("quitter", "setTimeout(() => { process.exit(3); __record('afterExit', 1); }, 1);");
        ResourceIsolationTest::WriteResource("bystander", "let beats = 0; setInterval(() => __record('beats', ++beats), 1);");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 2u);
        EQUALS((bool)manager.StartResource("quitter"), true);
        EQUALS((bool)manager.StartResource("bystander"), true);

        // With Node's default handler, process.exit() would end this test process here.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (manager.IsResourceRunning("quitter") && std::chrono::steady_clock::now() < deadline) {
            engine.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EQUALS(manager.IsResourceRunning("quitter"), false);
        EQUALS(engine.GetResourceRuntime("quitter") == nullptr, true);
        EQUALS(TestManagerHelper::RecordedValue("afterExit"), -1);

        const int32_t beats = TestManagerHelper::RecordedValue("beats");
        const auto beatDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (TestManagerHelper::RecordedValue("beats") == beats && std::chrono::steady_clock::now() < beatDeadline) {
            engine.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EQUALS(TestManagerHelper::RecordedValue("beats") > beats, true);

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("fails to start a resource that calls process.exit() while it loads, and leaves no runtime behind", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("early-quitter", "process.exit(0); __record('afterExit', 1);");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 1u);
        EQUALS((bool)manager.StartResource("early-quitter"), false);
        engine.Tick();
        EQUALS(manager.IsResourceRunning("early-quitter"), false);
        EQUALS(engine.GetResourceRuntime("early-quitter") == nullptr, true);
        EQUALS(TestManagerHelper::RecordedValue("afterExit"), -1);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("closes the files a resource opened when it stops", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("opener", "__record('fd', require('node:fs').openSync(__filename, 'r'));");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 1u);
        EQUALS((bool)manager.StartResource("opener"), true);
        const int32_t fd = TestManagerHelper::RecordedValue("fd");
        EQUALS(fd > 2, true);

        // Checked from another resource: the descriptor belongs to Node's C runtime, which on Windows is not this one.
        ResourceIsolationTest::WriteResource("fd-checker", "try { require('node:fs').fstatSync(" + std::to_string(fd) + "); __record('open', 1); } catch (e) { __record('open', 0); }");
        EQUALS(manager.DiscoverResources() >= 1u, true);
        EQUALS((bool)manager.StartResource("fd-checker"), true);
        EQUALS(TestManagerHelper::RecordedValue("open"), 1);
        EQUALS((bool)manager.StopResource("fd-checker"), true);

        EQUALS((bool)manager.StopResource("opener"), true);
        EQUALS((bool)manager.StartResource("fd-checker"), true);
        EQUALS(TestManagerHelper::RecordedValue("open"), 0);

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("does not copy back what a handler in another resource returns", {
        TestManagerHelper::Cleanup();
        // A Node timer links back to itself, so it cannot be copied; the emitter never needed it.
        ResourceIsolationTest::WriteResource("scheduler", "Events.on('schedule', () => setTimeout(() => __record('fired', 1), 1));");
        ResourceIsolationTest::WriteResource("planner", R"(
            Events.on('resourceStart', (name) => {
                if (name !== 'planner') return;
                Events.emit('schedule').then(() => __record('scheduled', 1), () => __record('scheduled', 0));
            });
        )");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 2u);
        EQUALS((bool)manager.StartResource("scheduler"), true);
        EQUALS((bool)manager.StartResource("planner"), true);

        EQUALS(ResourceIsolationTest::TickUntilRecorded(engine, "scheduled"), true);
        EQUALS(TestManagerHelper::RecordedValue("scheduled"), 1);
        EQUALS(ResourceIsolationTest::TickUntilRecorded(engine, "fired"), true);

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("sandboxes every resource's runtime when the engine is sandboxed", {
        NodeEngineOptions options;
        options.sandboxed = true;
        NodeEngine engine(options);
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        EQUALS(engine.CreateResourceRuntime("boxed"), true);
        STREQUALS(NodeTest::Eval(*engine.GetResourceRuntime("boxed"), "try { require('node:fs'); 'loaded' } catch (e) { e.message }").c_str(), "Module 'node:fs' is not available in sandbox mode");

        engine.DestroyResourceRuntime("boxed");
        engine.Shutdown();
    });
    IT("frees a resource stopped by another resource's event handler only once the dispatch is over", {
        TestManagerHelper::Cleanup();
        // The emission snapshots both handlers; the first stops the second's resource while the snapshot still holds it.
        ResourceIsolationTest::WriteResource("stopper", "Events.on('probe', () => __stop('stopped'));");
        ResourceIsolationTest::WriteResource("stopped", "Events.on('probe', () => {});");
        ResourceIsolationTest::WriteResource("prober", "Events.on('resourceStart', (name) => { if (name === 'prober') { Events.emit('probe'); __record('emitReturned', 1); } });");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        ResourceIsolationTest::StopTarget() = &manager;
        TestManagerHelper::RegisterEvents(engine, manager, ResourceIsolationTest::InstallStop);
        // Freed while the emission still ran, the snapshot would release its handle into a disposed isolate.
        bool disposed               = false;
        bool disposedDuringDispatch = false;
        engine.AddRuntimeDisposingListener([&](v8::Isolate *) {
            disposed               = true;
            disposedDuringDispatch = TestManagerHelper::RecordedValue("emitReturned") != 1;
        });
        EQUALS(manager.DiscoverResources(), 3u);
        EQUALS((bool)manager.StartResource("stopper"), true);
        EQUALS((bool)manager.StartResource("stopped"), true);
        EQUALS((bool)manager.StartResource("prober"), true);

        // Stopped at once, freed once nothing runs: StartResource ticks the engine while it waits for resourceStart.
        EQUALS(manager.IsResourceRunning("stopped"), false);
        engine.Tick();
        EQUALS(disposed, true);
        EQUALS(disposedDuringDispatch, false);

        manager.StopAll();
        engine.Shutdown();
        ResourceIsolationTest::StopTarget() = nullptr;
        TestManagerHelper::Cleanup();
    });

    IT("frees the emissions a stopped resource was still waiting on", {
        TestManagerHelper::Cleanup();
        ResourceIsolationTest::WriteResource("hanger", "Events.on('hang', () => new Promise(() => {})); Events.emit('hang');");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 1u);
        const size_t before = Builtins::Events::GetWaitingEmissionCount();
        EQUALS((bool)manager.StartResource("hanger"), true);
        EQUALS(Builtins::Events::GetWaitingEmissionCount(), before + 1);

        EQUALS((bool)manager.StopResource("hanger"), true);
        EQUALS(Builtins::Events::GetWaitingEmissionCount(), before);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });
    IT("finishes stopping a resource that calls process.exit() from its own resourceStop handler", {
        TestManagerHelper::Cleanup();
        // The second handler's promise can only settle in the runtime that just exited: the stop must not wait on it.
        ResourceIsolationTest::WriteResource("stop-quitter", R"(
            Events.on('resourceStop', async (name) => { if (name === 'stop-quitter') { await null; process.exit(0); } });
            Events.on('resourceStop', async (name) => { if (name === 'stop-quitter') await new Promise((resolve) => setTimeout(resolve, 50)); });
        )");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 1u);
        EQUALS((bool)manager.StartResource("stop-quitter"), true);

        const auto began = std::chrono::steady_clock::now();
        manager.StopResource("stop-quitter");
        const auto took = std::chrono::steady_clock::now() - began;
        EQUALS(took < std::chrono::milliseconds(config.resourceStopTimeoutMs / 2), true);
        engine.Tick();
        EQUALS(manager.IsResourceRunning("stop-quitter"), false);
        EQUALS(engine.GetResourceRuntime("stop-quitter") == nullptr, true);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("stops a resource that calls process.exit() while its resourceStart is settling", {
        TestManagerHelper::Cleanup();
        // It holds an export in its runtime: freeing the runtime under the start would leave the resource holding a
        // handle into a disposed isolate.
        ResourceIsolationTest::WriteResource("start-quitter", "Exports.register('api', { ready: true }); Events.on('resourceStart', async (name) => { if (name === 'start-quitter') { await null; process.exit(0); } });", "\"api\"");
        ResourceIsolationTest::WriteResource("bystander", "let beats = 0; setInterval(() => __record('beats', ++beats), 1);");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 2u);
        EQUALS((bool)manager.StartResource("bystander"), true);
        manager.StartResource("start-quitter");

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while ((manager.IsResourceRunning("start-quitter") || engine.GetResourceRuntime("start-quitter") != nullptr) && std::chrono::steady_clock::now() < deadline) {
            engine.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EQUALS(manager.IsResourceRunning("start-quitter"), false);
        EQUALS(engine.GetResourceRuntime("start-quitter") == nullptr, true);

        // The rest of the server carries on, and the resource can start again.
        const int32_t beats = TestManagerHelper::RecordedValue("beats");
        for (int i = 0; i < 20 && TestManagerHelper::RecordedValue("beats") == beats; ++i) {
            engine.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        EQUALS(TestManagerHelper::RecordedValue("beats") > beats, true);

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });
});
