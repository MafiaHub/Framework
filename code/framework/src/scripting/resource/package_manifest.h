#pragma once

#include <string>
#include <vector>
#include <optional>

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
        std::vector<ResourceDependency> resourceDependencies;
        std::vector<std::string> exports;
        int priority = 0;
        std::string errorBehavior = "stop";              // stop, restart, continue
    };

    /**
     * Module type for Node.js compatibility.
     * Determines whether to use require() (CommonJS) or import() (ES Modules).
     */
    enum class ModuleType {
        CommonJS,  // Default, uses require()
        ESModule   // Uses import(), supports import/export syntax
    };

    /**
     * Parses package.json files for JS resources.
     */
    class PackageManifest {
      public:
        bool Parse(const std::string &filepath);
        bool ParseJson(const nlohmann::json &json);

        const std::string &GetName() const { return _name; }
        const std::string &GetVersion() const { return _version; }
        const std::string &GetDescription() const { return _description; }
        const std::string &GetAuthor() const { return _author; }
        const MafiaHubConfig &GetMafiaHubConfig() const { return _mafiahubConfig; }
        const std::string &GetError() const { return _error; }
        ModuleType GetModuleType() const { return _moduleType; }
        bool IsESModule() const { return _moduleType == ModuleType::ESModule; }

        bool IsValid() const { return _valid; }

      private:
        bool _valid = false;
        std::string _error;

        std::string _name;
        std::string _version;
        std::string _description;
        std::string _author;
        ModuleType _moduleType = ModuleType::CommonJS;
        MafiaHubConfig _mafiahubConfig;
    };

} // namespace Framework::Scripting
