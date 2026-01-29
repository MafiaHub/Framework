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
        EQUALS(engine.Init(), true);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager *manager = new ResourceManager(&engine, config);
        NEQUALS(manager, nullptr);

        delete manager;
        engine.Shutdown();
    });

    IT("GetConfig returns configuration", {
        NodeEngine engine;
        EQUALS(engine.Init(), true);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();
        config.isClient = true;
        config.cascadeStopDependents = false;

        ResourceManager manager(&engine, config);

        const auto &retrievedConfig = manager.GetConfig();
        STREQUALS(retrievedConfig.resourcesPath.c_str(), config.resourcesPath.c_str());
        EQUALS(retrievedConfig.isClient, true);
        EQUALS(retrievedConfig.cascadeStopDependents, false);

        engine.Shutdown();
    });

    IT("SetConfig updates configuration", {
        NodeEngine engine;
        EQUALS(engine.Init(), true);

        ResourceManagerConfig config;
        config.resourcesPath = "/old/path";

        ResourceManager manager(&engine, config);

        ResourceManagerConfig newConfig;
        newConfig.resourcesPath = "/new/path";
        manager.SetConfig(newConfig);

        STREQUALS(manager.GetConfig().resourcesPath.c_str(), "/new/path");

        engine.Shutdown();
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
        EQUALS(engine.Init(), true);

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
        EQUALS(engine.Init(), true);

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
        EQUALS(engine.Init(), true);

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
        EQUALS(engine.Init(), true);

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
        EQUALS(engine.Init(), true);

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
        EQUALS(engine.Init(), true);

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
        EQUALS(engine.Init(), true);

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
        EQUALS(engine.Init(), true);

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
        EQUALS(engine.Init(), true);

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
        EQUALS(engine.Init(), true);

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
        EQUALS(engine.Init(), true);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        auto dependents = manager.GetDependents("parent-res");
        EQUALS(dependents.count("child-res"), 1u);

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
        EQUALS(engine.Init(), true);

        ResourceManagerConfig config;
        config.resourcesPath = TestManagerHelper::GetTestResourcePath();

        ResourceManager manager(&engine, config);
        manager.DiscoverResources();

        EQUALS(manager.GetRunningResourceCount(), 0u);

        engine.Shutdown();
        TestManagerHelper::Cleanup();
    });

    // ==================== Callbacks ====================

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
        EQUALS(engine.Init(), true);

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
        EQUALS(engine.Init(), true);

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
        EQUALS(engine.Init(), true);

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
