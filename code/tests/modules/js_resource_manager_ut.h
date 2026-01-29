/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/js/node_engine.h"
#include "scripting/js/resource/js_resource_manager.h"

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>

#include <cstdlib>
#include <fstream>

// Helper class to manage test JS resource directories for manager tests
class TestJSManagerHelper {
  public:
    static std::string GetTestResourcePath() {
#ifdef _WIN32
        const char *temp = std::getenv("TEMP");
        if (!temp) temp = std::getenv("TMP");
        if (!temp) temp = "C:\\Temp";
        return std::string(temp) + "\\framework_jsm_test_resources";
#else
        return "/tmp/framework_jsm_test_resources";
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

    static void Cleanup() {
        cppfs::FileHandle testDir = cppfs::fs::open(GetTestResourcePath());
        if (testDir.exists()) {
            testDir.removeDirectoryRec();
        }
    }
};

MODULE(js_resource_manager, {
    using namespace Framework::Scripting::JS;

    // ==================== Basic Initialization ====================

    IT("can create and destroy resource manager", {
        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager *manager = new JSResourceManager(&engine, config);
        NEQUALS(manager, nullptr);

        delete manager;
        engine.Shutdown();
    });

    IT("GetConfig returns configuration", {
        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();
        config.isClient = true;
        config.cascadeStopDependents = false;

        JSResourceManager manager(&engine, config);

        const auto &retrievedConfig = manager.GetConfig();
        STREQUALS(retrievedConfig.resourcesPath.c_str(), config.resourcesPath.c_str());
        EQUALS(retrievedConfig.isClient, true);
        EQUALS(retrievedConfig.cascadeStopDependents, false);

        engine.Shutdown();
    });

    IT("SetConfig updates configuration", {
        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = "/old/path";

        JSResourceManager manager(&engine, config);

        JSResourceManagerConfig newConfig;
        newConfig.resourcesPath = "/new/path";
        manager.SetConfig(newConfig);

        STREQUALS(manager.GetConfig().resourcesPath.c_str(), "/new/path");

        engine.Shutdown();
    });

    // ==================== Resource Discovery ====================

    IT("discovers resources in directory", {
        TestJSManagerHelper::CreateTestResource("discover-1", R"({
            "name": "discover-1",
            "version": "1.0.0"
        })");
        TestJSManagerHelper::CreateTestResource("discover-2", R"({
            "name": "discover-2",
            "version": "2.0.0"
        })");

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);
        size_t discovered = manager.DiscoverResources();

        EQUALS(discovered, 2u);
        EQUALS(manager.GetResourceCount(), 2u);
        EQUALS(manager.HasResource("discover-1"), true);
        EQUALS(manager.HasResource("discover-2"), true);

        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    IT("DiscoverResource adds single resource", {
        TestJSManagerHelper::CreateTestResource("single-disc", R"({
            "name": "single-disc",
            "version": "1.0.0"
        })");

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);

        bool result = manager.DiscoverResource(TestJSManagerHelper::GetTestResourcePath() + "/single-disc");
        EQUALS(result, true);
        EQUALS(manager.HasResource("single-disc"), true);

        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    IT("returns 0 when resources directory is empty", {
        TestJSManagerHelper::Cleanup(); // Ensure empty

        cppfs::FileHandle testDir = cppfs::fs::open(TestJSManagerHelper::GetTestResourcePath());
        if (!testDir.exists()) {
            testDir.createDirectory();
        }

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);
        size_t discovered = manager.DiscoverResources();

        EQUALS(discovered, 0u);
        EQUALS(manager.GetResourceCount(), 0u);

        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    // ==================== Resource Registry ====================

    IT("GetAllResourceNames returns discovered resources", {
        TestJSManagerHelper::CreateTestResource("reg-1", R"({
            "name": "reg-1",
            "version": "1.0.0"
        })");
        TestJSManagerHelper::CreateTestResource("reg-2", R"({
            "name": "reg-2",
            "version": "1.0.0"
        })");

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto names = manager.GetAllResourceNames();
        EQUALS(names.size(), 2u);

        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    IT("GetResource returns resource by name", {
        TestJSManagerHelper::CreateTestResource("get-test", R"({
            "name": "get-test",
            "version": "3.0.0"
        })");

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);
        manager.DiscoverResources();

        const JSResource *resource = manager.GetResource("get-test");
        NEQUALS(resource, nullptr);
        STREQUALS(resource->GetName().c_str(), "get-test");
        STREQUALS(resource->GetVersion().c_str(), "3.0.0");

        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    IT("GetResource returns nullptr for unknown resource", {
        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);

        const JSResource *resource = manager.GetResource("nonexistent");
        EQUALS(resource, nullptr);

        engine.Shutdown();
    });

    IT("HasResource checks for resource existence", {
        TestJSManagerHelper::CreateTestResource("has-test", R"({
            "name": "has-test",
            "version": "1.0.0"
        })");

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);
        manager.DiscoverResources();

        EQUALS(manager.HasResource("has-test"), true);
        EQUALS(manager.HasResource("nonexistent"), false);

        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    // ==================== Resource State ====================

    IT("GetResourceState returns correct state", {
        TestJSManagerHelper::CreateTestResource("state-test", R"({
            "name": "state-test",
            "version": "1.0.0"
        })");

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);
        manager.DiscoverResources();

        ResourceState state = manager.GetResourceState("state-test");
        EQUALS(state, ResourceState::Unloaded);

        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    IT("IsResourceRunning returns false for unloaded resources", {
        TestJSManagerHelper::CreateTestResource("running-test", R"({
            "name": "running-test",
            "version": "1.0.0"
        })");

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);
        manager.DiscoverResources();

        EQUALS(manager.IsResourceRunning("running-test"), false);

        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    // ==================== Dependencies ====================

    IT("GetDependencies returns resource dependencies", {
        TestJSManagerHelper::CreateTestResource("dep-core", R"({
            "name": "dep-core",
            "version": "1.0.0"
        })");
        TestJSManagerHelper::CreateTestResource("dep-child", R"({
            "name": "dep-child",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [{"name": "dep-core"}]
            }
        })");

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto deps = manager.GetDependencies("dep-child");
        EQUALS(deps.count("dep-core"), 1u);

        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    IT("GetDependents returns resources that depend on given resource", {
        TestJSManagerHelper::CreateTestResource("parent-res", R"({
            "name": "parent-res",
            "version": "1.0.0"
        })");
        TestJSManagerHelper::CreateTestResource("child-res", R"({
            "name": "child-res",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [{"name": "parent-res"}]
            }
        })");

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto dependents = manager.GetDependents("parent-res");
        EQUALS(dependents.count("child-res"), 1u);

        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    // ==================== Statistics ====================

    IT("GetRunningResourceCount returns zero when no resources running", {
        TestJSManagerHelper::CreateTestResource("stats-test", R"({
            "name": "stats-test",
            "version": "1.0.0"
        })");

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);
        manager.DiscoverResources();

        EQUALS(manager.GetRunningResourceCount(), 0u);

        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    // ==================== Callbacks ====================

    IT("fires OnResourceStarted callback", {
        TestJSManagerHelper::CreateTestResource("callback-start", R"({
            "name": "callback-start",
            "version": "1.0.0",
            "mafiahub": {
                "server": "main.js"
            }
        })");
        TestJSManagerHelper::CreateTestScript("callback-start", "main.js", "// empty script");

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);

        std::string startedResource;
        manager.SetOnResourceStarted([&startedResource](const std::string &name) {
            startedResource = name;
        });

        manager.DiscoverResources();
        manager.StartResource("callback-start");

        STREQUALS(startedResource.c_str(), "callback-start");

        manager.StopAll();
        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    IT("fires OnResourceStopped callback", {
        TestJSManagerHelper::CreateTestResource("callback-stop", R"({
            "name": "callback-stop",
            "version": "1.0.0",
            "mafiahub": {
                "server": "main.js"
            }
        })");
        TestJSManagerHelper::CreateTestScript("callback-stop", "main.js", "// empty script");

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);

        std::string stoppedResource;
        manager.SetOnResourceStopped([&stoppedResource](const std::string &name) {
            stoppedResource = name;
        });

        manager.DiscoverResources();
        manager.StartResource("callback-stop");
        manager.StopResource("callback-stop");

        STREQUALS(stoppedResource.c_str(), "callback-stop");

        engine.Shutdown();
        TestJSManagerHelper::Cleanup();
    });

    // ==================== Current Resource Context ====================

    IT("SetCurrentResourceContext and GetCurrentResourceContext work correctly", {
        NodeEngine engine;
        EQUALS(engine.Init(), true);

        JSResourceManagerConfig config;
        config.resourcesPath = TestJSManagerHelper::GetTestResourcePath();

        JSResourceManager manager(&engine, config);

        STREQUALS(manager.GetCurrentResourceContext().c_str(), "");

        manager.SetCurrentResourceContext("test-resource");
        STREQUALS(manager.GetCurrentResourceContext().c_str(), "test-resource");

        manager.SetCurrentResourceContext("");
        STREQUALS(manager.GetCurrentResourceContext().c_str(), "");

        engine.Shutdown();
    });

    // ==================== Cleanup ====================

    IT("final cleanup", {
        TestJSManagerHelper::Cleanup();
    });
})
