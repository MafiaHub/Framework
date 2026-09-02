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

    // Scripts are declared by role and what ships is derived from it, so server scripts are
    // excluded by construction rather than by a filter that could forget one. Paths are relative
    // to the resource root; only |files| accepts globs, so execution order is exactly as written.
    struct MafiaHubConfig {
        std::vector<std::string> clientScripts; // executed on the client, shipped
        std::vector<std::string> serverScripts; // executed on the server, never shipped
        std::vector<std::string> sharedScripts; // executed on both, shipped
        // Shipped to clients but not executed: web-view pages, styles, fonts. Globs allowed.
        // Empty falls back to scanning the client scripts' directory.
        std::vector<std::string> files;

        // Superseded by the lists above; folded into them at parse time so existing manifests
        // keep working.
        std::string server;
        std::string client;

        std::vector<std::string> GetClientExecutionList() const {
            std::vector<std::string> out(sharedScripts);
            out.insert(out.end(), clientScripts.begin(), clientScripts.end());
            return out;
        }

        std::vector<std::string> GetServerExecutionList() const {
            std::vector<std::string> out(sharedScripts);
            out.insert(out.end(), serverScripts.begin(), serverScripts.end());
            return out;
        }

        // Scripts a client receives; |files| is separate because it needs glob expansion.
        std::vector<std::string> GetShippedPaths() const {
            std::vector<std::string> out(clientScripts);
            out.insert(out.end(), sharedScripts.begin(), sharedScripts.end());
            return out;
        }

        bool HasClientContent() const { return !clientScripts.empty() || !sharedScripts.empty(); }

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
