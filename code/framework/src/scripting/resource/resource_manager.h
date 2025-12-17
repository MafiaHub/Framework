#pragma once

#include "dependency_graph.h"
#include "environment_sandbox.h"
#include "resource.h"
#include "resource_manifest.h"

#include <sol/sol.hpp>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace Framework::Scripting {

    /**
     * Configuration for the ResourceManager.
     */
    struct ResourceManagerConfig {
        // Path to the resources directory
        std::string resourcesPath = "resources";

        // Whether to enable legacy gamemode support (backward compatibility)
        bool enableLegacySupport = true;

        // Whether this is a client-side manager (enables extra sandboxing)
        bool isClient = false;

        // Whether to cascade stop dependents when a resource stops
        bool cascadeStopDependents = true;

        // Whether to warn (instead of error) when a dependency is missing
        bool warnOnMissingDependency = false;
    };

    /**
     * Result of a resource operation.
     */
    struct ResourceOperationResult {
        bool success = false;
        std::string error;
        std::vector<std::string> affectedResources;

        static ResourceOperationResult Success(const std::vector<std::string> &affected = {}) {
            ResourceOperationResult result;
            result.success           = true;
            result.affectedResources = affected;
            return result;
        }

        static ResourceOperationResult Failure(const std::string &error) {
            ResourceOperationResult result;
            result.success = false;
            result.error   = error;
            return result;
        }
    };

    /**
     * Callback types for resource events.
     */
    using ResourceEventCallback     = std::function<void(const std::string &resourceName)>;
    using ResourceErrorCallback     = std::function<void(const std::string &resourceName, const std::string &error)>;
    using ResourceStateCallback     = std::function<void(const std::string &resourceName, ResourceState oldState, ResourceState newState)>;

    /**
     * Central manager for all resources.
     *
     * Responsibilities:
     * - Discovery: Scan directories for valid resource manifests
     * - Dependency Resolution: Build and maintain dependency graph
     * - Lifecycle Management: Load, start, stop, and unload resources
     * - Registry: Track all discovered and running resources
     *
     * Phase 8 Considerations:
     * - Backward Compatibility: Supports legacy single-script gamemodes
     * - Integration: Works with existing Engine classes
     */
    class ResourceManager final {
      public:
        explicit ResourceManager(sol::state *luaState, const ResourceManagerConfig &config = {});
        ~ResourceManager();

        // Non-copyable
        ResourceManager(const ResourceManager &)            = delete;
        ResourceManager &operator=(const ResourceManager &) = delete;

        // Configuration
        const ResourceManagerConfig &GetConfig() const;
        void SetConfig(const ResourceManagerConfig &config);

        // Discovery

        /**
         * Scan the resources directory and discover all valid resources.
         * This populates the registry but doesn't start any resources.
         * @return Number of resources discovered
         */
        size_t DiscoverResources();

        /**
         * Discover a single resource from a directory path.
         * @param path Path to the resource directory
         * @return True if a valid resource was discovered
         */
        bool DiscoverResource(const std::string &path);

        /**
         * Check if the legacy gamemode structure exists.
         * Used for backward compatibility detection.
         */
        bool HasLegacyGamemode() const;

        /**
         * Get the path to the legacy gamemode if it exists.
         */
        std::string GetLegacyGamemodePath() const;

        // Lifecycle Management

        /**
         * Start all discovered resources in dependency order.
         * @return Result with list of started resources
         */
        ResourceOperationResult StartAll();

        /**
         * Stop all running resources in reverse dependency order.
         * @return Result with list of stopped resources
         */
        ResourceOperationResult StopAll();

        /**
         * Start a specific resource and its dependencies.
         * @param name Resource name
         * @return Result indicating success or failure
         */
        ResourceOperationResult StartResource(const std::string &name);

        /**
         * Stop a specific resource.
         * May cascade to dependents based on configuration.
         * @param name Resource name
         * @return Result with list of stopped resources
         */
        ResourceOperationResult StopResource(const std::string &name);

        /**
         * Restart a specific resource.
         * @param name Resource name
         * @return Result indicating success or failure
         */
        ResourceOperationResult RestartResource(const std::string &name);

        /**
         * Reload a resource from disk (hot-reload).
         * Preserves state if the resource supports it.
         * @param name Resource name
         * @return Result indicating success or failure
         */
        ResourceOperationResult ReloadResource(const std::string &name);

        // Registry Queries

        /**
         * Get all discovered resource names.
         */
        std::vector<std::string> GetAllResourceNames() const;

        /**
         * Get all currently running resource names.
         */
        std::vector<std::string> GetRunningResourceNames() const;

        /**
         * Get the load order for all resources.
         */
        std::vector<std::string> GetLoadOrder() const;

        /**
         * Check if a resource exists (discovered).
         */
        bool HasResource(const std::string &name) const;

        /**
         * Check if a resource is currently running.
         */
        bool IsResourceRunning(const std::string &name) const;

        /**
         * Get the state of a resource.
         */
        ResourceState GetResourceState(const std::string &name) const;

        /**
         * Get a resource by name (const access).
         * @return Pointer to resource, or nullptr if not found
         */
        const Resource *GetResource(const std::string &name) const;

        /**
         * Get resource info as a table (for Lua API).
         * Returns manifest data plus runtime state.
         */
        sol::table GetResourceInfo(sol::state &luaState, const std::string &name) const;

        // Dependency Queries

        /**
         * Get resources that directly depend on the given resource.
         */
        std::set<std::string> GetDependents(const std::string &name) const;

        /**
         * Get resources that the given resource directly depends on.
         */
        std::set<std::string> GetDependencies(const std::string &name) const;

        // Exports Registry

        /**
         * Get an export from a resource.
         * @param resourceName Name of the exporting resource
         * @param exportName Name of the export
         * @return The exported value, or nil if not found
         */
        sol::object GetExport(const std::string &resourceName, const std::string &exportName) const;

        /**
         * List all exports from a resource.
         * @param resourceName Name of the resource
         * @return Vector of export names
         */
        std::vector<std::string> ListExports(const std::string &resourceName) const;

        // Event Callbacks

        /**
         * Set callback for when a resource starts.
         */
        void SetOnResourceStarted(ResourceEventCallback callback);

        /**
         * Set callback for when a resource stops.
         */
        void SetOnResourceStopped(ResourceEventCallback callback);

        /**
         * Set callback for when a resource encounters an error.
         */
        void SetOnResourceError(ResourceErrorCallback callback);

        /**
         * Set callback for resource state changes.
         */
        void SetOnResourceStateChanged(ResourceStateCallback callback);

        // Lua State Access

        /**
         * Get the Lua state used by this manager.
         */
        sol::state *GetLuaState() const;

        // Statistics

        /**
         * Get the number of discovered resources.
         */
        size_t GetResourceCount() const;

        /**
         * Get the number of running resources.
         */
        size_t GetRunningResourceCount() const;

      private:
        // Internal resource access (mutable)
        Resource *GetResourceMutable(const std::string &name);

        // Build dependency graph from discovered resources
        void BuildDependencyGraph();

        // Validate all dependencies can be satisfied
        bool ValidateDependencies(std::string &outError) const;

        // Execute scripts for a resource
        bool ExecuteResourceScripts(Resource &resource, std::string &outError);

        // Create and setup environment for a resource
        std::unique_ptr<sol::environment> CreateResourceEnvironment(const std::string &resourceName);

        // Fire resource lifecycle events
        void FireOnResourceStarted(const std::string &name);
        void FireOnResourceStopped(const std::string &name);
        void FireOnResourceError(const std::string &name, const std::string &error);
        void FireOnResourceStateChanged(const std::string &name, ResourceState oldState, ResourceState newState);

        // Configuration
        ResourceManagerConfig _config;

        // Lua state (not owned)
        sol::state *_luaState = nullptr;

        // Resource registry
        std::map<std::string, std::unique_ptr<Resource>> _resources;
        mutable std::mutex _resourcesMutex;

        // Dependency graph
        DependencyGraph _dependencyGraph;
        mutable std::mutex _graphMutex;

        // Event callbacks
        ResourceEventCallback _onResourceStarted;
        ResourceEventCallback _onResourceStopped;
        ResourceErrorCallback _onResourceError;
        ResourceStateCallback _onResourceStateChanged;

        // Legacy gamemode path (for backward compatibility)
        std::string _legacyGamemodePath;
        bool _hasLegacyGamemode = false;
    };

} // namespace Framework::Scripting
