#pragma once

#include "package_manifest.h"

#include <v8.h>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Framework::Scripting::JS {

    /**
     * Resource state in the lifecycle state machine.
     *
     * State transitions:
     *                     +----------+
     *                     | Unloaded |
     *                     +----+-----+
     *                          | load()
     *                          v
     *                     +----------+
     *          +--------> | Loading  |
     *          |          +----+-----+
     *          |               | success
     *          |    error      v
     *          |          +----------+
     *          |    +-----| Running  |<--------+
     *          |    |     +----+-----+         |
     *          |    |          | stop()        | start()
     *          |    v          v               |
     *     +----------+    +----------+    +----------+
     *     |  Error   |    | Stopping |--->| Stopped  |
     *     +----------+    +----------+    +----------+
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

    class JSResourceManager;

    /**
     * Represents a single JavaScript resource unit.
     * Each resource has its own context, lifecycle, and communication channels.
     */
    class JSResource final {
      public:
        /**
         * Create a resource from a directory path.
         * @param path Path to the resource directory (containing package.json)
         */
        explicit JSResource(const std::string &path);

        ~JSResource();

        // Non-copyable
        JSResource(const JSResource &)            = delete;
        JSResource &operator=(const JSResource &) = delete;

        // Moveable
        JSResource(JSResource &&) noexcept;
        JSResource &operator=(JSResource &&) noexcept;

        // Identity
        const std::string &GetName() const;
        const std::string &GetVersion() const;
        const std::string &GetAuthor() const;
        const std::string &GetDescription() const;
        const std::string &GetPath() const;

        // Manifest access
        const PackageManifest &GetManifest() const;
        bool HasExport(const std::string &exportName) const;
        bool DependsOn(const std::string &resourceName) const;

        /**
         * Get the server entry point script path.
         */
        std::string GetServerEntryPoint() const;

        /**
         * Get the client entry point script path.
         */
        std::string GetClientEntryPoint() const;

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
         * Called by JSResourceManager when attempting to restart.
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

        // Exports
        /**
         * Register an export from this resource.
         * @param name Export name (must be declared in manifest)
         * @param value The exported JavaScript value
         */
        bool RegisterExport(const std::string &name, v8::Local<v8::Value> value);

        /**
         * Unregister an export.
         */
        void UnregisterExport(const std::string &name);

        /**
         * Clear all exports.
         */
        void ClearExports();

        /**
         * Check if an export is registered at runtime.
         */
        bool HasRegisteredExport(const std::string &name) const;

        /**
         * Get all registered export names.
         */
        std::vector<std::string> GetRegisteredExportNames() const;

        // Context access
        v8::Isolate *GetIsolate() const { return _isolate; }
        void SetIsolate(v8::Isolate *isolate) { _isolate = isolate; }

        // State transitions (called by JSResourceManager)
        friend class JSResourceManager;

      private:
        // State transition methods (only accessible to JSResourceManager)
        bool TransitionTo(ResourceState newState);
        void SetError(const std::string &error);
        void ClearError();
        void SetLoadTimestamp();

        // Check if a state transition is valid
        static bool IsValidTransition(ResourceState from, ResourceState to);

        // Get restart attempt count without locking
        int GetRestartAttemptCountUnlocked() const;

        // Path to resource directory
        std::string _path;

        // Manifest (loaded from package.json)
        PackageManifest _manifest;
        bool _manifestValid = false;

        // Current state
        ResourceState _state = ResourceState::Unloaded;
        std::string _errorMessage;
        std::chrono::system_clock::time_point _stateTimestamp;
        std::chrono::system_clock::time_point _loadTimestamp;

        // V8 isolate for this resource (set by manager)
        v8::Isolate *_isolate = nullptr;

        // Exports registered by this resource
        std::map<std::string, v8::Global<v8::Value>> _exports;
        mutable std::mutex _exportsMutex;

        std::vector<std::chrono::system_clock::time_point> _restartAttempts;
        mutable std::mutex _restartAttemptsMutex;
    };

} // namespace Framework::Scripting::JS
