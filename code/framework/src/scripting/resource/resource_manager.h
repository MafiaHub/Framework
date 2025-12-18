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
     * Represents a single entry in the export call chain.
     * Used for debugging and detecting infinite loops when exports call each other.
     */
    struct ExportCallEntry {
        std::string callerResource;   // Resource that made the export call
        std::string targetResource;   // Resource that owns the export
        std::string exportName;       // Name of the export being called

        bool operator==(const ExportCallEntry &other) const {
            return callerResource == other.callerResource &&
                   targetResource == other.targetResource &&
                   exportName == other.exportName;
        }
    };

    /**
     * Maximum depth for export call chain before considering it an infinite loop.
     */
    constexpr size_t kMaxExportCallDepth = 64;

    /**
     * Central manager for all resources.
     *
     * Responsibilities:
     * - Discovery: Scan directories for valid resource manifests
     * - Dependency Resolution: Build and maintain dependency graph
     * - Lifecycle Management: Load, start, stop, and unload resources
     * - Registry: Track all discovered and running resources
     * - Backward Compatibility: Supports legacy single-script gamemodes
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

        // Export Call Chain Tracking

        /**
         * Get the resource name that called the current export.
         * @return Caller resource name, or empty string if not in an export call
         */
        std::string GetExportCaller() const;

        /**
         * Get the full export call chain for debugging.
         * @return Vector of call entries, from oldest to newest
         */
        std::vector<ExportCallEntry> GetExportCallChain() const;

        /**
         * Get the current export call depth.
         * @return Number of nested export calls
         */
        size_t GetExportCallDepth() const;

        /**
         * Check if we're currently inside an export call.
         * @return True if inside an export call
         */
        bool IsInExportCall() const;

        /**
         * Format the export call chain as a readable string for debugging.
         * @return Formatted call chain string
         */
        std::string FormatExportCallChain() const;

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

        // Current Resource Context

        /**
         * Set the currently executing resource name.
         * Used by the event system and builtins to know which resource is calling.
         * @param name Resource name, or empty string to clear
         */
        void SetCurrentResourceContext(const std::string &name);

        /**
         * Get the currently executing resource name.
         * @return Current resource name, or empty string if none
         */
        std::string GetCurrentResourceContext() const;

        /**
         * Get the currently executing resource.
         * @return Pointer to current resource, or nullptr if none
         */
        Resource *GetCurrentResource();

        /**
         * Register an export from the current resource.
         * @param exportName Name of the export
         * @param value The value to export
         * @return True if successful
         */
        bool RegisterExport(const std::string &exportName, sol::object value);

        /**
         * Broadcast a global event to all running resources.
         * @param eventName Name of the event
         * @param args Event arguments
         */
        void BroadcastGlobalEvent(const std::string &eventName, sol::variadic_args args);

        /**
         * Send a targeted event to a specific resource.
         * @param targetResource Name of the target resource
         * @param eventName Name of the event
         * @param args Event arguments
         * @return True if the target resource was found and running
         */
        bool EmitTargetedEvent(const std::string &targetResource, const std::string &eventName, sol::variadic_args args);

        /**
         * Register a handler for global events in the current resource.
         * @param eventName Name of the event
         * @param handler Event handler function
         */
        void RegisterGlobalEventHandler(const std::string &eventName, sol::protected_function handler);

        /**
         * Register a handler for targeted events in the current resource.
         * @param eventName Name of the event
         * @param handler Event handler function
         */
        void RegisterTargetedEventHandler(const std::string &eventName, sol::protected_function handler);

        /**
         * Send a fire-and-forget message to a resource.
         * @param targetResource Name of the target resource
         * @param messageType Type of the message
         * @param payload Message payload
         */
        void SendMessage(const std::string &targetResource, const std::string &messageType, sol::object payload);

        /**
         * Send a request message with callback.
         * @param targetResource Name of the target resource
         * @param messageType Type of the message
         * @param payload Message payload
         * @param callback Callback to invoke with response
         */
        void SendRequest(const std::string &targetResource, const std::string &messageType, sol::object payload, sol::protected_function callback);

        /**
         * Register a message handler for the current resource.
         * @param messageType Type of message to handle
         * @param handler Handler function that receives (request, reply)
         */
        void RegisterMessageHandler(const std::string &messageType, sol::protected_function handler);

        /**
         * Process pending message callbacks (call in update loop).
         */
        void ProcessMessageQueue();

        /**
         * Fire a lifecycle event within a resource's environment.
         * These events are internal to the resource (onResourceLoad, onResourceStart, etc.)
         *
         * @param resourceName Name of the resource
         * @param eventName Name of the lifecycle event
         * @param args Optional arguments to pass to the handler
         * @return True if the event handler was found and executed successfully
         */
        bool FireResourceLifecycleEvent(const std::string &resourceName, const std::string &eventName, sol::variadic_args args);

        /**
         * Fire a lifecycle event within a resource with a preserved state table.
         * Used during hot-reload to pass preserved state.
         *
         * @param resourceName Name of the resource
         * @param eventName Name of the lifecycle event
         * @param state The preserved state table (or nil)
         * @return True if the event handler was found and executed successfully
         */
        bool FireResourceLifecycleEventWithState(const std::string &resourceName, const std::string &eventName, sol::object state);

        /**
         * Broadcast a resource awareness event to all running resources.
         * These events notify resources about other resources (onResourceStarted, onResourceStopped)
         *
         * @param eventName Name of the event (e.g., "onResourceStarted")
         * @param affectedResourceName Name of the resource that triggered the event
         */
        void BroadcastResourceAwarenessEvent(const std::string &eventName, const std::string &affectedResourceName);

        /**
         * Check if there is preserved state for a resource.
         * @param name Resource name
         * @return True if preserved state exists
         */
        bool HasPreservedState(const std::string &name) const;

        /**
         * Clear preserved state for a specific resource.
         * Use this if you want to discard saved state before a restart.
         * @param name Resource name
         */
        void ClearPreservedState(const std::string &name);

        /**
         * Clear all preserved states.
         */
        void ClearAllPreservedStates();

        /**
         * Get the preserved state for a resource (without removing it).
         * @param name Resource name
         * @return The preserved state object, or nil if none
         */
        sol::object GetPreservedState(const std::string &name) const;

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
         * Behavior depends on the resource's errorBehavior setting in manifest.
         *
         * @param resourceName Name of the resource that encountered the error
         * @param error Error message
         * @param stackTrace Optional stack trace
         */
        void HandleResourceRuntimeError(const std::string &resourceName, const std::string &error, const std::string &stackTrace = "");

        /**
         * Schedule an auto-restart for a resource.
         * Uses exponential backoff based on restart attempts.
         *
         * @param resourceName Name of the resource to restart
         * @return True if restart was scheduled, false if not allowed
         */
        bool ScheduleAutoRestart(const std::string &resourceName);

        /**
         * Process scheduled restart tasks.
         * Should be called periodically from the update loop.
         */
        void ProcessScheduledRestarts();

        /**
         * Run health checks on all resources that have them registered.
         * Should be called periodically.
         */
        void RunHealthChecks();

        /**
         * Check if a specific resource is healthy.
         * @param name Resource name
         * @return True if healthy or no health check registered
         */
        bool IsResourceHealthy(const std::string &name) const;

        /**
         * Register a health check function for a resource.
         * Called from Lua via the Resource builtin.
         *
         * @param healthCheck The health check function
         */
        void RegisterHealthCheck(sol::protected_function healthCheck);

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

        // Current resource context (for builtins to know which resource is calling)
        std::string _currentResourceContext;
        mutable std::mutex _contextMutex;

        // Export call chain stack for tracking nested export calls
        // Used for debugging infinite loops and knowing who called an export
        mutable std::vector<ExportCallEntry> _exportCallChain;
        mutable std::mutex _exportCallChainMutex;

        // Helper methods for export call chain management
        bool PushExportCall(const std::string &callerResource, const std::string &targetResource, const std::string &exportName) const;
        void PopExportCall() const;
        bool HasCycleInExportCallChain(const std::string &targetResource, const std::string &exportName) const;

        // Global event handlers: eventName -> {resourceName -> handlers}
        std::map<std::string, std::map<std::string, std::vector<sol::protected_function>>> _globalEventHandlers;
        mutable std::mutex _globalEventsMutex;

        // Targeted event handlers: resourceName -> {eventName -> handlers}
        std::map<std::string, std::map<std::string, std::vector<sol::protected_function>>> _targetedEventHandlers;
        mutable std::mutex _targetedEventsMutex;

        // Message handlers: resourceName -> {messageType -> handler}
        std::map<std::string, std::map<std::string, sol::protected_function>> _messageHandlers;
        mutable std::mutex _messageHandlersMutex;

        // Pending message request structure
        struct PendingRequest {
            uint64_t requestId;
            sol::protected_function callback;
            std::string sourceResource;
        };

        // Pending message response structure
        struct PendingResponse {
            uint64_t requestId;
            sol::object response;
        };

        // Pending request callbacks waiting for response
        std::map<uint64_t, PendingRequest> _pendingRequests;
        mutable std::mutex _pendingRequestsMutex;

        // Response queue for processing in the main thread
        std::vector<PendingResponse> _responseQueue;
        mutable std::mutex _responseQueueMutex;

        // Request ID counter
        uint64_t _nextRequestId = 1;

        std::map<std::string, sol::object> _preservedStates;
        mutable std::mutex _preservedStatesMutex;

        // Internal helper to fire lifecycle event without variadic args
        bool FireResourceLifecycleEventInternal(const std::string &resourceName, const std::string &eventName, std::vector<sol::object> args);

        struct ScheduledRestart {
            std::string resourceName;
            std::chrono::system_clock::time_point scheduledTime;
        };

        // Scheduled restarts queue
        std::vector<ScheduledRestart> _scheduledRestarts;
        mutable std::mutex _scheduledRestartsMutex;
    };

} // namespace Framework::Scripting
