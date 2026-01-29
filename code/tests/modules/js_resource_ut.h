/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/js/resource/js_resource.h"

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>

#include <cstdlib>
#include <fstream>

// Helper class to manage test JS resource directories
class TestJSResourceHelper {
  public:
    static std::string GetTestResourcePath() {
#ifdef _WIN32
        const char *temp = std::getenv("TEMP");
        if (!temp) temp = std::getenv("TMP");
        if (!temp) temp = "C:\\Temp";
        return std::string(temp) + "\\framework_js_test_resources";
#else
        return "/tmp/framework_js_test_resources";
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

MODULE(js_resource, {
    using namespace Framework::Scripting::JS;

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
        TestJSResourceHelper::CreateTestResource("pkg-test-1", R"({
            "name": "pkg-test-1",
            "version": "2.0.0",
            "author": "Test Author",
            "description": "A test resource"
        })");

        JSResource resource(TestJSResourceHelper::GetTestResourcePath() + "/pkg-test-1");

        EQUALS(resource.IsManifestValid(), true);
        STREQUALS(resource.GetName().c_str(), "pkg-test-1");
        STREQUALS(resource.GetVersion().c_str(), "2.0.0");
        STREQUALS(resource.GetAuthor().c_str(), "Test Author");
        STREQUALS(resource.GetDescription().c_str(), "A test resource");
        EQUALS(resource.GetState(), ResourceState::Unloaded);

        TestJSResourceHelper::Cleanup();
    });

    IT("enters error state for missing package.json", {
        // Create directory without package.json
        std::string basePath = TestJSResourceHelper::GetTestResourcePath();
        std::string resourcePath = basePath + "/no-package";

        cppfs::FileHandle baseDir = cppfs::fs::open(basePath);
        if (!baseDir.exists()) {
            baseDir.createDirectory();
        }

        cppfs::FileHandle resourceDir = cppfs::fs::open(resourcePath);
        if (!resourceDir.exists()) {
            resourceDir.createDirectory();
        }

        JSResource resource(resourcePath);

        EQUALS(resource.IsManifestValid(), false);
        EQUALS(resource.GetState(), ResourceState::Error);

        TestJSResourceHelper::Cleanup();
    });

