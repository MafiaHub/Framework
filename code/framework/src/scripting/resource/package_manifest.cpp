/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "package_manifest.h"

#include <logging/logger.h>
#include <utils/vfs.h>

#include <fstream>

namespace Framework::Scripting {

    bool PackageManifest::Parse(const std::string &filepath) {
        // Reset state to avoid stale data from previous parse attempts
        _valid = false;
        _error.clear();
        _name.clear();
        _version.clear();
        _description.clear();
        _author.clear();
        _mafiahubConfig = MafiaHubConfig {};

        std::string contents;
        if (!Utils::Vfs::Get().Read(filepath, contents)) {
            _error = "Failed to open: " + filepath;
            return false;
        }

        try {
            return ParseJson(nlohmann::json::parse(contents));
        } catch (const nlohmann::json::exception &e) {
            _error = "JSON parse error: " + std::string(e.what());
            return false;
        }
    }

    bool PackageManifest::ParseJson(const nlohmann::json &json) {
        // Reset state to avoid stale data from previous parse attempts
        _valid = false;
        _error.clear();
        _name.clear();
        _version.clear();
        _description.clear();
        _author.clear();
        _mafiahubConfig = MafiaHubConfig {};

        // Required fields
        if (!json.contains("name") || !json["name"].is_string()) {
            _error = "Missing required field: name";
            return false;
        }
        _name = json["name"].get<std::string>();
        if (_name.empty()) {
            _error = "Package name cannot be empty";
            return false;
        }

        // Optional standard fields
        _version = json.value("version", "1.0.0");
        _description = json.value("description", "");
        _author = json.value("author", "");

        // MafiaHub config
        if (json.contains("mafiahub") && json["mafiahub"].is_object()) {
            const auto &mh = json["mafiahub"];

            _mafiahubConfig.server = mh.value("server", "");
            _mafiahubConfig.client = mh.value("client", "");

            const auto readList = [&mh](const char *key, std::vector<std::string> &out) {
                if (!mh.contains(key) || !mh[key].is_array()) {
                    return;
                }
                for (const auto &entry : mh[key]) {
                    if (entry.is_string() && !entry.get<std::string>().empty()) {
                        out.push_back(entry.get<std::string>());
                    }
                }
            };

            readList("clientScripts", _mafiahubConfig.clientScripts);
            readList("serverScripts", _mafiahubConfig.serverScripts);
            readList("sharedScripts", _mafiahubConfig.sharedScripts);
            readList("files", _mafiahubConfig.files);

            // Folded in so everything downstream sees only the role model.
            if (!_mafiahubConfig.client.empty()) {
                _mafiahubConfig.clientScripts.insert(_mafiahubConfig.clientScripts.begin(), _mafiahubConfig.client);
            }
            if (!_mafiahubConfig.server.empty()) {
                _mafiahubConfig.serverScripts.insert(_mafiahubConfig.serverScripts.begin(), _mafiahubConfig.server);
            }
            readList("clientFiles", _mafiahubConfig.files);
            _mafiahubConfig.priority = mh.value("priority", 0);
            _mafiahubConfig.errorBehavior = mh.value("errorBehavior", "stop");

            // Exports
            if (mh.contains("exports") && mh["exports"].is_array()) {
                for (const auto &exp : mh["exports"]) {
                    if (exp.is_string()) {
                        _mafiahubConfig.exports.push_back(exp.get<std::string>());
                    }
                }
            }

            // Resource dependencies
            if (mh.contains("resourceDependencies") && mh["resourceDependencies"].is_array()) {
                for (const auto &dep : mh["resourceDependencies"]) {
                    ResourceDependency rd;
                    if (dep.is_string()) {
                        rd.name = dep.get<std::string>();
                        rd.version = "*";
                    } else if (dep.is_object()) {
                        // value() throws type_error.302 on a mismatch, which would drop the whole resource over one mistyped field.
                        const auto str = [&](const char *key, const char *fallback) { const auto it = dep.find(key); return (it != dep.end() && it->is_string()) ? it->get<std::string>() : fallback; };
                        rd.name = str("name", "");
                        rd.version = str("version", "*");
                        const auto optionalIt = dep.find("optional");
                        rd.optional = optionalIt != dep.end() && optionalIt->is_boolean() && optionalIt->get<bool>();
                        if (optionalIt != dep.end() && !optionalIt->is_boolean()) {
                            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Dependency '{}': 'optional' must be a boolean; treating it as required", rd.name);
                        }
                    }
                    if (!rd.name.empty()) {
                        _mafiahubConfig.resourceDependencies.push_back(rd);
                    }
                }
            }
        }

        _valid = true;
        return true;
    }

} // namespace Framework::Scripting
