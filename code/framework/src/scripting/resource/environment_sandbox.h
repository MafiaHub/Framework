#pragma once

#include <sol/sol.hpp>

#include <string>
#include <vector>

namespace Framework::Scripting {

    /**
     * Utility class for creating and managing sandboxed Lua environments.
     *
     * Each resource gets its own isolated environment table that:
     * - Has access to shared read-only globals (builtins like Console, JSON, math types)
     * - Can define its own globals without polluting other resources
     * - Executes scripts with the environment as _ENV
     */
    class EnvironmentSandbox final {
      public:
        /**
         * Create a new sandboxed environment for a resource.
         *
         * The environment is set up with:
         * - A metatable that falls back to the global table for reads
         * - Local writes that don't affect the global table
         * - Access to standard Lua libraries
         *
         * @param luaState The Lua state to create the environment in
         * @param resourceName Name of the resource (for debugging)
         * @return Unique pointer to the new environment
         */
        static std::unique_ptr<sol::environment> CreateEnvironment(sol::state &luaState, const std::string &resourceName);

        /**
         * Load and execute a script file within an environment.
         *
         * @param luaState The Lua state
         * @param env The sandboxed environment to execute in
         * @param scriptPath Path to the script file
         * @param outError Output: error message if execution fails
         * @return True if the script executed successfully
         */
        static bool ExecuteScript(sol::state &luaState, sol::environment &env, const std::string &scriptPath, std::string &outError);

        /**
         * Execute a Lua string within an environment.
         *
         * @param luaState The Lua state
         * @param env The sandboxed environment to execute in
         * @param code Lua code to execute
         * @param chunkName Name for the chunk (for error messages)
         * @param outError Output: error message if execution fails
         * @return True if the code executed successfully
         */
        static bool ExecuteString(sol::state &luaState, sol::environment &env, const std::string &code, const std::string &chunkName, std::string &outError);

        /**
         * Copy shared globals from the main state to an environment.
         * These are exposed as read-only (writes go to the environment's own table).
         *
         * @param luaState The Lua state
         * @param env The environment to populate
         * @param globalNames Names of globals to share
         */
        static void ShareGlobals(sol::state &luaState, sol::environment &env, const std::vector<std::string> &globalNames);

        /**
         * Get the list of standard safe globals that should be shared.
         * Excludes potentially dangerous functions like os.execute, dofile, etc.
         */
        static std::vector<std::string> GetSafeGlobalNames();

        /**
         * Get the list of builtin class names registered by the framework.
         */
        static std::vector<std::string> GetFrameworkBuiltinNames();

        /**
         * Disable dangerous Lua functions in an environment.
         * This should be called for client-side resources.
         *
         * @param env The environment to sandbox
         */
        static void DisableDangerousFunctions(sol::environment &env);

        /**
         * Set a value in an environment.
         *
         * @param env The environment
         * @param key The key/name
         * @param value The value to set
         */
        template <typename T>
        static void SetValue(sol::environment &env, const std::string &key, T &&value) {
            env.set(key, std::forward<T>(value));
        }

        /**
         * Get a value from an environment.
         *
         * @param env The environment
         * @param key The key/name
         * @return The value, or nil if not found
         */
        static sol::object GetValue(sol::environment &env, const std::string &key);

        /**
         * Check if a key exists in the environment (not in fallback globals).
         *
         * @param env The environment
         * @param key The key/name
         * @return True if the key exists directly in the environment
         */
        static bool HasOwnKey(sol::environment &env, const std::string &key);

        /**
         * Get all keys defined directly in the environment.
         *
         * @param env The environment
         * @return Vector of key names
         */
        static std::vector<std::string> GetOwnKeys(sol::environment &env);

        /**
         * Clear all values from an environment.
         *
         * @param env The environment to clear
         */
        static void ClearEnvironment(sol::environment &env);

      private:
        // Disabled function placeholder
        static int DisabledFunction(lua_State *L);
    };

} // namespace Framework::Scripting
