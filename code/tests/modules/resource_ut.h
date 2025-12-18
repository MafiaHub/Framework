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

#include <fstream>

// Helper class to manage test resource directories
class TestResourceHelper {
  public:
    static std::string GetTestResourcePath() {
        return "/tmp/framework_test_resources";
    }

    static void CreateTestResource(const std::string &name, const std::string &manifestJson) {
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

        // Write manifest
        std::string manifestPath = resourcePath + "/manifest.json";
        std::ofstream manifestFile(manifestPath);
        manifestFile << manifestJson;
        manifestFile.close();
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

    IT("loads valid manifest from directory", {
        TestResourceHelper::CreateTestResource("manifest-test-1", R"({
            "name": "manifest-test-1",
            "version": "2.0.0",
            "author": "Test Author",
            "description": "A test resource"
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/manifest-test-1");

        EQUALS(resource.IsManifestValid(), true);
        STREQUALS(resource.GetName().c_str(), "manifest-test-1");
        STREQUALS(resource.GetVersion().c_str(), "2.0.0");
        STREQUALS(resource.GetAuthor().c_str(), "Test Author");
        STREQUALS(resource.GetDescription().c_str(), "A test resource");
        EQUALS(resource.GetState(), ResourceState::Unloaded);

        TestResourceHelper::Cleanup();
    });

    IT("enters error state for invalid manifest name", {
        TestResourceHelper::CreateTestResource("manifest-test-2", R"({
            "name": "123invalid",
            "version": "1.0.0"
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/manifest-test-2");

        EQUALS(resource.IsManifestValid(), false);
        EQUALS(resource.GetState(), ResourceState::Error);
        EQUALS(resource.GetErrorMessage().empty(), false);

        TestResourceHelper::Cleanup();
    });

    IT("enters error state for missing manifest", {
        // Create directory without manifest
        std::string basePath = TestResourceHelper::GetTestResourcePath();
        std::string resourcePath = basePath + "/no-manifest";

        cppfs::FileHandle baseDir = cppfs::fs::open(basePath);
        if (!baseDir.exists()) {
            baseDir.createDirectory();
        }

        cppfs::FileHandle resourceDir = cppfs::fs::open(resourcePath);
        if (!resourceDir.exists()) {
            resourceDir.createDirectory();
        }

        Resource resource(resourcePath);

        EQUALS(resource.IsManifestValid(), false);
        EQUALS(resource.GetState(), ResourceState::Error);

        TestResourceHelper::Cleanup();
    });

    IT("enters error state for malformed JSON manifest", {
        TestResourceHelper::CreateTestResource("manifest-test-3", R"({
            "name": "test"
            "version": "1.0.0"
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/manifest-test-3");

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
        Resource resource(TestResourceHelper::GetTestResourcePath() + "/path-test/");

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

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/state-test-1");

        EQUALS(resource.GetState(), ResourceState::Unloaded);
        EQUALS(resource.IsRunning(), false);
        EQUALS(resource.IsStopped(), true); // Unloaded counts as stopped
        EQUALS(resource.HasError(), false);

        TestResourceHelper::Cleanup();
    });

    IT("starts in Error state for invalid manifest", {
        TestResourceHelper::CreateTestResource("state-test-2", R"({
            "version": "1.0.0"
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/state-test-2");

        EQUALS(resource.GetState(), ResourceState::Error);
        EQUALS(resource.IsRunning(), false);
        EQUALS(resource.IsStopped(), false);
        EQUALS(resource.HasError(), true);

        TestResourceHelper::Cleanup();
    });

    // ==================== Script Paths ====================

    IT("resolves server and client script paths", {
        TestResourceHelper::CreateTestResource("scripts-test", R"({
            "name": "scripts-test",
            "version": "1.0.0",
            "server_files": ["server/main.lua", "server/utils.lua"],
            "client_files": ["client/main.lua"]
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/scripts-test");

        auto serverPaths = resource.GetServerScriptPaths();
        auto clientPaths = resource.GetClientScriptPaths();

        EQUALS(serverPaths.size(), 2u);
        EQUALS(clientPaths.size(), 1u);
        EQUALS(resource.GetScriptCount(), 3u);

        // Check full paths
        std::string expectedServerPath = TestResourceHelper::GetTestResourcePath() + "/scripts-test/server/main.lua";
        STREQUALS(serverPaths[0].c_str(), expectedServerPath.c_str());

        TestResourceHelper::Cleanup();
    });

    IT("handles empty script lists", {
        TestResourceHelper::CreateTestResource("no-scripts", R"({
            "name": "no-scripts",
            "version": "1.0.0"
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/no-scripts");

        EQUALS(resource.GetServerScriptPaths().size(), 0u);
        EQUALS(resource.GetClientScriptPaths().size(), 0u);
        EQUALS(resource.GetScriptCount(), 0u);

        TestResourceHelper::Cleanup();
    });

    // ==================== Manifest Methods ====================

    IT("HasExport delegates to manifest", {
        TestResourceHelper::CreateTestResource("export-test", R"({
            "name": "export-test",
            "version": "1.0.0",
            "exports": ["getData", "setConfig"]
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/export-test");

        EQUALS(resource.HasExport("getData"), true);
        EQUALS(resource.HasExport("setConfig"), true);
        EQUALS(resource.HasExport("nonexistent"), false);

        TestResourceHelper::Cleanup();
    });

    IT("DependsOn delegates to manifest", {
        TestResourceHelper::CreateTestResource("depends-test", R"({
            "name": "depends-test",
            "version": "1.0.0",
            "dependencies": [{"name": "core"}, {"name": "utils"}]
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/depends-test");

        EQUALS(resource.DependsOn("core"), true);
        EQUALS(resource.DependsOn("utils"), true);
        EQUALS(resource.DependsOn("network"), false);

        TestResourceHelper::Cleanup();
    });

    IT("GetManifest returns full manifest", {
        TestResourceHelper::CreateTestResource("manifest-access", R"({
            "name": "manifest-access",
            "version": "3.0.0",
            "priority": 10,
            "exports": ["api"]
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/manifest-access");

        const auto &manifest = resource.GetManifest();
        STREQUALS(manifest.name.c_str(), "manifest-access");
        STREQUALS(manifest.version.c_str(), "3.0.0");
        EQUALS(manifest.priority, 10);
        EQUALS(manifest.exports.size(), 1u);

        TestResourceHelper::Cleanup();
    });

    // ==================== Restart Tracking ====================

    IT("tracks restart attempts correctly", {
        TestResourceHelper::CreateTestResource("restart-test-1", R"({
            "name": "restart-test-1",
            "version": "1.0.0",
            "auto_restart": {
                "enabled": true,
                "max_attempts": 3,
                "time_window_seconds": 60
            }
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/restart-test-1");

        EQUALS(resource.GetRestartAttemptCount(), 0);
        EQUALS(resource.CanAutoRestart(), true);

        resource.RecordRestartAttempt();
        EQUALS(resource.GetRestartAttemptCount(), 1);
        EQUALS(resource.CanAutoRestart(), true);

        resource.RecordRestartAttempt();
        resource.RecordRestartAttempt();
        EQUALS(resource.GetRestartAttemptCount(), 3);
        EQUALS(resource.CanAutoRestart(), false);

        TestResourceHelper::Cleanup();
    });

    IT("ClearRestartAttempts resets counter", {
        TestResourceHelper::CreateTestResource("restart-test-2", R"({
            "name": "restart-test-2",
            "version": "1.0.0",
            "auto_restart": {
                "enabled": true,
                "max_attempts": 3
            }
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/restart-test-2");

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
            "version": "1.0.0",
            "auto_restart": {
                "enabled": true,
                "backoff_base_ms": 1000
            }
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/backoff-test");

        EQUALS(resource.GetRestartBackoffMs(), 0);

        resource.RecordRestartAttempt();
        EQUALS(resource.GetRestartBackoffMs(), 1000); // 1000 * 2^0

        resource.RecordRestartAttempt();
        EQUALS(resource.GetRestartBackoffMs(), 2000); // 1000 * 2^1

        resource.RecordRestartAttempt();
        EQUALS(resource.GetRestartBackoffMs(), 4000); // 1000 * 2^2

        TestResourceHelper::Cleanup();
    });

    IT("CanAutoRestart returns false when disabled", {
        TestResourceHelper::CreateTestResource("restart-disabled", R"({
            "name": "restart-disabled",
            "version": "1.0.0"
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/restart-disabled");

        // auto_restart not enabled by default
        EQUALS(resource.CanAutoRestart(), false);

        TestResourceHelper::Cleanup();
    });

    IT("GetRestartAttemptCount returns 0 when auto-restart disabled", {
        TestResourceHelper::CreateTestResource("restart-disabled-count", R"({
            "name": "restart-disabled-count",
            "version": "1.0.0",
            "auto_restart": {
                "enabled": false
            }
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/restart-disabled-count");

        // Record attempts (these should still work)
        resource.RecordRestartAttempt();
        resource.RecordRestartAttempt();

        // But count returns 0 when disabled
        EQUALS(resource.GetRestartAttemptCount(), 0);

        TestResourceHelper::Cleanup();
    });

    // ==================== Content Hash ====================

    IT("calculates content hash", {
        TestResourceHelper::CreateTestResource("hash-test", R"({
            "name": "hash-test",
            "version": "1.0.0",
            "server_files": ["main.lua"]
        })");
        TestResourceHelper::CreateTestScript("hash-test", "main.lua", "print('hello')");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/hash-test");

        uint32_t hash1 = resource.GetContentHash();
        NEQUALS(hash1, 0u);

        // Same content should give same hash
        uint32_t hash2 = resource.GetContentHash();
        EQUALS(hash1, hash2);

        TestResourceHelper::Cleanup();
    });

    IT("can invalidate content hash", {
        TestResourceHelper::CreateTestResource("hash-invalidate", R"({
            "name": "hash-invalidate",
            "version": "1.0.0"
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/hash-invalidate");

        uint32_t hash1 = resource.GetContentHash();

        resource.InvalidateContentHash();

        // Hash should be recalculated
        uint32_t hash2 = resource.GetContentHash();

        // Same content, same hash after invalidation and recalculation
        EQUALS(hash1, hash2);

        TestResourceHelper::Cleanup();
    });

    // ==================== Environment ====================

    IT("environment is initially null", {
        TestResourceHelper::CreateTestResource("env-test", R"({
            "name": "env-test",
            "version": "1.0.0"
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/env-test");

        EQUALS(resource.GetEnvironment(), nullptr);

        TestResourceHelper::Cleanup();
    });

    // ==================== Health Check ====================

    IT("health check is initially not registered", {
        TestResourceHelper::CreateTestResource("health-test", R"({
            "name": "health-test",
            "version": "1.0.0"
        })");

        Resource resource(TestResourceHelper::GetTestResourcePath() + "/health-test");

        EQUALS(resource.HasHealthCheck(), false);
        // No health check means healthy by default
        EQUALS(resource.CheckHealth(), true);
        EQUALS(resource.GetLastHealthCheckResult(), true);

        TestResourceHelper::Cleanup();
    });

    // ==================== Move Semantics ====================

    IT("supports move construction", {
        TestResourceHelper::CreateTestResource("move-test-1", R"({
            "name": "move-test-1",
            "version": "1.0.0"
        })");

        Resource original(TestResourceHelper::GetTestResourcePath() + "/move-test-1");
        original.RecordRestartAttempt();

        Resource moved(std::move(original));

        STREQUALS(moved.GetName().c_str(), "move-test-1");
        EQUALS(moved.GetState(), ResourceState::Unloaded);
        EQUALS(moved.IsManifestValid(), true);

        // Original should be in moved-from state
        EQUALS(original.IsManifestValid(), false);
        EQUALS(original.GetState(), ResourceState::Unloaded);

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

        Resource original(TestResourceHelper::GetTestResourcePath() + "/move-test-2a");
        Resource target(TestResourceHelper::GetTestResourcePath() + "/move-test-2b");

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
        Resource resource(expectedPath);

        STREQUALS(resource.GetPath().c_str(), expectedPath.c_str());

        TestResourceHelper::Cleanup();
    });

    // ==================== Cleanup ====================

    IT("final cleanup", {
        TestResourceHelper::Cleanup();
    });
})
