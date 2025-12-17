#pragma once

#include "resource_manifest.h"

#include <sol/sol.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Framework::Scripting {

    /**
     * Resource state in the lifecycle state machine.
     *
     * State transitions:
     *                     ┌─────────┐
     *                     │ Unloaded│
     *                     └────┬────┘
     *                          │ load()
     *                          ▼
     *                     ┌─────────┐
     *          ┌─────────►│ Loading │
     *          │          └────┬────┘
     *          │               │ success
     *          │    error      ▼
     *          │          ┌─────────┐
     *          │    ┌─────│ Running │◄────────┐
     *          │    │     └────┬────┘         │
     *          │    │          │ stop()       │ start()
     *          │    ▼          ▼              │
     *     ┌─────────┐    ┌──────────┐    ┌─────────┐
     *     │  Error  │    │ Stopping │───►│ Stopped │
     *     └─────────┘    └──────────┘    └─────────┘
     */
    enum class ResourceState {
        Unloaded, // Resource discovered but not loaded
        Loading,  // Currently loading scripts
        Running,  // Successfully loaded and running
        Stopping, // Shutting down
        Stopped,  // Cleanly stopped, can be restarted
        Error     // Failed to load or runtime error
    };

    /**
     * Convert ResourceState to string representation.
     */
    const char *ResourceStateToString(ResourceState state);

    /**
     * Event handler callback type for resource events.
     */
    using ResourceEventHandler = std::function<void(sol::variadic_args)>;

    /**
     * Represents a single resource unit - a collection of related scripts with a manifest.
     * Each resource has its own isolated Lua environment, lifecycle, and communication channels.
     */
    class Resource final {
      public:
        /**
         * Create a resource from a directory path.
         * @param path Path to the resource directory (containing manifest.json)
         */
        explicit Resource(const std::string &path);
        ~Resource();

        // Non-copyable
        Resource(const Resource &)            = delete;
        Resource &operator=(const Resource &) = delete;

        // Moveable
        Resource(Resource &&) noexcept;
        Resource &operator=(Resource &&) noexcept;

        // Identity
        const std::string &GetName() const;
        const std::string &GetVersion() const;
        const std::string &GetAuthor() const;
        const std::string &GetDescription() const;
        const std::string &GetPath() const;

        // Manifest access
        const ResourceManifest &GetManifest() const;
        bool HasPermission(const std::string &permission) const;
        bool HasExport(const std::string &exportName) const;
        bool DependsOn(const std::string &resourceName) const;

        // State machine
        ResourceState GetState() const;
        bool IsRunning() const;
        bool IsStopped() const;
        bool HasError() const;

        /**
         * Check if the manifest was loaded successfully.
         */
        bool IsManifestValid() const;

        /**
         * Get the error message if the resource is in Error state.
         */
        const std::string &GetErrorMessage() const;

        /**
         * Get the timestamp when the resource entered its current state.
         */
        std::chrono::system_clock::time_point GetStateTimestamp() const;

        /**
         * Get the timestamp when the resource was loaded.
         */
        std::chrono::system_clock::time_point GetLoadTimestamp() const;

        // Restart tracking (Phase 6.3)

        /**
         * Get the number of restart attempts within the configured time window.
         */
        int GetRestartAttemptCount() const;

        /**
         * Check if an auto-restart is allowed based on policy.
         * Considers max attempts and time window from manifest config.
         * @return True if restart is allowed
         */
        bool CanAutoRestart() const;

        /**
         * Record a restart attempt timestamp.
         * Called by ResourceManager when attempting to restart.
         */
        void RecordRestartAttempt();

        /**
         * Clear restart attempt history.
         * Called after a successful sustained run or manual reset.
         */
        void ClearRestartAttempts();

        /**
         * Get the backoff delay for the next restart attempt.
         * Uses exponential backoff based on attempt count and config.
         * @return Delay in milliseconds
         */
        int GetRestartBackoffMs() const;

        // Health monitoring (Phase 6.3)

        /**
         * Register a health check function for this resource.
         * The function should return true if healthy, false otherwise.
         * @param healthCheck The health check function
         */
        void RegisterHealthCheck(sol::protected_function healthCheck);

        /**
         * Unregister the health check function.
         */
        void UnregisterHealthCheck();

        /**
         * Check if a health check is registered.
         */
        bool HasHealthCheck() const;

        /**
         * Invoke the health check and return the result.
         * @return True if healthy or no health check registered, false if unhealthy
         */
        bool CheckHealth() const;

        /**
         * Get the last health check result.
         */
        bool GetLastHealthCheckResult() const;

        /**
         * Get the timestamp of the last health check.
         */
        std::chrono::system_clock::time_point GetLastHealthCheckTime() const;

        // Environment access (for ResourceManager to set up)
        sol::environment *GetEnvironment();
        const sol::environment *GetEnvironment() const;

        /**
         * Set the Lua environment for this resource.
         * Should only be called by ResourceManager during load.
         */
        void SetEnvironment(std::unique_ptr<sol::environment> env);

        // Event handlers
        /**
         * Register an event handler for this resource.
         * @param eventName Name of the event
         * @param handler Callback function
         */
        void RegisterEventHandler(const std::string &eventName, sol::protected_function handler);

        /**
         * Unregister an event handler.
         * @param eventName Name of the event to unregister
         */
        void UnregisterEventHandler(const std::string &eventName);

        /**
         * Unregister all event handlers.
         */
        void ClearEventHandlers();

        /**
         * Check if an event handler is registered.
         */
        bool HasEventHandler(const std::string &eventName) const;

        /**
         * Get all registered event names.
         */
        std::vector<std::string> GetEventNames() const;

        // Exports
        /**
         * Register an export from this resource.
         * @param name Export name (must be declared in manifest)
         * @param value The exported Lua value (function or table)
         */
        bool RegisterExport(const std::string &name, sol::object value);

        /**
         * Unregister an export.
         */
        void UnregisterExport(const std::string &name);

        /**
         * Clear all exports.
         */
        void ClearExports();

        /**
         * Get an exported value by name.
         */
        sol::object GetExport(const std::string &name) const;

        /**
         * Check if an export is registered at runtime.
         */
        bool HasRegisteredExport(const std::string &name) const;

        /**
         * Get all registered export names.
         */
        std::vector<std::string> GetRegisteredExportNames() const;

        // Scripts
        /**
         * Get the list of server script paths.
         */
        std::vector<std::string> GetServerScriptPaths() const;

        /**
         * Get the list of client script paths.
         */
        std::vector<std::string> GetClientScriptPaths() const;

        /**
         * Get the number of scripts loaded.
         */
        size_t GetScriptCount() const;

        // State transitions (called by ResourceManager)
        friend class ResourceManager;

      private:
        // State transition methods (only accessible to ResourceManager)
        bool TransitionTo(ResourceState newState);
        void SetError(const std::string &error);
        void ClearError();
        void SetLoadTimestamp();

        // Check if a state transition is valid
        static bool IsValidTransition(ResourceState from, ResourceState to);

        // Path to resource directory
        std::string _path;

        // Manifest (loaded from manifest.json)
        ResourceManifest _manifest;
        bool _manifestValid = false;

        // Current state
        ResourceState _state = ResourceState::Unloaded;
        std::string _errorMessage;
        std::chrono::system_clock::time_point _stateTimestamp;
        std::chrono::system_clock::time_point _loadTimestamp;

        // Isolated Lua environment
        std::unique_ptr<sol::environment> _environment;

        // Event handlers owned by this resource
        std::map<std::string, sol::protected_function> _eventHandlers;
        mutable std::mutex _eventHandlersMutex;

        // Exports registered by this resource
        std::map<std::string, sol::object> _exports;
        mutable std::mutex _exportsMutex;

        // Script paths (resolved from manifest)
        std::vector<std::string> _serverScriptPaths;
        std::vector<std::string> _clientScriptPaths;

        // Restart tracking (Phase 6.3)
        std::vector<std::chrono::system_clock::time_point> _restartAttempts;
        mutable std::mutex _restartAttemptsMutex;

        // Health monitoring (Phase 6.3)
        std::optional<sol::protected_function> _healthCheck;
        mutable bool _lastHealthCheckResult = true;
        mutable std::chrono::system_clock::time_point _lastHealthCheckTime;
        mutable std::mutex _healthCheckMutex;
    };

} // namespace Framework::Scripting
