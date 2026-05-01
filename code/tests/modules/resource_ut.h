/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/resource/resource.h"

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>

#include <cstdlib>
#include <fstream>

// Helper class to manage test resource directories
class TestResourceHelper {
  public:
    static std::string GetTestResourcePath() {
#ifdef _WIN32
        const char *temp = std::getenv("TEMP");
        if (!temp) temp = std::getenv("TMP");
        if (!temp) temp = "C:\\Temp";
        return std::string(temp) + "\\framework_test_resources";
#else
        return "/tmp/framework_test_resources";
#endif
    }

    static void CreateTestResource(const std::string &name, const std::string &packageJson) {
        std::string basePath = GetTestResourcePath();
        std::string resourcePath = basePath + "/" + name;

        // Create directories
        cppfs::FileHandle baseDir = cppfs::fs::open(basePath);
        if (!baseDir.exists()) {
            baseDir.createDirectory();
        }

        cppfs::FileHandle resourceDir = cppfs::fs::open(resourcePath);
        if (!resourceDir.exists()) {
            resourceDir.createDirectory();
        }

        // Write package.json
        std::string packagePath = resourcePath + "/package.json";
        std::ofstream packageFile(packagePath);
        packageFile << packageJson;
        packageFile.close();
    }

    static void CreateTestScript(const std::string &resourceName, const std::string &scriptName, const std::string &content) {
        std::string scriptPath = GetTestResourcePath() + "/" + resourceName + "/" + scriptName;

        // Create subdirectory if needed
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

MODULE(resource, {
    using namespace Framework::Scripting;

    // ==================== ResourceState Helpers ====================

    IT("converts ResourceState to string correctly", {
        STREQUALS(ResourceStateToString(ResourceState::Unloaded), "unloaded");
        STREQUALS(ResourceStateToString(ResourceState::Loading), "loading");
        STREQUALS(ResourceStateToString(ResourceState::Running), "running");
        STREQUALS(ResourceStateToString(ResourceState::Stopping), "stopping");
        STREQUALS(ResourceStateToString(ResourceState::Stopped), "stopped");
        STREQUALS(ResourceStateToString(ResourceState::Error), "error");
    });

    // ==================== Manifest Loading ====================

    IT("loads valid package.json from directory", {
        TestResourceHelper::CreateTestResource("pkg-test-1", R"({
            "name": "pkg-test-1",
            "version": "2.0.0",
            "author": "Test Author",
            "description": "A test resource"
        })");

        flecs::world world;
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/pkg-test-1", &world);

        EQUALS(resource.IsManifestValid(), true);
        STREQUALS(resource.GetName().c_str(), "pkg-test-1");
        STREQUALS(resource.GetVersion().c_str(), "2.0.0");
        STREQUALS(resource.GetAuthor().c_str(), "Test Author");
        STREQUALS(resource.GetDescription().c_str(), "A test resource");
        EQUALS(resource.GetState(), ResourceState::Unloaded);

        TestResourceHelper::Cleanup();
    });

    IT("enters error state for missing package.json", {
        // Create directory without package.json
        std::string basePath = TestResourceHelper::GetTestResourcePath();
        std::string resourcePath = basePath + "/no-package";

        cppfs::FileHandle baseDir = cppfs::fs::open(basePath);
        if (!baseDir.exists()) {
            baseDir.createDirectory();
        }

        cppfs::FileHandle resourceDir = cppfs::fs::open(resourcePath);
        if (!resourceDir.exists()) {
            resourceDir.createDirectory();
        }

        flecs::world world;
        Resource resource(resourcePath, &world);

        EQUALS(resource.IsManifestValid(), false);
        EQUALS(resource.GetState(), ResourceState::Error);

        TestResourceHelper::Cleanup();
    });

    IT("enters error state for malformed JSON", {
        TestResourceHelper::CreateTestResource("pkg-test-2", R"({
            "name": "test"
            "version": "1.0.0"
        })");

        flecs::world world;
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/pkg-test-2", &world);

        EQUALS(resource.IsManifestValid(), false);
        EQUALS(resource.GetState(), ResourceState::Error);

        TestResourceHelper::Cleanup();
    });

    IT("normalizes path with trailing separator", {
        TestResourceHelper::CreateTestResource("path-test", R"({
            "name": "path-test",
            "version": "1.0.0"
        })");

        // Path with trailing slash
        flecs::world world;
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/path-test/", &world);

        EQUALS(resource.IsManifestValid(), true);
        STREQUALS(resource.GetName().c_str(), "path-test");

        TestResourceHelper::Cleanup();
    });

