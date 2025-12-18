#pragma once

#include "core_modules.h"

#include "../resource/resource_manager.h"

#include <networking/messages/resource_command.h>
#include <networking/messages/resource_list.h>
#include <networking/network_peer.h>

namespace Framework::Scripting::Builtins {

    /**
     * Resource builtin for resource lifecycle control and queries.
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
                return sol::lua_nil;
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
         * scriptCount, dependencies, exports, loadTime
         *
         * @param name Resource name
         * @return Info table or nil if not found
         */
        static sol::object GetInfo(sol::state_view luaState, const std::string &name) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return sol::lua_nil;
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
                return sol::lua_nil;
            }

            const Resource *resource = manager->GetResource(name);
            if (!resource || !resource->HasError()) {
                return sol::lua_nil;
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


        /**
         * Send a resource command to a specific client or all clients.
         * Helper method used by the client control functions.
         *
         * @param commandType The command type (Start=0, Stop=1, Restart=2, Reload=3)
         * @param resourceName Name of the resource
         * @param guid Optional client GUID (0 = all clients)
         * @return Number of clients command was sent to
         */
        static size_t SendResourceCommand(uint8_t commandType, const std::string &resourceName, uint64_t guid = 0) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!manager || !net) {
                return 0;
            }

            // Get resource info for version and hash
            std::string version = "";
            uint32_t hash = 0;

            const Resource *resource = manager->GetResource(resourceName);
            if (resource) {
                version = resource->GetManifest().version;
                hash = resource->GetContentHash();
            }

            Framework::Networking::Messages::ResourceCommandMessage msg;
            msg.FromParameters(static_cast<Framework::Networking::Messages::ResourceCommandType>(commandType), resourceName, version, hash);

            if (guid == 0) {
                // Broadcast to all clients
                net->Send(msg, SLNet::UNASSIGNED_RAKNET_GUID);
                return net->GetPeer()->NumberOfConnections();
            }
            else {
                // Send to specific client
                SLNet::RakNetGUID targetGuid;
                targetGuid.g = guid;
                net->Send(msg, targetGuid);
                return 1;
            }
        }

        /**
         * Start a resource on clients.
         *
         * @param name Resource name
         * @param guid Optional client GUID (0 or nil = all clients)
         * @return Number of clients command was sent to
         */
        static size_t ClientStart(const std::string &name, sol::optional<uint64_t> guid) {
            return SendResourceCommand(
                static_cast<uint8_t>(Framework::Networking::Messages::ResourceCommandType::Start),
                name, guid.value_or(0));
        }

        /**
         * Stop a resource on clients.
         *
         * @param name Resource name
         * @param guid Optional client GUID (0 or nil = all clients)
         * @return Number of clients command was sent to
         */
        static size_t ClientStop(const std::string &name, sol::optional<uint64_t> guid) {
            return SendResourceCommand(
                static_cast<uint8_t>(Framework::Networking::Messages::ResourceCommandType::Stop),
                name, guid.value_or(0));
        }

        /**
         * Restart a resource on clients.
         *
         * @param name Resource name
         * @param guid Optional client GUID (0 or nil = all clients)
         * @return Number of clients command was sent to
         */
        static size_t ClientRestart(const std::string &name, sol::optional<uint64_t> guid) {
            return SendResourceCommand(
                static_cast<uint8_t>(Framework::Networking::Messages::ResourceCommandType::Restart),
                name, guid.value_or(0));
        }

        /**
         * Reload a resource on clients (hot-reload without restart).
         *
         * @param name Resource name
         * @param guid Optional client GUID (0 or nil = all clients)
         * @return Number of clients command was sent to
         */
        static size_t ClientReload(const std::string &name, sol::optional<uint64_t> guid) {
            return SendResourceCommand(
                static_cast<uint8_t>(Framework::Networking::Messages::ResourceCommandType::Reload),
                name, guid.value_or(0));
        }

        /**
         * Send the current resource list to clients.
         *
         * @param guid Optional client GUID (0 or nil = all clients)
         * @return Number of clients the list was sent to
         */
        static size_t SendResourceList(sol::optional<uint64_t> guid) {
            auto *net = Framework::CoreModules::GetNetworkPeer();
            auto *manager = Framework::CoreModules::GetResourceManager();

            if (!net || !manager) {
                return 0;
            }

            // Build resource list message
            Framework::Networking::Messages::ResourceListMessage msg;
            auto resourceNames = manager->GetAllResourceNames();

            for (const auto &name : resourceNames) {
                const Resource *resource = manager->GetResource(name);
                if (!resource) {
                    continue;
                }

                const auto &manifest = resource->GetManifest();
                // Only include resources with client files
                if (!manifest.clientFiles.empty()) {
                    msg.AddResource(manifest.name, manifest.version, resource->GetContentHash());
                }
            }

            if (guid.value_or(0) == 0) {
                // Broadcast to all clients
                net->Send(msg, SLNet::UNASSIGNED_RAKNET_GUID);
                return net->GetPeer()->NumberOfConnections();
            }
            else {
                // Send to specific client
                SLNet::RakNetGUID targetGuid;
                targetGuid.g = guid.value();
                net->Send(msg, targetGuid);
                return 1;
            }
        }

        /**
         * Check if the current context is server-side.
         *
         * @return True if running on server
         */
        static bool IsServer() {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return false;
            }
            return !manager->GetConfig().isClient;
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

            cls["setHealthCheck"]       = &ResourceBuiltin::SetHealthCheck;
            cls["isHealthy"]            = &ResourceBuiltin::IsHealthy;
            cls["getErrorMessage"]      = &ResourceBuiltin::GetErrorMessage;
            cls["getRestartAttempts"]   = &ResourceBuiltin::GetRestartAttempts;
            cls["canAutoRestart"]       = &ResourceBuiltin::CanAutoRestart;
            cls["clearRestartAttempts"] = &ResourceBuiltin::ClearRestartAttempts;

            // Context check
            cls["isServer"] = &ResourceBuiltin::IsServer;
        }

        /**
         * Register server-only functions for client resource control.
         * Call this after Register() on server-side only.
         *
         * @param luaEngine Pointer to the Lua state
         */
        static void RegisterServer(sol::state *luaEngine) {
            // Get the existing Resource table
            sol::table resourceTable = (*luaEngine)["Resource"];

            resourceTable["clientStart"]       = &ResourceBuiltin::ClientStart;
            resourceTable["clientStop"]        = &ResourceBuiltin::ClientStop;
            resourceTable["clientRestart"]     = &ResourceBuiltin::ClientRestart;
            resourceTable["clientReload"]      = &ResourceBuiltin::ClientReload;
            resourceTable["sendResourceList"]  = &ResourceBuiltin::SendResourceList;
        }
    };

} // namespace Framework::Scripting::Builtins
