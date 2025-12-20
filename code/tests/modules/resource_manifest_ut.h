/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/resource/resource_manifest.h"

MODULE(resource_manifest, {
    using namespace Framework::Scripting;

    // ==================== Valid Manifest Parsing ====================

    IT("parses minimal valid manifest", {
        const char *json = R"({
            "name": "test-resource",
            "version": "1.0.0"
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);
        STREQUALS(result.manifest.name.c_str(), "test-resource");
        STREQUALS(result.manifest.version.c_str(), "1.0.0");
    });

    IT("parses full manifest with all fields", {
        const char *json = R"({
            "name": "my-resource",
            "version": "2.1.0",
            "author": "Test Author",
            "description": "A test resource",
            "priority": 10,
            "server_files": ["server/main.lua", "server/utils.lua"],
            "client_files": ["client/main.lua"],
            "exports": ["getData", "setConfig"],
            "dependencies": [
                {"name": "core", "version": ">=1.0.0"},
                {"name": "utils", "optional": true}
            ]
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);
        STREQUALS(result.manifest.name.c_str(), "my-resource");
        STREQUALS(result.manifest.version.c_str(), "2.1.0");
        STREQUALS(result.manifest.author.c_str(), "Test Author");
        STREQUALS(result.manifest.description.c_str(), "A test resource");
        EQUALS(result.manifest.priority, 10);
        EQUALS(result.manifest.serverFiles.size(), 2u);
        EQUALS(result.manifest.clientFiles.size(), 1u);
        EQUALS(result.manifest.exports.size(), 2u);
        EQUALS(result.manifest.dependencies.size(), 2u);
    });

    IT("parses error behavior settings", {
        const char *jsonContinue = R"({
            "name": "test",
            "version": "1.0.0",
            "error_behavior": "continue"
        })";

        auto result = ResourceManifestParser::Parse(jsonContinue);
        EQUALS(result.success, true);
        EQUALS(static_cast<int>(result.manifest.errorBehavior), static_cast<int>(ResourceErrorBehavior::Continue));

        const char *jsonRestart = R"({
            "name": "test",
            "version": "1.0.0",
            "error_behavior": "restart"
        })";

        result = ResourceManifestParser::Parse(jsonRestart);
        EQUALS(result.success, true);
        EQUALS(static_cast<int>(result.manifest.errorBehavior), static_cast<int>(ResourceErrorBehavior::Restart));

        const char *jsonStop = R"({
            "name": "test",
            "version": "1.0.0",
            "error_behavior": "stop"
        })";

        result = ResourceManifestParser::Parse(jsonStop);
        EQUALS(result.success, true);
        EQUALS(static_cast<int>(result.manifest.errorBehavior), static_cast<int>(ResourceErrorBehavior::Stop));
    });

    IT("parses auto-restart configuration", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "auto_restart": {
                "enabled": true,
                "max_attempts": 5,
                "time_window_seconds": 120,
                "backoff_base_ms": 2000
            }
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);
        EQUALS(result.manifest.autoRestart.enabled, true);
        EQUALS(result.manifest.autoRestart.maxAttempts, 5);
        EQUALS(result.manifest.autoRestart.timeWindowSeconds, 120);
        EQUALS(result.manifest.autoRestart.backoffBaseMilliseconds, 2000);
    });

    // ==================== Invalid JSON ====================

    IT("fails on invalid JSON syntax", {
        const char *json = R"({ "name": "test", "version": })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, false);
        NEQUALS(result.error.find("parse error"), std::string::npos);
    });

    IT("fails on non-object JSON", {
        const char *json = R"(["not", "an", "object"])";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, false);
    });

    // ==================== Missing Required Fields ====================

    IT("fails when name is missing", {
        const char *json = R"({
            "version": "1.0.0"
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, false);
    });

    IT("fails when version is missing", {
        const char *json = R"({
            "name": "test"
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, false);
        NEQUALS(result.error.find("version"), std::string::npos);
    });

    // ==================== Invalid Resource Name ====================

    IT("fails on resource name starting with number", {
        const char *json = R"({
            "name": "123resource",
            "version": "1.0.0"
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, false);
        NEQUALS(result.error.find("Invalid resource name"), std::string::npos);
    });

    IT("fails on resource name with invalid characters", {
        const char *json = R"({
            "name": "my.resource",
            "version": "1.0.0"
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, false);
    });

    IT("accepts valid resource names", {
        // Underscores allowed
        auto result = ResourceManifestParser::Parse(R"({"name": "my_resource", "version": "1.0.0"})");
        EQUALS(result.success, true);

        // Hyphens allowed
        result = ResourceManifestParser::Parse(R"({"name": "my-resource", "version": "1.0.0"})");
        EQUALS(result.success, true);

        // Numbers in the middle allowed
        result = ResourceManifestParser::Parse(R"({"name": "resource123", "version": "1.0.0"})");
        EQUALS(result.success, true);

        // Mixed case allowed
        result = ResourceManifestParser::Parse(R"({"name": "MyResource", "version": "1.0.0"})");
        EQUALS(result.success, true);
    });

    // ==================== Invalid Version Format ====================

    IT("fails on empty version string", {
        const char *json = R"({
            "name": "test",
            "version": ""
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, false);
        NEQUALS(result.error.find("version"), std::string::npos);
    });

    IT("accepts valid semantic versions", {
        auto result = ResourceManifestParser::Parse(R"({"name": "test", "version": "1.0.0"})");
        EQUALS(result.success, true);

        result = ResourceManifestParser::Parse(R"({"name": "test", "version": "0.1.0"})");
        EQUALS(result.success, true);

        result = ResourceManifestParser::Parse(R"({"name": "test", "version": "10.20.30"})");
        EQUALS(result.success, true);

        result = ResourceManifestParser::Parse(R"({"name": "test", "version": "1.0.0-alpha"})");
        EQUALS(result.success, true);

        result = ResourceManifestParser::Parse(R"({"name": "test", "version": "1.0.0-beta.1"})");
        EQUALS(result.success, true);
    });

    // ==================== Dependencies Parsing ====================

    IT("parses dependencies in simple string format", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "dependencies": ["core", "utils", "network"]
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);
        EQUALS(result.manifest.dependencies.size(), 3u);
        STREQUALS(result.manifest.dependencies[0].name.c_str(), "core");
        STREQUALS(result.manifest.dependencies[1].name.c_str(), "utils");
        STREQUALS(result.manifest.dependencies[2].name.c_str(), "network");
    });

    IT("parses dependencies in object format with version", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "dependencies": [
                {"name": "core", "version": ">=1.0.0"},
                {"name": "utils", "version": "^2.0.0"}
            ]
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);
        EQUALS(result.manifest.dependencies.size(), 2u);
        STREQUALS(result.manifest.dependencies[0].name.c_str(), "core");
        STREQUALS(result.manifest.dependencies[0].version.c_str(), ">=1.0.0");
        STREQUALS(result.manifest.dependencies[1].name.c_str(), "utils");
        STREQUALS(result.manifest.dependencies[1].version.c_str(), "^2.0.0");
    });

    IT("parses optional dependencies", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "dependencies": [
                {"name": "required-dep"},
                {"name": "optional-dep", "optional": true}
            ]
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);
        EQUALS(result.manifest.dependencies[0].optional, false);
        EQUALS(result.manifest.dependencies[1].optional, true);
    });

    IT("parses mixed dependency formats", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "dependencies": [
                "simple-dep",
                {"name": "object-dep", "version": ">=1.0.0"}
            ]
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);
        EQUALS(result.manifest.dependencies.size(), 2u);
        STREQUALS(result.manifest.dependencies[0].name.c_str(), "simple-dep");
        STREQUALS(result.manifest.dependencies[1].name.c_str(), "object-dep");
    });

    // ==================== Invalid Version Constraints ====================

    IT("fails on invalid version constraint", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "dependencies": [
                {"name": "core", "version": ">>>invalid"}
            ]
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, false);
        NEQUALS(result.error.find("Invalid version constraint"), std::string::npos);
    });

    IT("accepts valid version constraints", {
        auto result = ResourceManifestParser::Parse(R"({
            "name": "test", "version": "1.0.0",
            "dependencies": [{"name": "a", "version": ">=1.0.0"}]
        })");
        EQUALS(result.success, true);

        result = ResourceManifestParser::Parse(R"({
            "name": "test", "version": "1.0.0",
            "dependencies": [{"name": "a", "version": "^1.0.0"}]
        })");
        EQUALS(result.success, true);

        result = ResourceManifestParser::Parse(R"({
            "name": "test", "version": "1.0.0",
            "dependencies": [{"name": "a", "version": "~1.0.0"}]
        })");
        EQUALS(result.success, true);

        result = ResourceManifestParser::Parse(R"({
            "name": "test", "version": "1.0.0",
            "dependencies": [{"name": "a", "version": "<=2.0.0"}]
        })");
        EQUALS(result.success, true);
    });

    // ==================== Duplicate Detection ====================

    IT("fails on duplicate exports", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "exports": ["getData", "setConfig", "getData"]
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, false);
        NEQUALS(result.error.find("Duplicate export"), std::string::npos);
    });

    IT("fails on duplicate dependencies", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "dependencies": [
                {"name": "core"},
                {"name": "utils"},
                {"name": "core"}
            ]
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, false);
        NEQUALS(result.error.find("Duplicate dependency"), std::string::npos);
    });

    // ==================== ResourceManifest Methods ====================

    IT("HasExport returns correct result", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "exports": ["getData", "setConfig"]
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);
        EQUALS(result.manifest.HasExport("getData"), true);
        EQUALS(result.manifest.HasExport("setConfig"), true);
        EQUALS(result.manifest.HasExport("nonexistent"), false);
    });

    IT("DependsOn returns correct result", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "dependencies": [{"name": "core"}, {"name": "utils"}]
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);
        EQUALS(result.manifest.DependsOn("core"), true);
        EQUALS(result.manifest.DependsOn("utils"), true);
        EQUALS(result.manifest.DependsOn("network"), false);
    });

    IT("GetDependency returns correct dependency", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "dependencies": [{"name": "core", "version": ">=1.0.0"}]
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);

        auto dep = result.manifest.GetDependency("core");
        EQUALS(dep.has_value(), true);
        STREQUALS(dep->name.c_str(), "core");
        STREQUALS(dep->version.c_str(), ">=1.0.0");

        auto missing = result.manifest.GetDependency("nonexistent");
        EQUALS(missing.has_value(), false);
    });

    IT("GetRequiredDependencies filters correctly", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "dependencies": [
                {"name": "required1"},
                {"name": "optional1", "optional": true},
                {"name": "required2"},
                {"name": "optional2", "optional": true}
            ]
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);

        auto required = result.manifest.GetRequiredDependencies();
        EQUALS(required.size(), 2u);

        auto optional = result.manifest.GetOptionalDependencies();
        EQUALS(optional.size(), 2u);
    });

    // ==================== Serialization ====================

    IT("round-trips through serialize and parse", {
        ResourceManifest original;
        original.name        = "roundtrip-test";
        original.version     = "3.2.1";
        original.author      = "Tester";
        original.description = "A test manifest";
        original.priority    = 5;
        original.serverFiles = {"server.lua"};
        original.clientFiles = {"client.lua"};
        original.exports     = {"exportA", "exportB"};

        ResourceDependency dep;
        dep.name    = "core";
        dep.version = ">=1.0.0";
        original.dependencies.push_back(dep);

        std::string serialized = ResourceManifestParser::Serialize(original);
        auto result            = ResourceManifestParser::Parse(serialized);

        EQUALS(result.success, true);
        STREQUALS(result.manifest.name.c_str(), original.name.c_str());
        STREQUALS(result.manifest.version.c_str(), original.version.c_str());
        STREQUALS(result.manifest.author.c_str(), original.author.c_str());
        STREQUALS(result.manifest.description.c_str(), original.description.c_str());
        EQUALS(result.manifest.priority, original.priority);
        EQUALS(result.manifest.serverFiles.size(), original.serverFiles.size());
        EQUALS(result.manifest.clientFiles.size(), original.clientFiles.size());
        EQUALS(result.manifest.exports.size(), original.exports.size());
        EQUALS(result.manifest.dependencies.size(), original.dependencies.size());
    });

    // ==================== Edge Cases ====================

    IT("handles empty optional arrays", {
        const char *json = R"({
            "name": "test",
            "version": "1.0.0",
            "dependencies": [],
            "exports": [],
            "server_files": [],
            "client_files": []
        })";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);
        EQUALS(result.manifest.dependencies.empty(), true);
        EQUALS(result.manifest.exports.empty(), true);
        EQUALS(result.manifest.serverFiles.empty(), true);
        EQUALS(result.manifest.clientFiles.empty(), true);
    });

    IT("handles whitespace in JSON", {
        const char *json = R"(
            {
                "name"    :    "test"   ,
                "version" :    "1.0.0"
            }
        )";

        auto result = ResourceManifestParser::Parse(json);
        EQUALS(result.success, true);
        STREQUALS(result.manifest.name.c_str(), "test");
    });
})