    // ==================== Initial State ====================

    IT("starts in Unloaded state for valid manifest", {
        TestResourceHelper::CreateTestResource("state-test-1", R"({
            "name": "state-test-1",
            "version": "1.0.0"
        })");

        flecs::world world;
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/state-test-1", &world);

        EQUALS(resource.GetState(), ResourceState::Unloaded);
        EQUALS(resource.IsRunning(), false);
        EQUALS(resource.IsStopped(), true);
        EQUALS(resource.HasError(), false);

        TestResourceHelper::Cleanup();
    });

    IT("starts in Error state for invalid manifest", {
        TestResourceHelper::CreateTestResource("state-test-2", R"({
            "version": "1.0.0"
        })");

        flecs::world world;
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/state-test-2", &world);

        EQUALS(resource.GetState(), ResourceState::Error);
        EQUALS(resource.IsRunning(), false);
        EQUALS(resource.IsStopped(), false);
        EQUALS(resource.HasError(), true);

        TestResourceHelper::Cleanup();
    });

    // ==================== Entry Points ====================

    IT("returns server entry point from mafiahub config", {
        TestResourceHelper::CreateTestResource("entry-test", R"({
            "name": "entry-test",
            "version": "1.0.0",
            "mafiahub": {
                "server": "server/main.js",
                "client": "client/main.js"
            }
        })");

        flecs::world world;
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/entry-test", &world);

        std::string serverEntry = resource.GetServerEntryPoint();
        std::string clientEntry = resource.GetClientEntryPoint();

        // Should contain the path
        EQUALS(serverEntry.find("server/main.js") != std::string::npos, true);
        EQUALS(clientEntry.find("client/main.js") != std::string::npos, true);

        TestResourceHelper::Cleanup();
    });

    IT("returns empty entry point when not specified", {
        TestResourceHelper::CreateTestResource("no-entry", R"({
            "name": "no-entry",
            "version": "1.0.0"
        })");

        flecs::world world;
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/no-entry", &world);

        STREQUALS(resource.GetServerEntryPoint().c_str(), "");
        STREQUALS(resource.GetClientEntryPoint().c_str(), "");

        TestResourceHelper::Cleanup();
    });

    // ==================== Exports ====================

    IT("HasExport checks manifest exports", {
        TestResourceHelper::CreateTestResource("export-test", R"({
            "name": "export-test",
            "version": "1.0.0",
            "mafiahub": {
                "exports": ["getData", "setConfig"]
            }
        })");

        flecs::world world;
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/export-test", &world);

        EQUALS(resource.HasExport("getData"), true);
        EQUALS(resource.HasExport("setConfig"), true);
        EQUALS(resource.HasExport("nonexistent"), false);

        TestResourceHelper::Cleanup();
    });

    // ==================== Dependencies ====================

    IT("DependsOn checks resource dependencies", {
        TestResourceHelper::CreateTestResource("depends-test", R"({
            "name": "depends-test",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [
                    {"name": "core"},
                    {"name": "utils"}
                ]
            }
        })");

        flecs::world world;
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/depends-test", &world);

        EQUALS(resource.DependsOn("core"), true);
        EQUALS(resource.DependsOn("utils"), true);
        EQUALS(resource.DependsOn("network"), false);

        TestResourceHelper::Cleanup();
    });

    // ==================== Restart Tracking ====================

    IT("tracks restart attempts correctly", {
        TestResourceHelper::CreateTestResource("restart-test-1", R"({
            "name": "restart-test-1",
            "version": "1.0.0"
        })");

        flecs::world world;
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/restart-test-1", &world);

        EQUALS(resource.GetRestartAttemptCount(), 0);

        resource.RecordRestartAttempt();
        EQUALS(resource.GetRestartAttemptCount(), 1);

        resource.RecordRestartAttempt();
        resource.RecordRestartAttempt();
        EQUALS(resource.GetRestartAttemptCount(), 3);

        TestResourceHelper::Cleanup();
    });

    IT("ClearRestartAttempts resets counter", {
        TestResourceHelper::CreateTestResource("restart-test-2", R"({
            "name": "restart-test-2",
            "version": "1.0.0"
        })");

        flecs::world world;
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/restart-test-2", &world);

        resource.RecordRestartAttempt();
        resource.RecordRestartAttempt();
        EQUALS(resource.GetRestartAttemptCount(), 2);

        resource.ClearRestartAttempts();
        EQUALS(resource.GetRestartAttemptCount(), 0);

        TestResourceHelper::Cleanup();
    });

    IT("calculates exponential backoff correctly", {
        TestResourceHelper::CreateTestResource("backoff-test", R"({
            "name": "backoff-test",
            "version": "1.0.0"
        })");

        flecs::world world;
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/backoff-test", &world);

        EQUALS(resource.GetRestartBackoffMs(), 0);

        resource.RecordRestartAttempt();
        int backoff1 = resource.GetRestartBackoffMs();
        NEQUALS(backoff1, 0);

        resource.RecordRestartAttempt();
        int backoff2 = resource.GetRestartBackoffMs();
        EQUALS(backoff2 > backoff1, true); // Exponential increase

        TestResourceHelper::Cleanup();
    });

    // ==================== Move Semantics ====================

    IT("supports move construction", {
        TestResourceHelper::CreateTestResource("move-test-1", R"({
            "name": "move-test-1",
            "version": "1.0.0"
        })");

        flecs::world world;
        Resource original(TestResourceHelper::GetTestResourcePath() + "/move-test-1", &world);
        original.RecordRestartAttempt();

        Resource moved(std::move(original));

        STREQUALS(moved.GetName().c_str(), "move-test-1");
        EQUALS(moved.GetState(), ResourceState::Unloaded);
        EQUALS(moved.IsManifestValid(), true);

        TestResourceHelper::Cleanup();
    });

    IT("supports move assignment", {
        TestResourceHelper::CreateTestResource("move-test-2a", R"({
            "name": "move-test-2a",
            "version": "1.0.0"
        })");
        TestResourceHelper::CreateTestResource("move-test-2b", R"({
            "name": "move-test-2b",
            "version": "2.0.0"
        })");

        flecs::world world;
        Resource original(TestResourceHelper::GetTestResourcePath() + "/move-test-2a", &world);
        Resource target(TestResourceHelper::GetTestResourcePath() + "/move-test-2b", &world);

        target = std::move(original);

        STREQUALS(target.GetName().c_str(), "move-test-2a");
        STREQUALS(target.GetVersion().c_str(), "1.0.0");

        TestResourceHelper::Cleanup();
    });

    // ==================== Path Access ====================

    IT("GetPath returns resource directory path", {
        TestResourceHelper::CreateTestResource("path-access", R"({
            "name": "path-access",
            "version": "1.0.0"
        })");

        std::string expectedPath = TestResourceHelper::GetTestResourcePath() + "/path-access";
        flecs::world world;
        Resource resource(expectedPath, &world);

        STREQUALS(resource.GetPath().c_str(), expectedPath.c_str());

        TestResourceHelper::Cleanup();
    });

    // ==================== Root entity ====================

    IT("Resource creates root resource", {
        TestResourceHelper::CreateTestResource("fooResource", R"({
            "name": "foo-bar",
            "version": "1.0.0"
        })");

        std::string expectedPath = TestResourceHelper::GetTestResourcePath() + "/fooResource";
        flecs::world world;
        flecs::entity root;
        {
            Resource resource(expectedPath, &world);

            root = resource.GetRootEntity();
            EQUALS(root.is_alive(), true);
            STREQUALS(root.name().c_str(), "foo-bar");
            EQUALS(root.get<OwnedResource>().value, &resource);
        }
        EQUALS(root.is_alive(), false);

        TestResourceHelper::Cleanup();
    });

    // ==================== Cleanup ====================

    IT("final cleanup", {
        TestResourceHelper::Cleanup();
    });
})
