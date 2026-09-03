/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "package_manifest.h"

#include <v8.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Framework::Scripting {

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

    class Resource;

    /**
     * Convert ResourceState to string representation.
     */
    const char *ResourceStateToString(ResourceState state);

    class ResourceManager;

    /**
     * Represents a single JavaScript resource unit.
     * Each resource has its own context, lifecycle, and communication channels.
     */
    class Resource final {
      public:
        /**
         * Create a resource from a directory path.
         * @param path Path to the resource directory (containing package.json)
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
        const PackageManifest &GetManifest() const;
        bool HasExport(std::string_view exportName) const;
        bool DependsOn(std::string_view resourceName) const;

        // True only if every declaration of this dependency is marked optional.
        bool IsOptionalDependency(std::string_view resourceName) const;

        // Resolved against the resource root, in execution order: shared scripts first.
        std::vector<std::string> GetServerScripts() const;
        std::vector<std::string> GetClientScripts() const;

        bool HasClientContent() const;

        // '/'-joined for a packaged resource, native separators otherwise.
        std::string ResolvePath(const std::string &relative) const;

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

        // Exports
        /**
         * Register an export from this resource.
         * @param name Export name (must be declared in manifest)
         * @param value The exported JavaScript value
         */
        bool RegisterExport(std::string_view name, v8::Local<v8::Value> value);

        /**
         * Unregister an export.
         */
        void UnregisterExport(std::string_view name);

        /**
         * Clear all exports.
         */
        void ClearExports();

        /**
         * Check if an export is registered at runtime.
         */
        bool HasRegisteredExport(std::string_view name) const;

        /**
         * Get all registered export names.
         */
        std::vector<std::string> GetRegisteredExportNames() const;

        /**
         * Get an export value by name.
         */
        v8::Local<v8::Value> GetExportValue(std::string_view name) const;

        // Context access
        v8::Isolate *GetIsolate() const { return _isolate; }
        void SetIsolate(v8::Isolate *isolate) { _isolate = isolate; }

        // Replicated entities spawned while this resource was executing; destroyed on stop/error.
        void TrackEntity(uint64_t networkId);
        void UntrackEntity(uint64_t networkId);

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

        // Get restart attempt count without locking
        int GetRestartAttemptCountUnlocked() const;

        void DestroyOwnedEntities();

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
        std::map<std::string, v8::Global<v8::Value>, std::less<>> _exports;
        mutable std::mutex _exportsMutex;

        std::vector<std::chrono::system_clock::time_point> _restartAttempts;
        mutable std::mutex _restartAttemptsMutex;

        std::unordered_set<uint64_t> _ownedEntities;
        mutable std::mutex _ownedEntitiesMutex;
    };

} // namespace Framework::Scripting