    IT("enters error state for malformed JSON", {
        TestJSResourceHelper::CreateTestResource("pkg-test-2", R"({
            "name": "test"
            "version": "1.0.0"
        })");

        JSResource resource(TestJSResourceHelper::GetTestResourcePath() + "/pkg-test-2");

        EQUALS(resource.IsManifestValid(), false);
        EQUALS(resource.GetState(), ResourceState::Error);

        TestJSResourceHelper::Cleanup();
    });

    IT("normalizes path with trailing separator", {
        TestJSResourceHelper::CreateTestResource("path-test", R"({
            "name": "path-test",
            "version": "1.0.0"
        })");

        // Path with trailing slash
        JSResource resource(TestJSResourceHelper::GetTestResourcePath() + "/path-test/");

        EQUALS(resource.IsManifestValid(), true);
        STREQUALS(resource.GetName().c_str(), "path-test");

        TestJSResourceHelper::Cleanup();
    });

    // ==================== Initial State ====================

    IT("starts in Unloaded state for valid manifest", {
        TestJSResourceHelper::CreateTestResource("state-test-1", R"({
            "name": "state-test-1",
            "version": "1.0.0"
        })");

        JSResource resource(TestJSResourceHelper::GetTestResourcePath() + "/state-test-1");

        EQUALS(resource.GetState(), ResourceState::Unloaded);
        EQUALS(resource.IsRunning(), false);
        EQUALS(resource.IsStopped(), true);
        EQUALS(resource.HasError(), false);

        TestJSResourceHelper::Cleanup();
    });

    IT("starts in Error state for invalid manifest", {
        TestJSResourceHelper::CreateTestResource("state-test-2", R"({
            "version": "1.0.0"
        })");

        JSResource resource(TestJSResourceHelper::GetTestResourcePath() + "/state-test-2");

        EQUALS(resource.GetState(), ResourceState::Error);
        EQUALS(resource.IsRunning(), false);
        EQUALS(resource.IsStopped(), false);
        EQUALS(resource.HasError(), true);

        TestJSResourceHelper::Cleanup();
    });

    // ==================== Entry Points ====================

    IT("returns server entry point from mafiahub config", {
        TestJSResourceHelper::CreateTestResource("entry-test", R"({
            "name": "entry-test",
            "version": "1.0.0",
            "mafiahub": {
                "server": "server/main.js",
                "client": "client/main.js"
            }
        })");

        JSResource resource(TestJSResourceHelper::GetTestResourcePath() + "/entry-test");

        std::string serverEntry = resource.GetServerEntryPoint();
        std::string clientEntry = resource.GetClientEntryPoint();

        // Should contain the path
        EQUALS(serverEntry.find("server/main.js") != std::string::npos, true);
        EQUALS(clientEntry.find("client/main.js") != std::string::npos, true);

        TestJSResourceHelper::Cleanup();
    });

    IT("returns empty entry point when not specified", {
        TestJSResourceHelper::CreateTestResource("no-entry", R"({
            "name": "no-entry",
            "version": "1.0.0"
        })");

        JSResource resource(TestJSResourceHelper::GetTestResourcePath() + "/no-entry");

        STREQUALS(resource.GetServerEntryPoint().c_str(), "");
        STREQUALS(resource.GetClientEntryPoint().c_str(), "");

        TestJSResourceHelper::Cleanup();
    });

    // ==================== Exports ====================

    IT("HasExport checks manifest exports", {
        TestJSResourceHelper::CreateTestResource("export-test", R"({
            "name": "export-test",
            "version": "1.0.0",
            "mafiahub": {
                "exports": ["getData", "setConfig"]
            }
        })");

        JSResource resource(TestJSResourceHelper::GetTestResourcePath() + "/export-test");

        EQUALS(resource.HasExport("getData"), true);
        EQUALS(resource.HasExport("setConfig"), true);
        EQUALS(resource.HasExport("nonexistent"), false);

        TestJSResourceHelper::Cleanup();
    });

    // ==================== Dependencies ====================

    IT("DependsOn checks resource dependencies", {
        TestJSResourceHelper::CreateTestResource("depends-test", R"({
            "name": "depends-test",
            "version": "1.0.0",
            "mafiahub": {
                "resourceDependencies": [
                    {"name": "core"},
                    {"name": "utils"}
                ]
            }
        })");

        JSResource resource(TestJSResourceHelper::GetTestResourcePath() + "/depends-test");

        EQUALS(resource.DependsOn("core"), true);
        EQUALS(resource.DependsOn("utils"), true);
        EQUALS(resource.DependsOn("network"), false);

        TestJSResourceHelper::Cleanup();
    });

    // ==================== Restart Tracking ====================

    IT("tracks restart attempts correctly", {
        TestJSResourceHelper::CreateTestResource("restart-test-1", R"({
            "name": "restart-test-1",
            "version": "1.0.0"
        })");

        JSResource resource(TestJSResourceHelper::GetTestResourcePath() + "/restart-test-1");

        EQUALS(resource.GetRestartAttemptCount(), 0);

        resource.RecordRestartAttempt();
        EQUALS(resource.GetRestartAttemptCount(), 1);

        resource.RecordRestartAttempt();
        resource.RecordRestartAttempt();
        EQUALS(resource.GetRestartAttemptCount(), 3);

        TestJSResourceHelper::Cleanup();
    });

    IT("ClearRestartAttempts resets counter", {
        TestJSResourceHelper::CreateTestResource("restart-test-2", R"({
            "name": "restart-test-2",
            "version": "1.0.0"
        })");

        JSResource resource(TestJSResourceHelper::GetTestResourcePath() + "/restart-test-2");

        resource.RecordRestartAttempt();
        resource.RecordRestartAttempt();
        EQUALS(resource.GetRestartAttemptCount(), 2);

        resource.ClearRestartAttempts();
        EQUALS(resource.GetRestartAttemptCount(), 0);

        TestJSResourceHelper::Cleanup();
    });

    IT("calculates exponential backoff correctly", {
        TestJSResourceHelper::CreateTestResource("backoff-test", R"({
            "name": "backoff-test",
            "version": "1.0.0"
        })");

        JSResource resource(TestJSResourceHelper::GetTestResourcePath() + "/backoff-test");

        EQUALS(resource.GetRestartBackoffMs(), 0);

        resource.RecordRestartAttempt();
        int backoff1 = resource.GetRestartBackoffMs();
        NEQUALS(backoff1, 0);

        resource.RecordRestartAttempt();
        int backoff2 = resource.GetRestartBackoffMs();
        EQUALS(backoff2 > backoff1, true); // Exponential increase

        TestJSResourceHelper::Cleanup();
    });

    // ==================== Move Semantics ====================

    IT("supports move construction", {
        TestJSResourceHelper::CreateTestResource("move-test-1", R"({
            "name": "move-test-1",
            "version": "1.0.0"
        })");

        JSResource original(TestJSResourceHelper::GetTestResourcePath() + "/move-test-1");
        original.RecordRestartAttempt();

        JSResource moved(std::move(original));

        STREQUALS(moved.GetName().c_str(), "move-test-1");
        EQUALS(moved.GetState(), ResourceState::Unloaded);
        EQUALS(moved.IsManifestValid(), true);

        TestJSResourceHelper::Cleanup();
    });

    IT("supports move assignment", {
        TestJSResourceHelper::CreateTestResource("move-test-2a", R"({
            "name": "move-test-2a",
            "version": "1.0.0"
        })");
        TestJSResourceHelper::CreateTestResource("move-test-2b", R"({
            "name": "move-test-2b",
            "version": "2.0.0"
        })");

        JSResource original(TestJSResourceHelper::GetTestResourcePath() + "/move-test-2a");
        JSResource target(TestJSResourceHelper::GetTestResourcePath() + "/move-test-2b");

        target = std::move(original);

        STREQUALS(target.GetName().c_str(), "move-test-2a");
        STREQUALS(target.GetVersion().c_str(), "1.0.0");

        TestJSResourceHelper::Cleanup();
    });

    // ==================== Path Access ====================

    IT("GetPath returns resource directory path", {
        TestJSResourceHelper::CreateTestResource("path-access", R"({
            "name": "path-access",
            "version": "1.0.0"
        })");

        std::string expectedPath = TestJSResourceHelper::GetTestResourcePath() + "/path-access";
        JSResource resource(expectedPath);

        STREQUALS(resource.GetPath().c_str(), expectedPath.c_str());

        TestJSResourceHelper::Cleanup();
    });

    // ==================== Cleanup ====================

    IT("final cleanup", {
        TestJSResourceHelper::Cleanup();
    });
})
