#include "environment_sandbox.h"

#include <logging/logger.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Framework::Scripting {

    std::vector<std::string> EnvironmentSandbox::_builtinNames;
    std::mutex EnvironmentSandbox::_builtinNamesMutex;

    std::unique_ptr<sol::environment> EnvironmentSandbox::CreateEnvironment(sol::state &luaState, const std::string &resourceName) {
        // Create a new environment with the global table as a fallback
        // sol::create_if_nil ensures reads fall through to globals
        auto env = std::make_unique<sol::environment>(luaState, sol::create, luaState.globals());

        // Store the resource name in the environment for debugging
        env->set("__RESOURCE_NAME__", resourceName);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Created sandboxed environment for resource '{}'", resourceName);

        return env;
    }

    bool EnvironmentSandbox::ExecuteScript(sol::state &luaState, sol::environment &env, const std::string &scriptPath, std::string &outError) {
        // Load the script file
        auto loadResult = luaState.load_file(scriptPath);

        if (!loadResult.valid()) {
            sol::error err = loadResult;
            outError       = std::string("Failed to load script '") + scriptPath + "': " + err.what();
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("{}", outError);
            return false;
        }

        // Get the loaded function
        sol::protected_function script = loadResult;

        // Set the environment for the script
        sol::set_environment(env, script);

        // Execute the script
        sol::protected_function_result result = script();

        if (!result.valid()) {
            sol::error err = result;
            outError       = std::string("Error executing script '") + scriptPath + "': " + err.what();
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("{}", outError);
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Executed script '{}' successfully", scriptPath);
        return true;
    }

    bool EnvironmentSandbox::ExecuteString(sol::state &luaState, sol::environment &env, const std::string &code, const std::string &chunkName, std::string &outError) {
        // Load the string
        auto loadResult = luaState.load(code, chunkName);

        if (!loadResult.valid()) {
            sol::error err = loadResult;
            outError       = std::string("Failed to load code '") + chunkName + "': " + err.what();
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("{}", outError);
            return false;
        }

        // Get the loaded function
        sol::protected_function script = loadResult;

        // Set the environment for the script
        sol::set_environment(env, script);

        // Execute the script
        sol::protected_function_result result = script();

        if (!result.valid()) {
            sol::error err = result;
            outError       = std::string("Error executing code '") + chunkName + "': " + err.what();
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("{}", outError);
            return false;
        }

        return true;
    }

    void EnvironmentSandbox::ShareGlobals(sol::state &luaState, sol::environment &env, const std::vector<std::string> &globalNames) {
        sol::table globals = luaState.globals();

        for (const auto &name : globalNames) {
            sol::object value = globals[name];
            if (value.valid() && value != sol::nil) {
                env.set(name, value);
            }
        }
    }

    std::vector<std::string> EnvironmentSandbox::GetSafeGlobalNames() {
        return {
            // Standard Lua libraries (safe subset)
            "assert",
            "error",
            "ipairs",
            "next",
            "pairs",
            "pcall",
            "print",
            "rawequal",
            "rawget",
            "rawlen",
            "rawset",
            "select",
            "tonumber",
            "tostring",
            "type",
            "unpack",
            "xpcall",
            "_VERSION",

            // Safe tables
            "coroutine",
            "math",
            "string",
            "table",
            "utf8",
        };
    }

    std::vector<std::string> EnvironmentSandbox::GetFrameworkBuiltinNames() {
        std::lock_guard<std::mutex> lock(_builtinNamesMutex);
        return _builtinNames;
    }

    void EnvironmentSandbox::RegisterBuiltinName(const std::string &name) {
        std::lock_guard<std::mutex> lock(_builtinNamesMutex);
        if (std::find(_builtinNames.begin(), _builtinNames.end(), name) == _builtinNames.end()) {
            _builtinNames.push_back(name);
        }
    }

    void EnvironmentSandbox::DisableDangerousFunctions(sol::environment &env) {
        // Create a disabled function that reports a clear error using sol2's variadic args
        auto disabledFunc = [](sol::variadic_args) -> sol::object {
            throw sol::error("This function is disabled in this environment");
        };

        // Disable dangerous global functions with clear error messages
        env.set("dofile", disabledFunc);
        env.set("loadfile", disabledFunc);
        env.set("load", disabledFunc);
        env.set("loadstring", disabledFunc);
        env.set("require", disabledFunc);

        // Disable dangerous os functions if os table exists
        sol::object osTable = env["os"];
        if (osTable.valid() && osTable.is<sol::table>()) {
            sol::table os   = osTable;
            os["execute"]   = disabledFunc;
            os["exit"]      = disabledFunc;
            os["remove"]    = disabledFunc;
            os["rename"]    = disabledFunc;
            os["setlocale"] = disabledFunc;
            os["tmpname"]   = disabledFunc;
            os["getenv"]    = disabledFunc;
        }

        // Disable dangerous io functions if io table exists
        sol::object ioTable = env["io"];
        if (ioTable.valid() && ioTable.is<sol::table>()) {
            env.set("io", disabledFunc);
        }

        // Disable debug library
        env.set("debug", disabledFunc);

        // Disable package library (prevents require workarounds)
        env.set("package", disabledFunc);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Disabled dangerous functions in environment");
    }

    sol::object EnvironmentSandbox::GetValue(sol::environment &env, const std::string &key) {
        return env[key];
    }

    bool EnvironmentSandbox::HasOwnKey(sol::environment &env, const std::string &key) {
        // Check if the key exists directly in the environment table
        // (not inherited from the metatable/globals)
        lua_State *L = env.lua_state();

        env.push(); // Push environment table
        lua_pushstring(L, key.c_str());
        lua_rawget(L, -2); // Raw get (no metatable)

        bool hasKey = !lua_isnil(L, -1);

        lua_pop(L, 2); // Pop value and table

        return hasKey;
    }

    std::vector<std::string> EnvironmentSandbox::GetOwnKeys(sol::environment &env) {
        std::vector<std::string> keys;

        lua_State *L = env.lua_state();
        env.push(); // Push environment table

        lua_pushnil(L); // First key
        while (lua_next(L, -2) != 0) {
            // Key is at -2, value is at -1
            if (lua_type(L, -2) == LUA_TSTRING) {
                keys.push_back(lua_tostring(L, -2));
            }
            lua_pop(L, 1); // Pop value, keep key for next iteration
        }

        lua_pop(L, 1); // Pop table

        return keys;
    }

    void EnvironmentSandbox::ClearEnvironment(sol::environment &env) {
        auto keys = GetOwnKeys(env);
        for (const auto &key : keys) {
            env.set(key, sol::nil);
        }
    }

} // namespace Framework::Scripting
