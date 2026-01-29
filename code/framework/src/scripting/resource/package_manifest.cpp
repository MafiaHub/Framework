#include "package_manifest.h"

#include <fstream>

namespace Framework::Scripting {

    bool PackageManifest::Parse(const std::string &filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            _error = "Failed to open: " + filepath;
            return false;
        }

        try {
            nlohmann::json json;
            file >> json;
            return ParseJson(json);
        } catch (const nlohmann::json::exception &e) {
            _error = "JSON parse error: " + std::string(e.what());
            return false;
        }
    }

    bool PackageManifest::ParseJson(const nlohmann::json &json) {
        // Required fields
        if (!json.contains("name") || !json["name"].is_string()) {
            _error = "Missing required field: name";
            return false;
        }
        _name = json["name"].get<std::string>();

        // Optional standard fields
        _version = json.value("version", "1.0.0");
        _description = json.value("description", "");
        _author = json.value("author", "");

        // Module type (Node.js standard: "type": "module" for ES modules)
        std::string typeStr = json.value("type", "commonjs");
        _moduleType = (typeStr == "module") ? ModuleType::ESModule : ModuleType::CommonJS;

        // MafiaHub config
        if (json.contains("mafiahub") && json["mafiahub"].is_object()) {
            const auto &mh = json["mafiahub"];

            _mafiahubConfig.server = mh.value("server", "");
            _mafiahubConfig.client = mh.value("client", "");
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
                        rd.name = dep.value("name", "");
                        rd.version = dep.value("version", "*");
                        rd.optional = dep.value("optional", false);
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
