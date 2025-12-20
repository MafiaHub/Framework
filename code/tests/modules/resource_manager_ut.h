/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/resource/resource_manager.h"

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>
#include <sol/sol.hpp>

#include <cstdlib>
#include <fstream>
#include <thread>

// Helper class to manage test resource directories for ResourceManager tests
class TestResourceManagerHelper {
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

    static void CreateTestResource(const std::string &name, const std::string &manifestJson) {
        std::string basePath     = GetTestResourcePath();
        std::string resourcePath = basePath + "/" + name;

        cppfs::FileHandle baseDir = cppfs::fs::open(basePath);
        if (!baseDir.exists()) {
            baseDir.createDirectory();
        }

        cppfs::FileHandle resourceDir = cppfs::fs::open(resourcePath);
        if (!resourceDir.exists()) {
            resourceDir.createDirectory();
        }

        std::string manifestPath = resourcePath + "/manifest.json";
        std::ofstream manifestFile(manifestPath);
        manifestFile << manifestJson;
        manifestFile.close();
    }

    static void CreateTestScript(const std::string &resourceName, const std::string &scriptName, const std::string &content) {
        std::string scriptPath = GetTestResourcePath() + "/" + resourceName + "/" + scriptName;

        size_t lastSlash = scriptName.rfind('/');
        if (lastSlash != std::string::npos) {
            std::string subdir       = GetTestResourcePath() + "/" + resourceName + "/" + scriptName.substr(0, lastSlash);
            cppfs::FileHandle subdirHandle = cppfs::fs::open(subdir);
            if (!subdirHandle.exists()) {
                subdirHandle.createDirectory();
            }
        }

        std::ofstream scriptFile(scriptPath);
        scriptFile << content;
        scriptFile.close();
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

    // ==================== Configuration ====================

    IT("initializes with default config", {
        TestResourceManagerHelper::Cleanup();

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        ResourceManager manager(&luaState, config);
        const auto &resultConfig = manager.GetConfig();

        STREQUALS(resultConfig.resourcesPath.c_str(), "resources");
        EQUALS(resultConfig.isClient, false);
        EQUALS(resultConfig.cascadeStopDependents, true);
        EQUALS(resultConfig.warnOnMissingDependency, false);

        TestResourceManagerHelper::Cleanup();
    });

    IT("initializes with custom config", {
        TestResourceManagerHelper::Cleanup();

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath           = "/custom/path";
        config.isClient                = true;
        config.cascadeStopDependents   = false;
        config.warnOnMissingDependency = true;

        ResourceManager manager(&luaState, config);
        const auto &resultConfig = manager.GetConfig();

        STREQUALS(resultConfig.resourcesPath.c_str(), "/custom/path");
        EQUALS(resultConfig.isClient, true);
        EQUALS(resultConfig.cascadeStopDependents, false);
        EQUALS(resultConfig.warnOnMissingDependency, true);

        TestResourceManagerHelper::Cleanup();
    });

    IT("SetConfig updates configuration", {
        TestResourceManagerHelper::Cleanup();

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        ResourceManager manager(&luaState, config);

        ResourceManagerConfig newConfig;
        newConfig.resourcesPath = "/new/path";
        newConfig.isClient      = true;

        manager.SetConfig(newConfig);

        STREQUALS(manager.GetConfig().resourcesPath.c_str(), "/new/path");
        EQUALS(manager.GetConfig().isClient, true);

        TestResourceManagerHelper::Cleanup();
    });

    // ==================== Discovery ====================

    IT("discovers resources in directory", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("resource-a", R"({
            "name": "resource-a",
            "version": "1.0.0"
        })");
        TestResourceManagerHelper::CreateTestResource("resource-b", R"({
            "name": "resource-b",
            "version": "2.0.0"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        size_t count = manager.DiscoverResources();

        EQUALS(count, 2u);
        EQUALS(manager.HasResource("resource-a"), true);
        EQUALS(manager.HasResource("resource-b"), true);
        EQUALS(manager.HasResource("nonexistent"), false);

        TestResourceManagerHelper::Cleanup();
    });

