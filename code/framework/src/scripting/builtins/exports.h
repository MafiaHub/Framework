#pragma once

#include "core_modules.h"

#include "../resource/resource_manager.h"

namespace Framework::Scripting::Builtins {

    /**
     * Exports builtin for inter-resource communication.
     *
     * Lua API:
     *   Exports.register(name, value)              -- Register an export from current resource
     *   Exports.get(resourceName, exportName)      -- Get an export from another resource
     *   Exports.list(resourceName)                 -- List exports from a resource
     */
    class Exports final {
        /**
         * Register an export from the current resource.
         * The export name must be declared in the resource's manifest.
         *
         * @param name Export name
         * @param value Value to export (function or table)
         * @return True if successful
         */
        static bool RegisterExport(const std::string &name, sol::object value) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return false;
            }

            return manager->RegisterExport(name, value);
        }

        /**
         * Get an export from another resource.
         * The resource must be running for exports to be available.
         *
         * @param resourceName Name of the exporting resource
         * @param exportName Name of the export
         * @return The exported value, or nil if not found
         */
        static sol::object Get(const std::string &resourceName, const std::string &exportName) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return sol::nil;
            }

            return manager->GetExport(resourceName, exportName);
        }

        /**
         * List all registered exports from a resource.
         *
         * @param luaState Lua state for creating the return table
         * @param resourceName Name of the resource
         * @return Table array of export names
         */
        static sol::table List(sol::state_view luaState, const std::string &resourceName) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return luaState.create_table();
            }

            auto exports    = manager->ListExports(resourceName);
            sol::table result = luaState.create_table();

            int i = 1;
            for (const auto &exportName : exports) {
                result[i++] = exportName;
            }

            return result;
        }

      public:
        static void Register(sol::state *luaEngine) {
            sol::usertype<Exports> cls = luaEngine->new_usertype<Exports>("Exports");
            cls["register"]            = &Exports::RegisterExport;
            cls["get"]                 = &Exports::Get;
            cls["list"]                = &Exports::List;
        }
    };

} // namespace Framework::Scripting::Builtins
