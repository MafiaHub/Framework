/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Framework::Scripting {

    struct ResourceDependency {
        std::string name;
        std::string version;
        bool optional = false;
    };

    struct MafiaHubConfig {
        std::string server;                              // Server entry point
        std::string client;                              // Client entry point
        // Globs, relative to the resource root, for files shipped to clients alongside the entry
        // point. Empty falls back to scanning the entry's directory.
        std::vector<std::string> clientFiles;
        std::vector<ResourceDependency> resourceDependencies;
        std::vector<std::string> exports;
        int priority = 0;
        std::string errorBehavior = "stop";              // stop, restart, continue
    };

    /**
     * Parses package.json files for JS resources.
     */
    class PackageManifest final {
      public:
        bool Parse(const std::string &filepath);
        bool ParseJson(const nlohmann::json &json);

        const std::string &GetName() const { return _name; }
        const std::string &GetVersion() const { return _version; }
        const std::string &GetDescription() const { return _description; }
        const std::string &GetAuthor() const { return _author; }
        const MafiaHubConfig &GetMafiaHubConfig() const { return _mafiahubConfig; }
        const std::string &GetError() const { return _error; }

        bool IsValid() const { return _valid; }

      private:
        bool _valid = false;
        std::string _error;

        std::string _name;
        std::string _version;
        std::string _description;
        std::string _author;
        MafiaHubConfig _mafiahubConfig;
    };

} // namespace Framework::Scripting