    IT("skips invalid manifests during discovery", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("valid-resource", R"({
            "name": "valid-resource",
            "version": "1.0.0"
        })");
        TestResourceManagerHelper::CreateTestResource("invalid-resource", R"({
            "name": "123invalid",
            "version": "1.0.0"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        size_t count = manager.DiscoverResources();

        EQUALS(count, 1u);
        EQUALS(manager.HasResource("valid-resource"), true);
        EQUALS(manager.HasResource("123invalid"), false);

        TestResourceManagerHelper::Cleanup();
    });

    IT("DiscoverResource adds single resource", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("single-resource", R"({
            "name": "single-resource",
            "version": "1.0.0"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        ResourceManager manager(&luaState, config);

        std::string resourcePath = TestResourceManagerHelper::GetTestResourcePath() + "/single-resource";
        bool success             = manager.DiscoverResource(resourcePath);

        EQUALS(success, true);
        EQUALS(manager.HasResource("single-resource"), true);
        EQUALS(manager.GetResourceCount(), 1u);

        TestResourceManagerHelper::Cleanup();
    });

    IT("DiscoverResource fails for invalid path", {
        TestResourceManagerHelper::Cleanup();

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        ResourceManager manager(&luaState, config);

        bool success = manager.DiscoverResource("/nonexistent/path");

        EQUALS(success, false);
        EQUALS(manager.GetResourceCount(), 0u);

        TestResourceManagerHelper::Cleanup();
    });

    IT("DiscoverResource fails for duplicate resource", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("duplicate-test", R"({
            "name": "duplicate-test",
            "version": "1.0.0"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        ResourceManager manager(&luaState, config);

        std::string resourcePath = TestResourceManagerHelper::GetTestResourcePath() + "/duplicate-test";
        manager.DiscoverResource(resourcePath);

        // Try to discover again
        bool success = manager.DiscoverResource(resourcePath);

        EQUALS(success, false);
        EQUALS(manager.GetResourceCount(), 1u);

        TestResourceManagerHelper::Cleanup();
    });

    // ==================== Registry Queries ====================

    IT("GetAllResourceNames returns all discovered resources", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("res-1", R"({"name": "res-1", "version": "1.0.0"})");
        TestResourceManagerHelper::CreateTestResource("res-2", R"({"name": "res-2", "version": "1.0.0"})");
        TestResourceManagerHelper::CreateTestResource("res-3", R"({"name": "res-3", "version": "1.0.0"})");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        auto names = manager.GetAllResourceNames();

        EQUALS(names.size(), 3u);

        TestResourceManagerHelper::Cleanup();
    });

    IT("GetRunningResourceNames returns empty when no resources running", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("not-running", R"({"name": "not-running", "version": "1.0.0"})");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        auto running = manager.GetRunningResourceNames();

        EQUALS(running.size(), 0u);

        TestResourceManagerHelper::Cleanup();
    });

    IT("GetResourceState returns correct state", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("state-test", R"({"name": "state-test", "version": "1.0.0"})");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        ResourceState state = manager.GetResourceState("state-test");
        EQUALS(static_cast<int>(state), static_cast<int>(ResourceState::Unloaded));

        ResourceState unknownState = manager.GetResourceState("unknown");
        EQUALS(static_cast<int>(unknownState), static_cast<int>(ResourceState::Unloaded));

        TestResourceManagerHelper::Cleanup();
    });

    IT("GetResource returns resource pointer", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("get-test", R"({
            "name": "get-test",
            "version": "2.0.0",
            "author": "Test Author"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        const Resource *resource = manager.GetResource("get-test");

        NEQUALS(resource, nullptr);
        STREQUALS(resource->GetName().c_str(), "get-test");
        STREQUALS(resource->GetVersion().c_str(), "2.0.0");
        STREQUALS(resource->GetAuthor().c_str(), "Test Author");

        const Resource *unknown = manager.GetResource("unknown");
        EQUALS(unknown, nullptr);

        TestResourceManagerHelper::Cleanup();
    });

    // ==================== Lifecycle - Start/Stop ====================

    IT("StartResource starts a resource with no scripts", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("start-test", R"({
            "name": "start-test",
            "version": "1.0.0"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        auto result = manager.StartResource("start-test");

        EQUALS(result.success, true);
        EQUALS(manager.IsResourceRunning("start-test"), true);
        EQUALS(static_cast<int>(manager.GetResourceState("start-test")), static_cast<int>(ResourceState::Running));

        manager.StopAll();
        TestResourceManagerHelper::Cleanup();
    });

    IT("StartResource fails for unknown resource", {
        TestResourceManagerHelper::Cleanup();

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        ResourceManager manager(&luaState, config);

        auto result = manager.StartResource("unknown");

        EQUALS(result.success, false);
        NEQUALS(result.error.find("not found"), std::string::npos);

        TestResourceManagerHelper::Cleanup();
    });

    IT("StartResource is idempotent", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("idempotent-test", R"({
            "name": "idempotent-test",
            "version": "1.0.0"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        manager.StartResource("idempotent-test");
        auto result = manager.StartResource("idempotent-test");

        EQUALS(result.success, true);
        EQUALS(manager.GetRunningResourceCount(), 1u);

        manager.StopAll();
        TestResourceManagerHelper::Cleanup();
    });

    IT("StopResource stops a running resource", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("stop-test", R"({
            "name": "stop-test",
            "version": "1.0.0"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        manager.StartResource("stop-test");
        EQUALS(manager.IsResourceRunning("stop-test"), true);

        auto result = manager.StopResource("stop-test");

        EQUALS(result.success, true);
        EQUALS(manager.IsResourceRunning("stop-test"), false);
        EQUALS(static_cast<int>(manager.GetResourceState("stop-test")), static_cast<int>(ResourceState::Stopped));

        TestResourceManagerHelper::Cleanup();
    });

    IT("StopResource succeeds for already stopped resource", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("already-stopped", R"({
            "name": "already-stopped",
            "version": "1.0.0"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        auto result = manager.StopResource("already-stopped");

        EQUALS(result.success, true);

        TestResourceManagerHelper::Cleanup();
    });

    IT("RestartResource restarts a running resource", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("restart-test", R"({
            "name": "restart-test",
            "version": "1.0.0"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        manager.StartResource("restart-test");
        auto result = manager.RestartResource("restart-test");

        EQUALS(result.success, true);
        EQUALS(manager.IsResourceRunning("restart-test"), true);

        manager.StopAll();
        TestResourceManagerHelper::Cleanup();
    });

    // ==================== Lifecycle - StartAll/StopAll ====================

    IT("StartAll starts all resources", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("all-1", R"({"name": "all-1", "version": "1.0.0"})");
        TestResourceManagerHelper::CreateTestResource("all-2", R"({"name": "all-2", "version": "1.0.0"})");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        auto result = manager.StartAll();

        EQUALS(result.success, true);
        EQUALS(manager.GetRunningResourceCount(), 2u);

        manager.StopAll();
        TestResourceManagerHelper::Cleanup();
    });

    IT("StopAll stops all running resources", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("stopall-1", R"({"name": "stopall-1", "version": "1.0.0"})");
        TestResourceManagerHelper::CreateTestResource("stopall-2", R"({"name": "stopall-2", "version": "1.0.0"})");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        manager.StartAll();
        EQUALS(manager.GetRunningResourceCount(), 2u);

        auto result = manager.StopAll();

        EQUALS(result.success, true);
        EQUALS(manager.GetRunningResourceCount(), 0u);

        TestResourceManagerHelper::Cleanup();
    });

    // ==================== Dependencies ====================

    IT("starts dependencies before dependent", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("dep-core", R"({
            "name": "dep-core",
            "version": "1.0.0"
        })");
        TestResourceManagerHelper::CreateTestResource("dep-game", R"({
            "name": "dep-game",
            "version": "1.0.0",
            "dependencies": [{"name": "dep-core"}]
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        // Start only dep-game, should auto-start dep-core
        auto result = manager.StartResource("dep-game");

        EQUALS(result.success, true);
        EQUALS(manager.IsResourceRunning("dep-core"), true);
        EQUALS(manager.IsResourceRunning("dep-game"), true);

        manager.StopAll();
        TestResourceManagerHelper::Cleanup();
    });

    IT("GetDependents returns direct dependents", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("base", R"({"name": "base", "version": "1.0.0"})");
        TestResourceManagerHelper::CreateTestResource("child-1", R"({
            "name": "child-1",
            "version": "1.0.0",
            "dependencies": ["base"]
        })");
        TestResourceManagerHelper::CreateTestResource("child-2", R"({
            "name": "child-2",
            "version": "1.0.0",
            "dependencies": ["base"]
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        auto dependents = manager.GetDependents("base");

        EQUALS(dependents.size(), 2u);
        EQUALS(dependents.count("child-1"), 1u);
        EQUALS(dependents.count("child-2"), 1u);

        TestResourceManagerHelper::Cleanup();
    });

    IT("GetDependencies returns direct dependencies", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("lib-a", R"({"name": "lib-a", "version": "1.0.0"})");
        TestResourceManagerHelper::CreateTestResource("lib-b", R"({"name": "lib-b", "version": "1.0.0"})");
        TestResourceManagerHelper::CreateTestResource("app", R"({
            "name": "app",
            "version": "1.0.0",
            "dependencies": ["lib-a", "lib-b"]
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        auto deps = manager.GetDependencies("app");

        EQUALS(deps.size(), 2u);
        EQUALS(deps.count("lib-a"), 1u);
        EQUALS(deps.count("lib-b"), 1u);

        TestResourceManagerHelper::Cleanup();
    });

    IT("GetLoadOrder respects dependencies", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("order-a", R"({"name": "order-a", "version": "1.0.0"})");
        TestResourceManagerHelper::CreateTestResource("order-b", R"({
            "name": "order-b",
            "version": "1.0.0",
            "dependencies": ["order-a"]
        })");
        TestResourceManagerHelper::CreateTestResource("order-c", R"({
            "name": "order-c",
            "version": "1.0.0",
            "dependencies": ["order-b"]
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        auto order = manager.GetLoadOrder();

        EQUALS(order.size(), 3u);

        // Find positions
        size_t posA = 0, posB = 0, posC = 0;
        for (size_t i = 0; i < order.size(); i++) {
            if (order[i] == "order-a") posA = i;
            if (order[i] == "order-b") posB = i;
            if (order[i] == "order-c") posC = i;
        }

        // A must come before B, B before C
        EQUALS(posA < posB, true);
        EQUALS(posB < posC, true);

        TestResourceManagerHelper::Cleanup();
    });

    IT("cascades stop to dependents by default", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("cascade-base", R"({"name": "cascade-base", "version": "1.0.0"})");
        TestResourceManagerHelper::CreateTestResource("cascade-child", R"({
            "name": "cascade-child",
            "version": "1.0.0",
            "dependencies": ["cascade-base"]
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath           = TestResourceManagerHelper::GetTestResourcePath();
        config.cascadeStopDependents   = true;

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();
        manager.StartAll();

        EQUALS(manager.IsResourceRunning("cascade-base"), true);
        EQUALS(manager.IsResourceRunning("cascade-child"), true);

        // Stop base - should cascade to child
        manager.StopResource("cascade-base");

        EQUALS(manager.IsResourceRunning("cascade-base"), false);
        EQUALS(manager.IsResourceRunning("cascade-child"), false);

        manager.StopAll();
        TestResourceManagerHelper::Cleanup();
    });

    // ==================== Script Execution ====================

    IT("executes server scripts", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("script-test", R"({
            "name": "script-test",
            "version": "1.0.0",
            "server_files": ["main.lua"]
        })");
        TestResourceManagerHelper::CreateTestScript("script-test", "main.lua", R"(
            testGlobal = 42
        )");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();
        config.isClient      = false;

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        auto result = manager.StartResource("script-test");

        EQUALS(result.success, true);

        // Verify script executed by checking the environment
        const Resource *resource = manager.GetResource("script-test");
        sol::environment *env    = const_cast<Resource *>(resource)->GetEnvironment();
        NEQUALS(env, nullptr);
        EQUALS((*env)["testGlobal"].get<int>(), 42);

        manager.StopAll();
        TestResourceManagerHelper::Cleanup();
    });

    IT("handles script syntax errors", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("syntax-error", R"({
            "name": "syntax-error",
            "version": "1.0.0",
            "server_files": ["bad.lua"]
        })");
        TestResourceManagerHelper::CreateTestScript("syntax-error", "bad.lua", R"(
            local x =
        )");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        auto result = manager.StartResource("syntax-error");

        EQUALS(result.success, false);
        EQUALS(manager.IsResourceRunning("syntax-error"), false);

        TestResourceManagerHelper::Cleanup();
    });

    // ==================== Event Callbacks ====================

    IT("fires onResourceStarted callback", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("callback-test", R"({
            "name": "callback-test",
            "version": "1.0.0"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        std::string startedResource;
        manager.SetOnResourceStarted([&startedResource](const std::string &name) {
            startedResource = name;
        });

        manager.StartResource("callback-test");

        STREQUALS(startedResource.c_str(), "callback-test");

        manager.StopAll();
        TestResourceManagerHelper::Cleanup();
    });

    IT("fires onResourceStopped callback", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("stopped-callback", R"({
            "name": "stopped-callback",
            "version": "1.0.0"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        std::string stoppedResource;
        manager.SetOnResourceStopped([&stoppedResource](const std::string &name) {
            stoppedResource = name;
        });

        manager.StartResource("stopped-callback");
        manager.StopResource("stopped-callback");

        STREQUALS(stoppedResource.c_str(), "stopped-callback");

        TestResourceManagerHelper::Cleanup();
    });

    IT("fires onResourceStateChanged callback", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("state-callback", R"({
            "name": "state-callback",
            "version": "1.0.0"
        })");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        std::vector<std::pair<ResourceState, ResourceState>> stateChanges;
        manager.SetOnResourceStateChanged([&stateChanges](const std::string &, ResourceState oldState, ResourceState newState) {
            stateChanges.push_back({oldState, newState});
        });

        manager.StartResource("state-callback");

        // Should have: Unloaded->Loading, Loading->Running
        GREATEREQ(stateChanges.size(), 2u);

        manager.StopAll();
        TestResourceManagerHelper::Cleanup();
    });

    // ==================== Current Resource Context ====================

    IT("SetCurrentResourceContext and GetCurrentResourceContext work correctly", {
        TestResourceManagerHelper::Cleanup();

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        ResourceManager manager(&luaState, config);

        STREQUALS(manager.GetCurrentResourceContext().c_str(), "");

        manager.SetCurrentResourceContext("test-resource");
        STREQUALS(manager.GetCurrentResourceContext().c_str(), "test-resource");

        manager.SetCurrentResourceContext("");
        STREQUALS(manager.GetCurrentResourceContext().c_str(), "");

        TestResourceManagerHelper::Cleanup();
    });

    // ==================== Preserved State ====================

    IT("HasPreservedState returns false initially", {
        TestResourceManagerHelper::Cleanup();

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        ResourceManager manager(&luaState, config);

        EQUALS(manager.HasPreservedState("any-resource"), false);

        TestResourceManagerHelper::Cleanup();
    });

    IT("ClearAllPreservedStates clears all states", {
        TestResourceManagerHelper::Cleanup();

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        ResourceManager manager(&luaState, config);

        // No states to clear - should not crash
        manager.ClearAllPreservedStates();

        EQUALS(manager.HasPreservedState("test"), false);

        TestResourceManagerHelper::Cleanup();
    });

    // ==================== Statistics ====================

    IT("GetResourceCount returns correct count", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("count-1", R"({"name": "count-1", "version": "1.0.0"})");
        TestResourceManagerHelper::CreateTestResource("count-2", R"({"name": "count-2", "version": "1.0.0"})");
        TestResourceManagerHelper::CreateTestResource("count-3", R"({"name": "count-3", "version": "1.0.0"})");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);

        EQUALS(manager.GetResourceCount(), 0u);

        manager.DiscoverResources();

        EQUALS(manager.GetResourceCount(), 3u);

        TestResourceManagerHelper::Cleanup();
    });

    IT("GetRunningResourceCount returns correct count", {
        TestResourceManagerHelper::Cleanup();

        TestResourceManagerHelper::CreateTestResource("running-1", R"({"name": "running-1", "version": "1.0.0"})");
        TestResourceManagerHelper::CreateTestResource("running-2", R"({"name": "running-2", "version": "1.0.0"})");

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        config.resourcesPath = TestResourceManagerHelper::GetTestResourcePath();

        ResourceManager manager(&luaState, config);
        manager.DiscoverResources();

        EQUALS(manager.GetRunningResourceCount(), 0u);

        manager.StartResource("running-1");
        EQUALS(manager.GetRunningResourceCount(), 1u);

        manager.StartResource("running-2");
        EQUALS(manager.GetRunningResourceCount(), 2u);

        manager.StopResource("running-1");
        EQUALS(manager.GetRunningResourceCount(), 1u);

        manager.StopAll();
        TestResourceManagerHelper::Cleanup();
    });

    // ==================== GetLuaState ====================

    IT("GetLuaState returns the Lua state", {
        TestResourceManagerHelper::Cleanup();

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        ResourceManager manager(&luaState, config);

        EQUALS(manager.GetLuaState(), &luaState);

        TestResourceManagerHelper::Cleanup();
    });

    // ==================== Export Call Chain ====================

    IT("export call chain is empty initially", {
        TestResourceManagerHelper::Cleanup();

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        ResourceManager manager(&luaState, config);

        EQUALS(manager.GetExportCallDepth(), 0u);
        EQUALS(manager.IsInExportCall(), false);
        STREQUALS(manager.GetExportCaller().c_str(), "");

        TestResourceManagerHelper::Cleanup();
    });

    IT("GetExportCallChain returns empty vector initially", {
        TestResourceManagerHelper::Cleanup();

        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        ResourceManagerConfig config;
        ResourceManager manager(&luaState, config);

        auto chain = manager.GetExportCallChain();
        EQUALS(chain.size(), 0u);

        TestResourceManagerHelper::Cleanup();
    });

    // ==================== Cleanup ====================

    IT("final cleanup", {
        TestResourceManagerHelper::Cleanup();
    });
})
