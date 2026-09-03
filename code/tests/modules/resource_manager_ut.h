/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>

// Helper class to manage test resource directories for manager tests
class TestManagerHelper {
  public:
    static std::string GetTestResourcePath() {
#ifdef _WIN32
        const char *temp = std::getenv("TEMP");
        if (!temp) temp = std::getenv("TMP");
        if (!temp) temp = "C:\\Temp";
        return std::string(temp) + "\\framework_rm_test_resources";
#else
        return "/tmp/framework_rm_test_resources";
#endif
    }

    static void CreateTestResource(const std::string &name, const std::string &packageJson) {
        std::string basePath = GetTestResourcePath();
        std::string resourcePath = basePath + "/" + name;

        cppfs::FileHandle baseDir = cppfs::fs::open(basePath);
        if (!baseDir.exists()) {
            baseDir.createDirectory();
        }

        cppfs::FileHandle resourceDir = cppfs::fs::open(resourcePath);
        if (!resourceDir.exists()) {
            resourceDir.createDirectory();
        }

        std::string packagePath = resourcePath + "/package.json";
        std::ofstream packageFile(packagePath);
        packageFile << packageJson;
        packageFile.close();
    }

    static void CreateTestScript(const std::string &resourceName, const std::string &scriptName, const std::string &content) {
        std::string scriptPath = GetTestResourcePath() + "/" + resourceName + "/" + scriptName;

        size_t lastSlash = scriptName.rfind('/');
        if (lastSlash != std::string::npos) {
            std::string subdir = GetTestResourcePath() + "/" + resourceName + "/" + scriptName.substr(0, lastSlash);
            cppfs::FileHandle subdirHandle = cppfs::fs::open(subdir);
            if (!subdirHandle.exists()) {
                subdirHandle.createDirectory();
            }
        }

        std::ofstream scriptFile(scriptPath);
        scriptFile << content;
        scriptFile.close();
    }

    static void RegisterEvents(Framework::Scripting::NodeEngine &engine, Framework::Scripting::ResourceManager &manager) {
        v8::Isolate *isolate = engine.GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine.GetContext();
        v8::Context::Scope contextScope(context);
        v8::Local<v8::Value> coreValue;
        if (!context->Global()->Get(context, v8::String::NewFromUtf8Literal(isolate, "Core")).ToLocal(&coreValue) || !coreValue->IsObject()) {
            coreValue = v8::Object::New(isolate);
            context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "Core"), coreValue).Check();
        }
        manager.GetEvents().Register(isolate, context, coreValue.As<v8::Object>(), &manager);
    }

    static int32_t EvalInt(Framework::Scripting::NodeEngine &engine, const char *source) {
        v8::Isolate *isolate = engine.GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine.GetContext();
        v8::Context::Scope contextScope(context);
        v8::TryCatch tryCatch(isolate);
        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(context, v8::String::NewFromUtf8(isolate, source).ToLocalChecked()).ToLocal(&script)) {
            return -1;
        }
        v8::Local<v8::Value> result;
        if (!script->Run(context).ToLocal(&result) || !result->IsNumber()) {
            return -1;
        }
        return result->Int32Value(context).FromMaybe(-1);
    }

    static void Cleanup() {
        cppfs::FileHandle testDir = cppfs::fs::open(GetTestResourcePath());
        if (testDir.exists()) {
            testDir.removeDirectoryRec();
        }
    }
};

