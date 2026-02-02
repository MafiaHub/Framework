#pragma once

#include "resource.h"
#include "../engine.h"
#include "../builtins/events.h"

#include <logging/logger.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

// Forward declaration for V8
namespace v8 {
    class Isolate;
}

namespace Framework::Scripting {

    /**
     * Configuration for the ResourceManager.
     */
    struct ResourceManagerConfig {
        // Path to the resources directory
        std::string resourcesPath = "resources";

        // Whether this is a client-side manager
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
            result.success = true;
            result.affectedResources = affected;
            return result;
        }

        static ResourceOperationResult Failure(const std::string &error) {
            ResourceOperationResult result;
            result.success = false;
            result.error = error;
            return result;
        }
    };

    /**
     * Callback types for resource events.
     */
    using ResourceEventCallback = std::function<void(const std::string &resourceName)>;
    using ResourceErrorCallback = std::function<void(const std::string &resourceName, const std::string &error)>;
    using ResourceStateCallback = std::function<void(const std::string &resourceName, ResourceState oldState, ResourceState newState)>;

    /**
     * Central manager for JavaScript resources.
     *
     * Responsibilities:
     * - Discovery: Scan directories for valid package.json files
     * - Dependency Resolution: Build and maintain dependency graph
     * - Lifecycle Management: Load, start, stop, and unload resources
     * - Registry: Track all discovered and running resources
     */
    class ResourceManager final {
      public:
        explicit ResourceManager(Engine *jsEngine, const ResourceManagerConfig &config = {});
        ~ResourceManager();

        // Non-copyable
        ResourceManager(const ResourceManager &) = delete;
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

        // Dependency Queries

        /**
         * Get resources that directly depend on the given resource.
         */
        std::set<std::string> GetDependents(const std::string &name) const;

        /**
         * Get resources that the given resource directly depends on.
         */
        std::set<std::string> GetDependencies(const std::string &name) const;

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

        // JS Engine Access

        /**
         * Get the JavaScript engine used by this manager.
         */
        Engine *GetJSEngine() const;

        /**
         * Get the Events instance owned by this manager.
         */
        Events &GetEvents();

        // Current Resource Context

        /**
         * Set the currently executing resource name.
         * @param name Resource name, or empty string to clear
         */
        void SetCurrentResourceContext(const std::string &name);

        /**
         * Get the currently executing resource name.
         * @return Current resource name, or empty string if none
         */
        std::string GetCurrentResourceContext() const;

        /**
         * Get resource name from V8 call stack by examining file paths.
         * Used as fallback when async ES modules are loading.
         * @param isolate V8 isolate to get stack trace from
         * @return Resource name extracted from stack, or empty string
         */
        std::string GetResourceContextFromStack(v8::Isolate *isolate) const;

        /**
         * Get the currently executing resource.
         * @return Pointer to current resource, or nullptr if none
         */
        Resource *GetCurrentResource();

        /**
         * Get the currently executing resource with stack fallback.
         * Uses V8 stack trace to determine resource when context isn't set.
         * @param isolate V8 isolate for stack trace
         * @return Pointer to current resource, or nullptr if none
         */
        Resource *GetCurrentResourceWithStackFallback(v8::Isolate *isolate);

        // Statistics

        /**
         * Get the number of discovered resources.
         */
        size_t GetResourceCount() const;

        /**
         * Get the number of running resources.
         */
        size_t GetRunningResourceCount() const;

        /**
         * Handle a runtime error in a resource.
         * @param resourceName Name of the resource that encountered the error
         * @param error Error message
         */
        void HandleResourceRuntimeError(const std::string &resourceName, const std::string &error);

        /**
         * Schedule an auto-restart for a resource.
         * @param resourceName Name of the resource to restart
         * @return True if restart was scheduled
         */
        bool ScheduleAutoRestart(const std::string &resourceName);

        /**
         * Process scheduled restart tasks.
         */
        void ProcessScheduledRestarts();

      private:
        // Internal resource access (mutable)
        Resource *GetResourceMutable(const std::string &name);

        // Call resource onResourceStop lifecycle function
        bool CallResourceStop(const std::string &resourceName);

        // Build dependency graph from discovered resources
        void BuildDependencyGraph();

        // Validate all dependencies can be satisfied
        bool ValidateDependencies(std::string &outError) const;

        // Execute entry point script for a resource
        bool ExecuteResourceScript(Resource &resource, std::string &outError);

        // Fire resource lifecycle events
        void FireOnResourceStarted(const std::string &name);
        void FireOnResourceStopped(const std::string &name);
        void FireOnResourceError(const std::string &name, const std::string &error);
        void FireOnResourceStateChanged(const std::string &name, ResourceState oldState, ResourceState newState);

        // Compute topological sort for load order
        std::vector<std::string> ComputeLoadOrder() const;

        // Configuration
        ResourceManagerConfig _config;

        // JS engine (not owned)
        Engine *_jsEngine = nullptr;

        // Resource registry
        std::map<std::string, std::unique_ptr<Resource>> _resources;
        mutable std::mutex _resourcesMutex;

        // Dependency graph: resource -> set of dependencies
        std::map<std::string, std::set<std::string>> _dependencies;
        // Reverse dependency graph: resource -> set of dependents
        std::map<std::string, std::set<std::string>> _dependents;
        mutable std::mutex _graphMutex;

        // Event callbacks
        ResourceEventCallback _onResourceStarted;
        ResourceEventCallback _onResourceStopped;
        ResourceErrorCallback _onResourceError;
        ResourceStateCallback _onResourceStateChanged;

        // Current resource context
        std::string _currentResourceContext;
        mutable std::mutex _contextMutex;

        // Scheduled restarts
        struct ScheduledRestart {
            std::string resourceName;
            std::chrono::system_clock::time_point scheduledTime;
        };
        std::vector<ScheduledRestart> _scheduledRestarts;
        mutable std::mutex _scheduledRestartsMutex;

        // Events instance owned by this manager
        Events _events;
    };

} // namespace Framework::Scripting
