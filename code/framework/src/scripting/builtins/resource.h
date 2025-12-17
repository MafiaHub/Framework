#pragma once

#include "core_modules.h"

#include "../resource/resource_manager.h"

namespace Framework::Scripting::Builtins {

    /**
     * Resource builtin for resource lifecycle control and queries.
     *
     * Lua API:
     *   -- Lifecycle control
     *   Resource.start(name)                -- Load and start a resource
     *   Resource.stop(name)                 -- Stop a running resource
     *   Resource.restart(name)              -- Stop then start
     *
     *   -- State queries
     *   Resource.isRunning(name)            -- Returns boolean
     *   Resource.getState(name)             -- Returns "running", "stopped", etc.
     *
     *   -- Discovery
     *   Resource.list()                     -- Returns array of all resource names
     *   Resource.getRunning()               -- Returns array of running resource names
     *   Resource.getInfo(name)              -- Returns manifest metadata table
     *
     *   -- Self-reference
     *   Resource.getCurrent()               -- Returns current resource's name
     *   Resource.getPath()                  -- Returns current resource's directory path
     *
     *   -- Permissions
     *   Resource.hasPermission(permission)  -- Check if current resource has permission
     *   Resource.getPermissions(name)       -- Get all permissions for a resource
     *
     *   -- Error handling and recovery (Phase 6)
     *   Resource.setHealthCheck(fn)         -- Register health check function for current resource
     *   Resource.isHealthy(name)            -- Check if resource is healthy
     *   Resource.getErrorMessage(name)      -- Get error message if in error state
     *   Resource.getRestartAttempts(name)   -- Get restart attempts in time window
     *   Resource.canAutoRestart(name)       -- Check if auto-restart is allowed
     *   Resource.clearRestartAttempts(name) -- Reset restart attempt counter
     */
    class ResourceBuiltin final {
        /**
         * Start a resource by name.
         * Also starts any dependencies that aren't running.
         *
         * @param name Resource name
         * @return True if started successfully
         */
        static bool Start(const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return false;
            }

            auto result = manager->StartResource(name);
            return result.success;
        }

        /**
         * Stop a running resource.
         * May cascade to dependents based on configuration.
         *
         * @param name Resource name
         * @return True if stopped successfully
         */
        static bool Stop(const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return false;
            }