MODULE(resource_manager, {
    using namespace Framework::Scripting;

    // ==================== Basic Initialization ====================

    IT("can create and destroy resource manager", {
        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager *manager = new ResourceManager(&engine, config);
        NEQUALS(manager, nullptr);

        delete manager;
        engine.Shutdown();
    });

    IT("GetConfig returns configuration", {
        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        config.isClient = true;
        config.cascadeStopDependents = false;
        config.resourceStartTimeoutMs = 1234;
        config.resourceStopTimeoutMs  = 5678;

        ResourceManager manager(&engine, config);

        const auto &retrievedConfig = manager.GetConfig();
        STREQUALS(retrievedConfig.resourcesPath.c_str(), config.resourcesPath.c_str());
        EQUALS(retrievedConfig.isClient, true);
        EQUALS(retrievedConfig.cascadeStopDependents, false);
        EQUALS(retrievedConfig.resourceStartTimeoutMs, 1234);
        EQUALS(retrievedConfig.resourceStopTimeoutMs, 5678);

        engine.Shutdown();
    });

    IT("SetConfig updates configuration", {
        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = "/old/path";

        ResourceManager manager(&engine, config);

        ResourceManagerConfig newConfig;
        newConfig.resourcesPath = "/new/path";
        manager.SetConfig(newConfig);

        STREQUALS(manager.GetConfig().resourcesPath.c_str(), "/new/path");

        engine.Shutdown();
    });

    IT("Reset clears session state without replacing the manager", {
        TestManagerHelper::Cleanup();
        TestManagerHelper::CreateTestResource("reset-me", R"({
            "name": "reset-me",
            "version": "1.0.0"
        })");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        ResourceManager manager(&engine, config);

        EQUALS(manager.DiscoverResources(), 1u);
        manager.SetCurrentResourceContext("reset-me");
        EQUALS(engine.GetResourceManager(), &manager);

        manager.Reset();

        EQUALS(engine.GetResourceManager(), &manager);
        EQUALS(manager.GetResourceCount(), 0u);
        EQUALS(manager.GetRunningResourceCount(), 0u);
        STREQUALS(manager.GetCurrentResourceContext().c_str(), "");

        ResourceManagerConfig newConfig;
        newConfig.resourcesPath = "/new/session/path";
        manager.SetConfig(newConfig);
        STREQUALS(manager.GetConfig().resourcesPath.c_str(), "/new/session/path");

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    // ==================== Resource Discovery ====================

    IT("discovers resources in directory", {
        TestManagerHelper::CreateTestResource("discover-1", R"({
            "name": "discover-1",
            "version": "1.0.0"
        })");
        TestManagerHelper::CreateTestResource("discover-2", R"({
            "name": "discover-2",
            "version": "2.0.0"
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        size_t discovered = manager.DiscoverResources();

        EQUALS(discovered, 2u);
        EQUALS(manager.GetResourceCount(), 2u);
        EQUALS(manager.HasResource("discover-1"), true);
        EQUALS(manager.HasResource("discover-2"), true);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("DiscoverResource adds single resource", {
        TestManagerHelper::CreateTestResource("single-disc", R"({
            "name": "single-disc",
            "version": "1.0.0"
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);

        bool result = manager.DiscoverResource(TestManagerHelper::GetTestResourcePath() + "/single-disc");
        EQUALS(result, true);
        EQUALS(manager.HasResource("single-disc"), true);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("returns 0 when resources directory is empty", {
        TestManagerHelper::Cleanup(); // Ensure empty

        cppfs::FileHandle testDir = cppfs::fs::open(TestManagerHelper::GetTestResourcePath());
        if (!testDir.exists()) {
            testDir.createDirectory();
        }

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        size_t discovered = manager.DiscoverResources();

        EQUALS(discovered, 0u);
        EQUALS(manager.GetResourceCount(), 0u);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    // ==================== Resource Registry ====================

    IT("GetAllResourceNames returns discovered resources", {
        TestManagerHelper::CreateTestResource("reg-1", R"({
            "name": "reg-1",
            "version": "1.0.0"
        })");
        TestManagerHelper::CreateTestResource("reg-2", R"({
            "name": "reg-2",
            "version": "1.0.0"
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto names = manager.GetAllResourceNames();
        EQUALS(names.size(), 2u);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("GetResource returns resource by name", {
        TestManagerHelper::CreateTestResource("get-test", R"({
            "name": "get-test",
            "version": "3.0.0"
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        const Resource *resource = manager.GetResource("get-test");
        NEQUALS(resource, nullptr);
        STREQUALS(resource->GetName().c_str(), "get-test");
        STREQUALS(resource->GetVersion().c_str(), "3.0.0");

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("GetResource returns nullptr for unknown resource", {
        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);

        const Resource *resource = manager.GetResource("nonexistent");
        EQUALS(resource, nullptr);

        engine.Shutdown();
    });

    IT("HasResource checks for resource existence", {
        TestManagerHelper::CreateTestResource("has-test", R"({
            "name": "has-test",
            "version": "1.0.0"
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        EQUALS(manager.HasResource("has-test"), true);
        EQUALS(manager.HasResource("nonexistent"), false);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    // ==================== Resource State ====================

    IT("GetResourceState returns correct state", {
        TestManagerHelper::CreateTestResource("state-test", R"({
            "name": "state-test",
            "version": "1.0.0"
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        ResourceState state = manager.GetResourceState("state-test");
        EQUALS(state, ResourceState::Unloaded);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("IsResourceRunning returns false for unloaded resources", {
        TestManagerHelper::CreateTestResource("running-test", R"({
            "name": "running-test",
            "version": "1.0.0"
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        EQUALS(manager.IsResourceRunning("running-test"), false);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    // ==================== Dependencies ====================

    IT("GetDependencies returns resource dependencies", {
        TestManagerHelper::CreateTestResource("dep-core", R"({
            "name": "dep-core",
            "version": "1.0.0"
        })");
        TestManagerHelper::CreateTestResource("dep-child", R"({
            "name": "dep-child",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [{"name": "dep-core"}]
            }
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto deps = manager.GetDependencies("dep-child");
        EQUALS(deps.count("dep-core"), 1u);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("GetDependents returns resources that depend on given resource", {
        TestManagerHelper::CreateTestResource("parent-res", R"({
            "name": "parent-res",
            "version": "1.0.0"
        })");
        TestManagerHelper::CreateTestResource("child-res", R"({
            "name": "child-res",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [{"name": "parent-res"}]
            }
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto dependents = manager.GetDependents("parent-res");
        EQUALS(dependents.count("child-res"), 1u);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("StartAll skips a missing optional dependency", {
        TestManagerHelper::Cleanup();
        TestManagerHelper::CreateTestResource("opt-user", R"({
            "name": "opt-user",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [{"name": "opt-absent", "optional": true}]
            }
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto result = manager.StartAll();
        EQUALS(static_cast<bool>(result), true);
        // StartAll reports Ok even when a resource fails to start, so assert on the resource itself.
        EQUALS(manager.IsResourceRunning("opt-user"), true);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("StartAll fails on a missing required dependency", {
        TestManagerHelper::Cleanup();
        TestManagerHelper::CreateTestResource("req-user", R"({
            "name": "req-user",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [{"name": "req-absent"}]
            }
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto result = manager.StartAll();
        EQUALS(static_cast<bool>(result), false);
        EQUALS(result.GetError(), std::string("Resource 'req-user' depends on missing resource 'req-absent'"));

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("optional dependency still orders the load when it is installed", {
        TestManagerHelper::Cleanup();
        TestManagerHelper::CreateTestResource("opt-present", R"({
            "name": "opt-present",
            "version": "1.0.0"
        })");
        TestManagerHelper::CreateTestResource("opt-consumer", R"({
            "name": "opt-consumer",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [{"name": "opt-present", "optional": true}]
            }
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto order      = manager.GetLoadOrder();
        auto providerIt = std::find(order.begin(), order.end(), "opt-present");
        auto consumerIt = std::find(order.begin(), order.end(), "opt-consumer");
        EQUALS(providerIt != order.end(), true);
        EQUALS(consumerIt != order.end(), true);
        EQUALS(providerIt < consumerIt, true);

        manager.StartAll();
        EQUALS(manager.IsResourceRunning("opt-present"), true);
        EQUALS(manager.IsResourceRunning("opt-consumer"), true);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("a dependency declared both optional and required stays required", {
        TestManagerHelper::Cleanup();
        TestManagerHelper::CreateTestResource("dup-user", R"({
            "name": "dup-user",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [
                    {"name": "dup-absent", "optional": true},
                    {"name": "dup-absent"}
                ]
            }
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto result = manager.StartAll();
        EQUALS(static_cast<bool>(result), false);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("an installed optional dependency that fails to start does not fail the dependent", {
        TestManagerHelper::Cleanup();
        // No server/main.js is written, so this one fails with "Script not found".
        TestManagerHelper::CreateTestResource("opt-broken", R"({
            "name": "opt-broken",
            "version": "1.0.0",
            "mafiahub": {
                "serverScripts": ["server/main.js"]
            }
        })");
        TestManagerHelper::CreateTestResource("opt-tolerant", R"({
            "name": "opt-tolerant",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [{"name": "opt-broken", "optional": true}]
            }
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        manager.StartAll();
        EQUALS(manager.IsResourceRunning("opt-broken"), false);
        EQUALS(manager.IsResourceRunning("opt-tolerant"), true);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("warnOnMissingDependency starts a resource whose required dependency is absent", {
        TestManagerHelper::Cleanup();
        TestManagerHelper::CreateTestResource("warn-user", R"({
            "name": "warn-user",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [{"name": "warn-absent"}]
            }
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath            = TestManagerHelper::GetTestResourcePath();
        config.warnOnMissingDependency  = true;

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto result = manager.StartAll();
        EQUALS(static_cast<bool>(result), true);
        // Validation only warns, so the start gate has to let the dependency through too.
        EQUALS(manager.IsResourceRunning("warn-user"), true);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("a mistyped optional flag keeps the resource and treats the dependency as required", {
        TestManagerHelper::Cleanup();
        TestManagerHelper::CreateTestResource("badflag-user", R"({
            "name": "badflag-user",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [{"name": "badflag-absent", "optional": "true"}]
            }
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        // The manifest still parses, so the resource is discovered rather than dropped.
        EQUALS(manager.HasResource("badflag-user"), true);

        auto result = manager.StartAll();
        EQUALS(static_cast<bool>(result), false);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    // ==================== Statistics ====================

    IT("GetRunningResourceCount returns zero when no resources running", {
        TestManagerHelper::CreateTestResource("stats-test", R"({
            "name": "stats-test",
            "version": "1.0.0"
        })");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        EQUALS(manager.GetRunningResourceCount(), 0u);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("rejects entry points that escape resource directory", {
        TestManagerHelper::CreateTestResource("escape-test", R"({
            "name": "escape-test",
            "version": "1.0.0",
            "mafiahub": {
                "server": "../outside.js"
            }
        })");

        // Create the target outside the resource directory to simulate traversal.
        std::ofstream outsideFile(TestManagerHelper::GetTestResourcePath() + "/outside.js");
        outsideFile << "// outside";
        outsideFile.close();

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto startResult = manager.StartResource("escape-test");
        EQUALS((bool)startResult, false);
        EQUALS(startResult.GetError().find("escapes resource directory") != std::string::npos, true);
        EQUALS(manager.IsResourceRunning("escape-test"), false);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    // ==================== Callbacks ====================
});

MODULE(resource_lifecycle, {
    using namespace Framework::Scripting;

    IT("awaits async resourceStart before starting dependents", {
        TestManagerHelper::Cleanup();
        TestManagerHelper::CreateTestResource("async-dependency", R"({
            "name": "async-dependency",
            "version": "1.0.0",
            "mafiahub": { "server": "main.js" }
        })");
        TestManagerHelper::CreateTestScript("async-dependency", "main.js", R"(
            globalThis.__dependencyReady = 0;
            Core.Events.on("resourceStart", async (name) => {
                if (name !== "async-dependency") return;
                await new Promise((resolve) => setTimeout(resolve, 15));
                globalThis.__dependencyReady = 1;
            });
        )");
        TestManagerHelper::CreateTestResource("async-dependent", R"({
            "name": "async-dependent",
            "version": "1.0.0",
            "mafiahub": {
                "server": "main.js",
                "resourceDependencies": [{"name": "async-dependency"}]
            }
        })");
        TestManagerHelper::CreateTestScript("async-dependent", "main.js", R"(
            globalThis.__dependentSawReady = globalThis.__dependencyReady === 1 ? 1 : 0;
        )");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath          = TestManagerHelper::GetTestResourcePath();
        config.resourceStartTimeoutMs = 250;
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 2u);

        const auto result = manager.StartResource("async-dependent");
        EQUALS((bool)result, true);
        EQUALS(manager.IsResourceRunning("async-dependency"), true);
        EQUALS(manager.IsResourceRunning("async-dependent"), true);
        EQUALS(TestManagerHelper::EvalInt(engine, "globalThis.__dependencyReady"), 1);
        EQUALS(TestManagerHelper::EvalInt(engine, "globalThis.__dependentSawReady"), 1);

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("awaits async resourceStop before removing handlers and timers", {
        TestManagerHelper::Cleanup();
        TestManagerHelper::CreateTestResource("async-stop", R"({
            "name": "async-stop",
            "version": "1.0.0",
            "mafiahub": { "server": "main.js" }
        })");
        TestManagerHelper::CreateTestScript("async-stop", "main.js", R"(
            globalThis.__stopFinished = 0;
            globalThis.__stopSawOwnedListener = 0;
            Core.Events.on("owned-listener", () => {});
            Core.Events.on("resourceStop", async (name) => {
                if (name !== "async-stop") return;
                await new Promise((resolve) => setTimeout(resolve, 15));
                globalThis.__stopSawOwnedListener = Core.Events.listenerCount("owned-listener");
                globalThis.__stopFinished = 1;
            });
        )");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath         = TestManagerHelper::GetTestResourcePath();
        config.resourceStopTimeoutMs = 250;
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 1u);
        EQUALS((bool)manager.StartResource("async-stop"), true);

        const auto result = manager.StopResource("async-stop");
        EQUALS((bool)result, true);
        EQUALS(manager.GetResourceState("async-stop"), ResourceState::Stopped);
        EQUALS(TestManagerHelper::EvalInt(engine, "globalThis.__stopFinished"), 1);
        EQUALS(TestManagerHelper::EvalInt(engine, "globalThis.__stopSawOwnedListener"), 1);
        EQUALS(manager.GetEvents().GetListenerCount("owned-listener"), static_cast<size_t>(0));

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("fails and cleans a resource whose async start rejects", {
        TestManagerHelper::Cleanup();
        TestManagerHelper::CreateTestResource("reject-start", R"({
            "name": "reject-start",
            "version": "1.0.0",
            "mafiahub": { "server": "main.js" }
        })");
        TestManagerHelper::CreateTestScript("reject-start", "main.js", R"(
            Core.Events.on("leaked-start-listener", () => {});
            Core.Events.on("resourceStart", async (name) => {
                if (name !== "reject-start") return;
                await Promise.resolve();
                throw new Error("migration failed");
            });
        )");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath          = TestManagerHelper::GetTestResourcePath();
        config.resourceStartTimeoutMs = 250;
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 1u);

        const auto result = manager.StartResource("reject-start");
        EQUALS((bool)result, false);
        EQUALS(result.GetError().find("migration failed") != std::string::npos, true);
        EQUALS(manager.GetResourceState("reject-start"), ResourceState::Error);
        EQUALS(manager.GetEvents().GetListenerCount("leaked-start-listener"), static_cast<size_t>(0));

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("bounds async start and cleans a timed-out partial resource", {
        TestManagerHelper::Cleanup();
        TestManagerHelper::CreateTestResource("timeout-start", R"({
            "name": "timeout-start",
            "version": "1.0.0",
            "mafiahub": { "server": "main.js" }
        })");
        TestManagerHelper::CreateTestScript("timeout-start", "main.js", R"(
            Core.Events.on("leaked-timeout-listener", () => {});
            Core.Events.on("resourceStart", (name) =>
                name === "timeout-start" ? new Promise(() => {}) : undefined);
        )");

        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath          = TestManagerHelper::GetTestResourcePath();
        config.resourceStartTimeoutMs = 10;
        ResourceManager manager(&engine, config);
        TestManagerHelper::RegisterEvents(engine, manager);
        EQUALS(manager.DiscoverResources(), 1u);

        const auto result = manager.StartResource("timeout-start");
        EQUALS((bool)result, false);
        EQUALS(result.GetError().find("timed out") != std::string::npos, true);
        EQUALS(manager.GetResourceState("timeout-start"), ResourceState::Error);
        EQUALS(manager.GetEvents().GetListenerCount("leaked-timeout-listener"), static_cast<size_t>(0));

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("forces stop cleanup after async rejection or timeout", {
        const auto runCase = [](const std::string &name, const std::string &stopBody) {
            TestManagerHelper::Cleanup();
            TestManagerHelper::CreateTestResource(name, "{\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"mafiahub\":{\"server\":\"main.js\"}}");
            TestManagerHelper::CreateTestScript(name, "main.js",
                "Core.Events.on('force-cleanup-listener', () => {});"
                "Core.Events.on('resourceStop', (name) => name === '"
                    + name + "' ? (" + stopBody + ") : undefined);");

            NodeEngine engine;
            bool ok = engine.Init() == ScriptingError::SCRIPTING_NONE;
            ResourceManagerConfig config;
            config.resourcesPath         = TestManagerHelper::GetTestResourcePath();
            config.resourceStopTimeoutMs = 10;
            ResourceManager manager(&engine, config);
            TestManagerHelper::RegisterEvents(engine, manager);
            ok = ok && manager.DiscoverResources() == 1u;
            ok = ok && static_cast<bool>(manager.StartResource(name));
            ok = ok && static_cast<bool>(manager.StopResource(name));
            ok = ok && manager.GetResourceState(name) == ResourceState::Stopped;
            ok = ok && manager.GetEvents().GetListenerCount("force-cleanup-listener") == 0u;
            engine.Shutdown();
            TestManagerHelper::Cleanup();
            return ok;
        };

        EQUALS(runCase("reject-stop", "Promise.reject(new Error('final save failed'))"), true);
        EQUALS(runCase("timeout-stop", "new Promise(() => {})"), true);
    });
});

MODULE(resource_manager_callbacks, {
    using namespace Framework::Scripting;

    IT("fires OnResourceStarted callback", {
        TestManagerHelper::CreateTestResource("callback-start", R"({
            "name": "callback-start",
            "version": "1.0.0",
            "mafiahub": {
                "server": "main.js"
            }
        })");
        TestManagerHelper::CreateTestScript("callback-start", "main.js", "// empty script");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);

        std::string startedResource;
        manager.SetOnResourceStarted([&startedResource](const std::string &name) {
            startedResource = name;
        });

        manager.DiscoverResources();
        manager.StartResource("callback-start");

        STREQUALS(startedResource.c_str(), "callback-start");

        manager.StopAll();
        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    IT("fires OnResourceStopped callback", {
        TestManagerHelper::CreateTestResource("callback-stop", R"({
            "name": "callback-stop",
            "version": "1.0.0",
            "mafiahub": {
                "server": "main.js"
            }
        })");
        TestManagerHelper::CreateTestScript("callback-stop", "main.js", "// empty script");

        NodeEngine engine;

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);

        std::string stoppedResource;
        manager.SetOnResourceStopped([&stoppedResource](const std::string &name) {
            stoppedResource = name;
        });

        manager.DiscoverResources();
        manager.StartResource("callback-stop");
        manager.StopResource("callback-stop");

        STREQUALS(stoppedResource.c_str(), "callback-stop");

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    // ==================== Current Resource Context ====================

    IT("SetCurrentResourceContext and GetCurrentResourceContext work correctly", {
        NodeEngine engine;
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        ResourceManagerConfig config;

        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);

        STREQUALS(manager.GetCurrentResourceContext().c_str(), "");

        manager.SetCurrentResourceContext("test-resource");
        STREQUALS(manager.GetCurrentResourceContext().c_str(), "test-resource");

        manager.SetCurrentResourceContext("");
        STREQUALS(manager.GetCurrentResourceContext().c_str(), "");

        engine.Shutdown();
    });

    // ==================== Cleanup ====================

    IT("final cleanup", {
        TestManagerHelper::Cleanup();
    });
})
