#pragma once

#include "core_modules.h"

#include "../resource/environment_sandbox.h"
#include "../resource/resource_manager.h"

namespace Framework::Scripting::Builtins {

    /**
     * Exports builtin for inter-resource communication.
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

        /**
         * Get the name of the resource that called the current export.
         * Useful for exported functions to know who is calling them.
         *
         * @return Caller resource name, or empty string if not in an export call
         */
        static std::string GetCaller() {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return "";
            }

            return manager->GetExportCaller();
        }

        /**
         * Get the full export call chain for debugging.
         * Returns a table of call entries, each with callerResource, targetResource, and exportName.
         *
         * @param luaState Lua state for creating the return table
         * @return Table array of call chain entries
         */
        static sol::table GetCallChain(sol::state_view luaState) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return luaState.create_table();
            }

            auto chain = manager->GetExportCallChain();
            sol::table result = luaState.create_table();

            int i = 1;
            for (const auto &entry : chain) {
                sol::table entryTable = luaState.create_table();
                entryTable["callerResource"] = entry.callerResource;
                entryTable["targetResource"] = entry.targetResource;
                entryTable["exportName"] = entry.exportName;
                result[i++] = entryTable;
            }

            return result;
        }

        /**
         * Get the current export call depth.
         *
         * @return Number of nested export calls (0 if not in an export call)
         */
        static size_t GetCallDepth() {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return 0;
            }

            return manager->GetExportCallDepth();
        }

        /**
         * Check if the code is currently executing inside an export call.
         *
         * @return True if inside an export call
         */
        static bool IsInExportCall() {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (!manager) {
                return false;
            }

            return manager->IsInExportCall();
        }

      public:
        static void Register(sol::state *luaEngine) {
            sol::usertype<Exports> cls = luaEngine->new_usertype<Exports>("Exports");
            cls["register"]            = &Exports::RegisterExport;
            cls["get"]                 = &Exports::Get;
            cls["list"]                = &Exports::List;
            cls["getCaller"]           = &Exports::GetCaller;
            cls["getCallChain"]        = &Exports::GetCallChain;
            cls["getCallDepth"]        = &Exports::GetCallDepth;
            cls["isInExportCall"]      = &Exports::IsInExportCall;
            EnvironmentSandbox::RegisterBuiltinName("Exports");
        }
    };

} // namespace Framework::Scripting::Builtins