            auto result = manager->StopResource(name);
            return result.success;
        }

        /**
         * Restart a resource (stop then start).
         *
         * @param name Resource name
         * @return True if restarted successfully
         */
        static bool Restart(const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return false;
            }

            auto result = manager->RestartResource(name);
            return result.success;
        }

        /**
         * Check if a resource is currently running.
         *
         * @param name Resource name
         * @return True if running
         */
        static bool IsRunning(const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return false;
            }

            return manager->IsResourceRunning(name);
        }

        /**
         * Get the state of a resource as a string.
         *
         * @param name Resource name
         * @return State string ("unloaded", "loading", "running", "stopping", "stopped", "error")
         *         or nil if resource not found
         */
        static sol::object GetState(sol::state_view luaState, const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager || !manager->HasResource(name)) {
                return sol::nil;
            }

            return sol::make_object(luaState, ResourceStateToString(manager->GetResourceState(name)));
        }

        /**
         * Get all discovered resource names.
         *
         * @return Array table of resource names
         */
        static sol::table List(sol::state_view luaState) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return luaState.create_table();
            }

            auto names      = manager->GetAllResourceNames();
            sol::table result = luaState.create_table();

            int i = 1;
            for (const auto &name : names) {
                result[i++] = name;
            }

            return result;
        }

        /**
         * Get all currently running resource names.
         *
         * @return Array table of running resource names
         */
        static sol::table GetRunning(sol::state_view luaState) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return luaState.create_table();
            }

            auto names      = manager->GetRunningResourceNames();
            sol::table result = luaState.create_table();

            int i = 1;
            for (const auto &name : names) {
                result[i++] = name;
            }

            return result;
        }

        /**
         * Get detailed information about a resource.
         * Returns a table with: name, version, author, description, state, path,
         * scriptCount, dependencies, exports, permissions, loadTime
         *
         * @param name Resource name
         * @return Info table or nil if not found
         */
        static sol::object GetInfo(sol::state_view luaState, const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return sol::nil;
            }

            sol::state &state = const_cast<sol::state &>(static_cast<const sol::state &>(luaState));
            return manager->GetResourceInfo(state, name);
        }

        /**
         * Get the name of the currently executing resource.
         *
         * @return Resource name or empty string if not in resource context
         */
        static std::string GetCurrent() {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return "";
            }

            return manager->GetCurrentResourceContext();
        }

        /**
         * Get the directory path of the current resource.
         *
         * @return Path to resource directory or empty string if not in resource context
         */
        static std::string GetPath() {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return "";
            }

            auto *resource = manager->GetCurrentResource();
            if (!resource) {
                return "";
            }

            return resource->GetPath();
        }

        /**
         * Check if the current resource has a specific permission.
         *
         * @param permission Permission name to check
         * @return True if the current resource has the permission
         */
        static bool HasPermission(const std::string &permission) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return false;
            }

            auto *resource = manager->GetCurrentResource();
            if (!resource) {
                return false;
            }

            return resource->HasPermission(permission);
        }

        /**
         * Get all permissions for a resource.
         *
         * @param name Resource name
         * @return Array table of permission strings, or empty table if not found
         */
        static sol::table GetPermissions(sol::state_view luaState, const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return luaState.create_table();
            }

            const Resource *resource = manager->GetResource(name);
            if (!resource) {
                return luaState.create_table();
            }

            sol::table result           = luaState.create_table();
            const auto &permissions = resource->GetManifest().permissions;

            int i = 1;
            for (const auto &perm : permissions) {
                result[i++] = perm;
            }

            return result;
        }

        // Error Handling and Recovery (Phase 6)

        /**
         * Register a health check function for the current resource.
         * The function should return true if healthy, false otherwise.
         *
         * @param healthCheck The health check function
         */
        static void SetHealthCheck(sol::protected_function healthCheck) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return;
            }

            manager->RegisterHealthCheck(healthCheck);
        }

        /**
         * Check if a resource is healthy.
         *
         * @param name Resource name
         * @return True if healthy or no health check registered
         */
        static bool IsHealthy(const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return true;
            }

            return manager->IsResourceHealthy(name);
        }

        /**
         * Get the error message for a resource in error state.
         *
         * @param name Resource name
         * @return Error message or nil if no error
         */
        static sol::object GetErrorMessage(sol::state_view luaState, const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return sol::nil;
            }

            const Resource *resource = manager->GetResource(name);
            if (!resource || !resource->HasError()) {
                return sol::nil;
            }

            return sol::make_object(luaState, resource->GetErrorMessage());
        }

        /**
         * Get the number of restart attempts for a resource within its time window.
         *
         * @param name Resource name
         * @return Number of restart attempts
         */
        static int GetRestartAttempts(const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return 0;
            }

            const Resource *resource = manager->GetResource(name);
            if (!resource) {
                return 0;
            }

            return resource->GetRestartAttemptCount();
        }

        /**
         * Check if a resource can be auto-restarted.
         *
         * @param name Resource name
         * @return True if auto-restart is possible
         */
        static bool CanAutoRestart(const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return false;
            }

            const Resource *resource = manager->GetResource(name);
            if (!resource) {
                return false;
            }

            return resource->CanAutoRestart();
        }

        /**
         * Clear restart attempts for a resource, resetting the backoff counter.
         *
         * @param name Resource name
         */
        static void ClearRestartAttempts(const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return;
            }

            Resource *resource = const_cast<Resource *>(manager->GetResource(name));
            if (!resource) {
                return;
            }

            resource->ClearRestartAttempts();
        }

      public:
        static void Register(sol::state *luaEngine) {
            sol::usertype<ResourceBuiltin> cls = luaEngine->new_usertype<ResourceBuiltin>("Resource");

            // Lifecycle control
            cls["start"]   = &ResourceBuiltin::Start;
            cls["stop"]    = &ResourceBuiltin::Stop;
            cls["restart"] = &ResourceBuiltin::Restart;

            // State queries
            cls["isRunning"] = &ResourceBuiltin::IsRunning;
            cls["getState"]  = &ResourceBuiltin::GetState;

            // Discovery
            cls["list"]       = &ResourceBuiltin::List;
            cls["getRunning"] = &ResourceBuiltin::GetRunning;
            cls["getInfo"]    = &ResourceBuiltin::GetInfo;

            // Self-reference
            cls["getCurrent"] = &ResourceBuiltin::GetCurrent;
            cls["getPath"]    = &ResourceBuiltin::GetPath;

            // Permissions
            cls["hasPermission"]  = &ResourceBuiltin::HasPermission;
            cls["getPermissions"] = &ResourceBuiltin::GetPermissions;

            // Error handling and recovery (Phase 6)
            cls["setHealthCheck"]       = &ResourceBuiltin::SetHealthCheck;
            cls["isHealthy"]            = &ResourceBuiltin::IsHealthy;
            cls["getErrorMessage"]      = &ResourceBuiltin::GetErrorMessage;
            cls["getRestartAttempts"]   = &ResourceBuiltin::GetRestartAttempts;
            cls["canAutoRestart"]       = &ResourceBuiltin::CanAutoRestart;
            cls["clearRestartAttempts"] = &ResourceBuiltin::ClearRestartAttempts;
        }
    };

} // namespace Framework::Scripting::Builtins
