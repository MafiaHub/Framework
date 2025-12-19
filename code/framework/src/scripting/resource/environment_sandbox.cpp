#include "environment_sandbox.h"

#include <logging/logger.h>

#include <algorithm>
#include <filesystem>
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

    void EnvironmentSandbox::DisableCommonDangerousFunctions(sol::environment &env) {
        // Create a disabled function that reports a clear error
        auto disabledFunc = [](sol::variadic_args) -> sol::object {
            throw sol::error("This function is disabled in this environment");
        };

        // Disable dangerous global functions (dofile, loadfile, load, loadstring)
        env.set("dofile", disabledFunc);
        env.set("loadfile", disabledFunc);
        env.set("load", disabledFunc);
        env.set("loadstring", disabledFunc);

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

        // Disable io library
        sol::object ioTable = env["io"];
        if (ioTable.valid() && ioTable.is<sol::table>()) {
            env.set("io", disabledFunc);
        }

        // Disable debug library
        env.set("debug", disabledFunc);
    }

    void EnvironmentSandbox::SetupClientSandbox(sol::environment &env) {
        // Apply common restrictions
        DisableCommonDangerousFunctions(env);

        // Create a disabled function for client-specific restrictions
        auto disabledFunc = [](sol::variadic_args) -> sol::object {
            throw sol::error("This function is disabled in this environment");
        };

        // Disable require (clients cannot load arbitrary modules)
        env.set("require", disabledFunc);

        // Disable package library entirely (prevents require workarounds)
        env.set("package", disabledFunc);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Setup client sandbox (full lockdown)");
    }

    void EnvironmentSandbox::SetupServerSandbox(sol::state &luaState, sol::environment &env, const std::string &basePath) {
        // Apply common restrictions
        DisableCommonDangerousFunctions(env);

        // Normalize the base path to absolute
        std::filesystem::path baseDir;
        try {
            baseDir = std::filesystem::canonical(std::filesystem::path(basePath));
        } catch (const std::filesystem::filesystem_error &) {
            // If canonical fails, use absolute
            baseDir = std::filesystem::absolute(std::filesystem::path(basePath));
        }
        std::string normalizedBase = baseDir.string();

        // Create a disabled function for package restrictions
        auto disabledFunc = [](sol::variadic_args) -> sol::object {
            throw sol::error("This function is disabled in this environment");
        };

        // Setup restricted package paths for require
        // Only allow loading from the base path and its subdirectories
        sol::object packageTable = luaState["package"];
        if (packageTable.valid() && packageTable.is<sol::table>()) {
            sol::table package = packageTable;

            // Set package.path to only include the base directory
            std::string luaPath = normalizedBase + "/?.lua;" + normalizedBase + "/?/init.lua";
            package["path"] = luaPath;

            // Set package.cpath to only include the base directory (for native modules)
            #ifdef _WIN32
            std::string cPath = normalizedBase + "/?.dll";
            #elif __APPLE__
            std::string cPath = normalizedBase + "/?.so;" + normalizedBase + "/?.dylib";
            #else
            std::string cPath = normalizedBase + "/?.so";
            #endif
            package["cpath"] = cPath;

            // Disable package.loadlib (can load arbitrary native code)
            package["loadlib"] = disabledFunc;

            // Disable package.searchpath (can probe filesystem)
            package["searchpath"] = disabledFunc;
        }

        // Create a safe require wrapper that validates module names
        auto safeRequire = [normalizedBase, &luaState](const std::string &moduleName) -> sol::object {
            // Reject module names with path traversal attempts
            if (moduleName.find("..") != std::string::npos) {
                throw sol::error("Invalid module name: path traversal not allowed");
            }

            // Reject absolute paths
            if (moduleName.length() > 0 && (moduleName[0] == '/' || moduleName[0] == '\\')) {
                throw sol::error("Invalid module name: absolute paths not allowed");
            }

            #ifdef _WIN32
            // On Windows, also check for drive letters
            if (moduleName.length() >= 2 && moduleName[1] == ':') {
                throw sol::error("Invalid module name: absolute paths not allowed");
            }
            #endif

            // Call the original require
            sol::protected_function originalRequire = luaState["require"];
            if (!originalRequire.valid()) {
                throw sol::error("require function not available");
            }

            sol::protected_function_result result = originalRequire(moduleName);
            if (!result.valid()) {
                sol::error err = result;
                throw sol::error(err.what());
            }

            return result.get<sol::object>();
        };

        env.set("require", safeRequire);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Setup server sandbox with base path: {}", normalizedBase);
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
