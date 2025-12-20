#include "resource_manifest.h"

#include <logging/logger.h>

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>
#include <semver.h>

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace Framework::Scripting {

    // ResourceManifest implementation

    bool ResourceManifest::HasExport(const std::string &exportName) const {
        return std::find(exports.begin(), exports.end(), exportName) != exports.end();
    }

    bool ResourceManifest::DependsOn(const std::string &resourceName) const {
        return std::any_of(dependencies.begin(), dependencies.end(), [&resourceName](const ResourceDependency &dep) {
            return dep.name == resourceName;
        });
    }

    std::optional<ResourceDependency> ResourceManifest::GetDependency(const std::string &resourceName) const {
        auto it = std::find_if(dependencies.begin(), dependencies.end(), [&resourceName](const ResourceDependency &dep) {
            return dep.name == resourceName;
        });
        if (it != dependencies.end()) {
            return *it;
        }
        return std::nullopt;
    }

    bool ResourceManifest::IsDependencyOptional(const std::string &resourceName) const {
        auto dep = GetDependency(resourceName);
        return dep.has_value() && dep->optional;
    }

    std::vector<ResourceDependency> ResourceManifest::GetRequiredDependencies() const {
        std::vector<ResourceDependency> required;
        std::copy_if(dependencies.begin(), dependencies.end(), std::back_inserter(required),
            [](const ResourceDependency &dep) { return !dep.optional; });
        return required;
    }

    std::vector<ResourceDependency> ResourceManifest::GetOptionalDependencies() const {
        std::vector<ResourceDependency> optional;
        std::copy_if(dependencies.begin(), dependencies.end(), std::back_inserter(optional),
            [](const ResourceDependency &dep) { return dep.optional; });
        return optional;
    }

    // ResourceManifestParser implementation

    ManifestParseResult ResourceManifestParser::Parse(const std::string &jsonString) {
        try {
            nlohmann::json json = nlohmann::json::parse(jsonString);
            ResourceManifest manifest;
            from_json(json, manifest);

            std::string validationError;
            if (!Validate(manifest, validationError)) {
                return ManifestParseResult::Failure(validationError);
            }

            return ManifestParseResult::Success(std::move(manifest));
        } catch (const nlohmann::json::parse_error &e) {
            return ManifestParseResult::Failure(std::string("JSON parse error: ") + e.what());
        } catch (const nlohmann::json::type_error &e) {
            return ManifestParseResult::Failure(std::string("JSON type error: ") + e.what());
        } catch (const std::exception &e) {
            return ManifestParseResult::Failure(std::string("Error parsing manifest: ") + e.what());
        }
    }

    ManifestParseResult ResourceManifestParser::ParseFile(const std::string &filePath) {
        cppfs::FileHandle file = cppfs::fs::open(filePath);
        if (!file.exists()) {
            return ManifestParseResult::Failure("Manifest file not found: " + filePath);
        }

        if (!file.isFile()) {
            return ManifestParseResult::Failure("Path is not a file: " + filePath);
        }

        std::ifstream stream(filePath);
        if (!stream.is_open()) {
            return ManifestParseResult::Failure("Failed to open manifest file: " + filePath);
        }

        std::stringstream buffer;
        buffer << stream.rdbuf();
        return Parse(buffer.str());
    }

    bool ResourceManifestParser::Validate(const ResourceManifest &manifest, std::string &outError) {
        // Required fields
        if (manifest.name.empty()) {
            outError = "Manifest missing required field: name";
            return false;
        }

        if (manifest.version.empty()) {
            outError = "Manifest missing required field: version";
            return false;
        }

        // Validate resource name (alphanumeric, hyphens, underscores)
        static const std::regex namePattern("^[a-zA-Z][a-zA-Z0-9_-]*$");
        if (!std::regex_match(manifest.name, namePattern)) {
            outError = "Invalid resource name: must start with a letter and contain only alphanumeric characters, hyphens, and underscores";
            return false;
        }

        // Validate version format
        if (!IsValidVersionString(manifest.version)) {
            outError = "Invalid version format: must be semantic versioning (e.g., 1.0.0)";
            return false;
        }

        // Validate dependency version constraints
        for (const auto &dep : manifest.dependencies) {
            if (dep.name.empty()) {
                outError = "Dependency with empty name";
                return false;
            }
            if (!dep.version.empty() && !IsValidVersionConstraint(dep.version)) {
                outError = "Invalid version constraint for dependency '" + dep.name + "': " + dep.version;
                return false;
            }
        }

        // Validate export names
        for (const auto &exportName : manifest.exports) {
            if (exportName.empty()) {
                outError = "Empty export name";
                return false;
            }
        }

        // Check for duplicate exports
        std::vector<std::string> sortedExports = manifest.exports;
        std::sort(sortedExports.begin(), sortedExports.end());
        auto dupIt = std::adjacent_find(sortedExports.begin(), sortedExports.end());
        if (dupIt != sortedExports.end()) {
            outError = "Duplicate export name: " + *dupIt;
            return false;
        }

        // Check for duplicate dependencies
        std::vector<std::string> depNames;
        for (const auto &dep : manifest.dependencies) {
            depNames.push_back(dep.name);
        }
        std::sort(depNames.begin(), depNames.end());
        auto dupDepIt = std::adjacent_find(depNames.begin(), depNames.end());
        if (dupDepIt != depNames.end()) {
            outError = "Duplicate dependency: " + *dupDepIt;
            return false;
        }

        return true;
    }

    std::string ResourceManifestParser::Serialize(const ResourceManifest &manifest) {
        nlohmann::json json;
        to_json(json, manifest);
        return json.dump(2); // Pretty print with 2 spaces
    }

    std::vector<ResourceDependency> ResourceManifestParser::ParseDependencies(const nlohmann::json &json) {
        std::vector<ResourceDependency> dependencies;
        if (!json.is_array()) {
            return dependencies;
        }

        for (const auto &item : json) {
            ResourceDependency dep;
            if (item.is_string()) {
                // Simple string format: just the name
                dep.name = item.get<std::string>();
            } else if (item.is_object()) {
                // Object format with optional version
                from_json(item, dep);
            }
            dependencies.push_back(dep);
        }

        return dependencies;
    }

    bool ResourceManifestParser::IsValidVersionString(const std::string &version) {
        return semver_is_valid(version.c_str()) != 0;
    }

    bool ResourceManifestParser::IsValidVersionConstraint(const std::string &constraint) {
        if (constraint.empty()) {
            return true; // Empty constraint means "any version"
        }

        // Version constraint formats: ">=1.0.0", "^1.0.0", "~1.0.0", "1.0.0", etc.
        // Strip the operator prefix and validate the version part
        std::string versionPart = constraint;
        size_t pos              = 0;

        // Skip operator characters at the beginning
        while (pos < constraint.size() && (constraint[pos] == '<' || constraint[pos] == '>' || constraint[pos] == '=' || constraint[pos] == '~' || constraint[pos] == '^')) {
            pos++;
        }

        // Operator can be at most 2 characters (e.g., >=, <=)
        if (pos > 2) {
            return false;
        }

        versionPart = constraint.substr(pos);
        if (versionPart.empty()) {
            return false;
        }

        return semver_is_valid(versionPart.c_str()) != 0;
    }

    // JSON serialization

    void to_json(nlohmann::json &j, const ResourceDependency &dep) {
        j = nlohmann::json {{"name", dep.name}};
        if (!dep.version.empty()) {
            j["version"] = dep.version;
        }
        if (dep.optional) {
            j["optional"] = dep.optional;
        }
    }

    void from_json(const nlohmann::json &j, ResourceDependency &dep) {
        j.at("name").get_to(dep.name);
        if (j.contains("version")) {
            j.at("version").get_to(dep.version);
        }
        if (j.contains("optional")) {
            j.at("optional").get_to(dep.optional);
        }
    }

    void to_json(nlohmann::json &j, const ResourceManifest &manifest) {
        j = nlohmann::json {{"name", manifest.name}, {"version", manifest.version}};

        if (!manifest.author.empty()) {
            j["author"] = manifest.author;
        }
        if (!manifest.description.empty()) {
            j["description"] = manifest.description;
        }
        if (!manifest.dependencies.empty()) {
            j["dependencies"] = manifest.dependencies;
        }
        if (!manifest.exports.empty()) {
            j["exports"] = manifest.exports;
        }
        if (manifest.priority != 0) {
            j["priority"] = manifest.priority;
        }
        if (!manifest.serverFiles.empty()) {
            j["server_files"] = manifest.serverFiles;
        }
        if (!manifest.clientFiles.empty()) {
            j["client_files"] = manifest.clientFiles;
        }

        // Error behavior serialization (only if not default)
        if (manifest.errorBehavior != ResourceErrorBehavior::Stop) {
            switch (manifest.errorBehavior) {
            case ResourceErrorBehavior::Continue:
                j["error_behavior"] = "continue";
                break;
            case ResourceErrorBehavior::Restart:
                j["error_behavior"] = "restart";
                break;
            default:
                break;
            }
        }

        // Auto-restart config serialization (only if enabled or non-default values)
        if (manifest.autoRestart.enabled || manifest.autoRestart.maxAttempts != 3 || manifest.autoRestart.timeWindowSeconds != 60 || manifest.autoRestart.backoffBaseMilliseconds != 1000) {
            nlohmann::json ar;
            ar["enabled"]            = manifest.autoRestart.enabled;
            ar["max_attempts"]       = manifest.autoRestart.maxAttempts;
            ar["time_window_seconds"] = manifest.autoRestart.timeWindowSeconds;
            ar["backoff_base_ms"]    = manifest.autoRestart.backoffBaseMilliseconds;
            j["auto_restart"]        = ar;
        }
    }

    void from_json(const nlohmann::json &j, ResourceManifest &manifest) {
        j.at("name").get_to(manifest.name);
        j.at("version").get_to(manifest.version);

        if (j.contains("author")) {
            j.at("author").get_to(manifest.author);
        }
        if (j.contains("description")) {
            j.at("description").get_to(manifest.description);
        }
        if (j.contains("dependencies")) {
            manifest.dependencies = ResourceManifestParser::ParseDependencies(j.at("dependencies"));
        }
        if (j.contains("exports")) {
            j.at("exports").get_to(manifest.exports);
        }
        if (j.contains("priority")) {
            j.at("priority").get_to(manifest.priority);
        }
        if (j.contains("server_files")) {
            j.at("server_files").get_to(manifest.serverFiles);
        }
        if (j.contains("client_files")) {
            j.at("client_files").get_to(manifest.clientFiles);
        }

        // Error behavior parsing
        if (j.contains("error_behavior")) {
            std::string behavior = j.at("error_behavior").get<std::string>();
            if (behavior == "continue") {
                manifest.errorBehavior = ResourceErrorBehavior::Continue;
            } else if (behavior == "restart") {
                manifest.errorBehavior = ResourceErrorBehavior::Restart;
            } else {
                manifest.errorBehavior = ResourceErrorBehavior::Stop;
            }
        }

        // Auto-restart config parsing
        if (j.contains("auto_restart")) {
            const auto &ar               = j.at("auto_restart");
            manifest.autoRestart.enabled = ar.value("enabled", false);
            manifest.autoRestart.maxAttempts = ar.value("max_attempts", 3);
            manifest.autoRestart.timeWindowSeconds = ar.value("time_window_seconds", 60);
            manifest.autoRestart.backoffBaseMilliseconds = ar.value("backoff_base_ms", 1000);
        }
    }

} // namespace Framework::Scripting
