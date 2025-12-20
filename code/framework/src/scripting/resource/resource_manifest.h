#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace Framework::Scripting {

    /**
     * Represents a dependency on another resource with version constraints.
     */
    struct ResourceDependency {
        std::string name;
        std::string version; // Semantic version constraint (e.g., ">=1.0.0")
        bool optional = false; // If true, resource can start without this dependency

        bool operator==(const ResourceDependency &other) const {
            return name == other.name && version == other.version && optional == other.optional;
        }
    };

    /**
     * Error behavior when a resource encounters an error.
     */
    enum class ResourceErrorBehavior {
        Continue, // Log error, keep resource running
        Stop,     // Stop the resource, others continue
        Restart   // Automatically restart the resource
    };

    /**
     * Configuration for auto-restart policy.
     */
    struct ResourceAutoRestartConfig {
        bool enabled                = false;
        int maxAttempts             = 3;    // Maximum restart attempts within time window
        int timeWindowSeconds       = 60;   // Time window for counting attempts
        int backoffBaseMilliseconds = 1000; // Base delay for exponential backoff
    };

    /**
     * Resource manifest containing all metadata and configuration for a resource.
     */
    struct ResourceManifest {
        // Identity
        std::string name;
        std::string version;
        std::string author;
        std::string description;

        // Dependencies and exports
        std::vector<ResourceDependency> dependencies;
        std::vector<std::string> exports;

        // Load order
        int priority = 0;

        // Script files
        std::vector<std::string> serverFiles;
        std::vector<std::string> clientFiles;

        ResourceErrorBehavior errorBehavior = ResourceErrorBehavior::Stop;
        ResourceAutoRestartConfig autoRestart;

        /**
         * Check if this resource exports a specific name.
         */
        bool HasExport(const std::string &exportName) const;

        /**
         * Check if this resource depends on another resource.
         */
        bool DependsOn(const std::string &resourceName) const;

        /**
         * Get the dependency entry for a specific resource.
         * Returns nullopt if not a dependency.
         */
        std::optional<ResourceDependency> GetDependency(const std::string &resourceName) const;

        /**
         * Check if a dependency is optional.
         */
        bool IsDependencyOptional(const std::string &resourceName) const;

        /**
         * Get all required (non-optional) dependencies.
         */
        std::vector<ResourceDependency> GetRequiredDependencies() const;

        /**
         * Get all optional dependencies.
         */
        std::vector<ResourceDependency> GetOptionalDependencies() const;
    };

    /**
     * Result of parsing a manifest file.
     */
    struct ManifestParseResult {
        bool success = false;
        std::string error;
        ResourceManifest manifest;

        static ManifestParseResult Success(ResourceManifest manifest) {
            ManifestParseResult result;
            result.success  = true;
            result.manifest = std::move(manifest);
            return result;
        }

        static ManifestParseResult Failure(const std::string &error) {
            ManifestParseResult result;
            result.success = false;
            result.error   = error;
            return result;
        }
    };

    /**
     * Parser and validator for resource manifest files.
     */
    class ResourceManifestParser final {
      public:
        /**
         * Parse a manifest from a JSON string.
         */
        static ManifestParseResult Parse(const std::string &jsonString);

        /**
         * Parse a manifest from a file path.
         */
        static ManifestParseResult ParseFile(const std::string &filePath);

        /**
         * Validate a manifest for correctness.
         * Checks required fields, valid version format, etc.
         */
        static bool Validate(const ResourceManifest &manifest, std::string &outError);

        /**
         * Serialize a manifest to JSON string.
         */
        static std::string Serialize(const ResourceManifest &manifest);

        /**
         * Parse dependency array from JSON.
         * Supports both simple string format and object format with version.
         */
        static std::vector<ResourceDependency> ParseDependencies(const nlohmann::json &json);

      private:

        /**
         * Validate semantic version string format.
         */
        static bool IsValidVersionString(const std::string &version);

        /**
         * Validate version constraint string format.
         */
        static bool IsValidVersionConstraint(const std::string &constraint);
    };

    // JSON serialization support for nlohmann::json
    void to_json(nlohmann::json &j, const ResourceDependency &dep);
    void from_json(const nlohmann::json &j, ResourceDependency &dep);
    void to_json(nlohmann::json &j, const ResourceManifest &manifest);
    void from_json(const nlohmann::json &j, ResourceManifest &manifest);

} // namespace Framework::Scripting
