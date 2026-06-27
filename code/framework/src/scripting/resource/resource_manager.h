/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "resource.h"
#include "../engine.h"
#include "../builtins/events.h"

#include <logging/logger.h>
#include <utils/result.h>

#include <function2.hpp>

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
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

        // Dev mode: poll resource files and auto-reload on change (off in prod).
        bool devMode = false;

        // Minimum interval between file-change polls, in milliseconds.
        int fileWatchIntervalMs = 1000;
    };

    /**
     * Result of a resource operation.
     * Value: list of affected resource names.  Error: description string (empty on success).
     */
    using ResourceOperationResult = Utils::Result<std::vector<std::string>, std::string>;

    /**
     * Callback types for resource events.
     */
    using ResourceEventCallback = fu2::function<void(const std::string &resourceName) const>;
    using ResourceErrorCallback = fu2::function<void(const std::string &resourceName, const std::string &error) const>;
    using ResourceStateCallback = fu2::function<void(const std::string &resourceName, ResourceState oldState, ResourceState newState) const>;

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
        ResourceOperationResult StartResource(std::string_view name);

        /**
         * Stop a specific resource.
         * May cascade to dependents based on configuration.
         * @param name Resource name
         * @return Result with list of stopped resources
         */
        ResourceOperationResult StopResource(std::string_view name);

        /**
         * Restart a specific resource.
         * @param name Resource name
         * @return Result indicating success or failure
         */
        ResourceOperationResult RestartResource(std::string_view name);

        /**
         * Reload a resource from disk (hot-reload).
         * @param name Resource name
         * @return Result indicating success or failure
         */
        ResourceOperationResult ReloadResource(std::string_view name);

        /**
         * Re-parse a resource's package.json from disk and, if it was running,
         * restart it (evicting its cached modules first). Picks up both code
         * and manifest edits. Resources stopped beforehand stay stopped.
         * Backs the `refresh` console command.
         * @param name Resource name
         * @return Result with the list of affected resources
         */
        ResourceOperationResult RefreshResource(std::string_view name);

        /**
         * Re-scan the resources directory, re-parse every known resource, and
         * restart exactly those that were running. Newly discovered resources
         * are registered but left stopped. Backs the `refreshall` command.
         * @return Result with the list of affected resources
         */
        ResourceOperationResult RefreshAll();

        /**
         * Re-scan the resources directory for newly-added resources and rebuild
         * the dependency graph, without starting or restarting anything (the
         * FiveM-style `refresh`). New resources are left stopped.
         * @return Names of resources newly discovered
         */
        std::vector<std::string> Rescan();

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
        bool HasResource(std::string_view name) const;

        /**
         * Check if a resource is currently running.
         */
        bool IsResourceRunning(std::string_view name) const;

        /**
         * Get the state of a resource.
         */
        ResourceState GetResourceState(std::string_view name) const;

        /**
         * Get a resource by name (const access).
         * @return Pointer to resource, or nullptr if not found
         */
        const Resource *GetResource(std::string_view name) const;

        // Dependency Queries

        /**
         * Get resources that directly depend on the given resource.
         */
        std::set<std::string> GetDependents(std::string_view name) const;

        /**
         * Get resources that the given resource directly depends on.
         */
        std::set<std::string> GetDependencies(std::string_view name) const;

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
         * Extract resource name from a script file path.
         * Matches the path against the configured resources directory.
         * @param scriptPath Absolute path to a script file
         * @return Resource name, or empty string if not in resources directory
         */
        std::string GetResourceNameFromScriptPath(const std::string &scriptPath) const;

        /**
         * Get resource name from a V8 function's script origin.
         * Uses the function's definition location (immune to async boundaries).
         * @param isolate V8 isolate
         * @param fn V8 function to inspect
         * @return Resource name, or empty string if not determinable
         */
        std::string GetResourceNameFromFunction(v8::Isolate *isolate, v8::Local<v8::Function> fn) const;

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

        // Wired to ReplicationManager::SetOnEntityCreated/Destroyed.
        void OnEntityCreated(uint64_t networkId);
        void OnEntityDestroyed(uint64_t networkId);

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

        // Dev mode: poll running resources for file changes and hot-reload
        // them. No-op unless devMode. Call from the scripting update tick.
        void ProcessFileWatch();

      private:
        // Internal resource access (mutable)
        Resource *GetResourceMutable(std::string_view name);

        // Call resource onResourceStop lifecycle function
        bool CallResourceStop(std::string_view resourceName);

        // Build dependency graph from discovered resources
        void BuildDependencyGraph();

        // Scan the resources directory for resources not already registered and
        // register them. Returns the names added. Used by RefreshAll.
        std::vector<std::string> RescanResources();

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
        std::map<std::string, std::unique_ptr<Resource>, std::less<>> _resources;
        mutable std::mutex _resourcesMutex;

        // Dependency graph: resource -> set of dependencies
        std::map<std::string, std::set<std::string, std::less<>>, std::less<>> _dependencies;
        // Reverse dependency graph: resource -> set of dependents
        std::map<std::string, std::set<std::string, std::less<>>, std::less<>> _dependents;
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

        // Dev-mode file watcher: resource name -> newest file mtime seen.
        std::map<std::string, int64_t> _watchSnapshots;
        std::chrono::steady_clock::time_point _lastFileWatchPoll{};

        // Events instance owned by this manager
        Events _events;
    };

} // namespace Framework::Scripting
