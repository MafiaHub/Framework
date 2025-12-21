/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/resource/environment_sandbox.h"

#include <sol/sol.hpp>

MODULE(environment_sandbox, {
    using namespace Framework::Scripting;

    // ==================== CreateEnvironment ====================

    IT("creates an isolated environment", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test-resource");

        NEQUALS(env.get(), nullptr);
    });

    IT("sets __RESOURCE_NAME__ in environment", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "my-resource");

        sol::object nameObj = (*env)["__RESOURCE_NAME__"];
        EQUALS(nameObj.valid(), true);
        EQUALS(nameObj.is<std::string>(), true);
        STREQUALS(nameObj.as<std::string>().c_str(), "my-resource");
    });

    IT("environment inherits from globals for reading", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::math);

        // Set a global value
        luaState["globalValue"] = 42;

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        // Should be able to read the global through the environment
        sol::object value = (*env)["globalValue"];
        EQUALS(value.valid(), true);
        EQUALS(value.as<int>(), 42);
    });

    IT("environment writes do not pollute globals", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        // Write to environment
        (*env)["localValue"] = 123;

        // Global should NOT have this value
        sol::object globalValue = luaState["localValue"];
        EQUALS(globalValue.valid(), false);

        // Environment should have the value
        sol::object envValue = (*env)["localValue"];
        EQUALS(envValue.valid(), true);
        EQUALS(envValue.as<int>(), 123);
    });

    // ==================== ExecuteString ====================

    IT("executes Lua code in environment", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::math);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "testVar = 100", "test_chunk", error);

        EQUALS(success, true);
        EQUALS(error.empty(), true);

        sol::object value = (*env)["testVar"];
        EQUALS(value.valid(), true);
        EQUALS(value.as<int>(), 100);
    });

    IT("ExecuteString reports syntax errors", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "local x = ", "bad_chunk", error);

        EQUALS(success, false);
        NEQUALS(error.find("bad_chunk"), std::string::npos);
    });

    IT("ExecuteString reports runtime errors", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "error('test error')", "runtime_test", error);

        EQUALS(success, false);
        NEQUALS(error.find("test error"), std::string::npos);
    });

    IT("ExecuteString variables stay in environment", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env1 = EnvironmentSandbox::CreateEnvironment(luaState, "resource1");
        auto env2 = EnvironmentSandbox::CreateEnvironment(luaState, "resource2");

        std::string error;

        // Execute in env1
        EnvironmentSandbox::ExecuteString(luaState, *env1, "myVar = 'env1'", "chunk1", error);

        // Execute in env2
        EnvironmentSandbox::ExecuteString(luaState, *env2, "myVar = 'env2'", "chunk2", error);

        // Each environment should have its own value
        STREQUALS((*env1)["myVar"].get<std::string>().c_str(), "env1");
        STREQUALS((*env2)["myVar"].get<std::string>().c_str(), "env2");

        // Global should not have the value
        EQUALS(luaState["myVar"].valid(), false);
    });

    // ==================== ShareGlobals ====================

    IT("shares specified globals to environment", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        // Initially, env should have access through metatable fallback
        // but ShareGlobals copies them directly
        EnvironmentSandbox::ShareGlobals(luaState, *env, {"math", "string"});

        // Should have direct access to math and string
        sol::object mathObj = (*env)["math"];
        EQUALS(mathObj.valid(), true);
        EQUALS(mathObj.is<sol::table>(), true);

        sol::object stringObj = (*env)["string"];
        EQUALS(stringObj.valid(), true);
        EQUALS(stringObj.is<sol::table>(), true);
    });

    IT("ShareGlobals ignores non-existent globals", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        // Should not crash when sharing non-existent globals
        EnvironmentSandbox::ShareGlobals(luaState, *env, {"nonexistent_global", "another_missing"});

        // Non-existent globals should not be in environment
        EQUALS(EnvironmentSandbox::HasOwnKey(*env, "nonexistent_global"), false);
    });

    // ==================== GetSafeGlobalNames ====================

    IT("returns safe global names", {
        auto safeNames = EnvironmentSandbox::GetSafeGlobalNames();

        NEQUALS(safeNames.size(), 0u);

        // Should contain some expected safe functions
        bool hasAssert  = false;
        bool hasPairs   = false;
        bool hasMath    = false;
        bool hasString  = false;
        bool hasTable   = false;

        for (const auto &name : safeNames) {
            if (name == "assert") hasAssert = true;
            if (name == "pairs") hasPairs = true;
            if (name == "math") hasMath = true;
            if (name == "string") hasString = true;
            if (name == "table") hasTable = true;
        }

        EQUALS(hasAssert, true);
        EQUALS(hasPairs, true);
        EQUALS(hasMath, true);
        EQUALS(hasString, true);
        EQUALS(hasTable, true);
    });

    IT("safe globals do not include dangerous functions", {
        auto safeNames = EnvironmentSandbox::GetSafeGlobalNames();

        bool hasDofile   = false;
        bool hasLoadfile = false;
        bool hasOs       = false;
        bool hasIo       = false;
        bool hasDebug    = false;

        for (const auto &name : safeNames) {
            if (name == "dofile") hasDofile = true;
            if (name == "loadfile") hasLoadfile = true;
            if (name == "os") hasOs = true;
            if (name == "io") hasIo = true;
            if (name == "debug") hasDebug = true;
        }

        // These should NOT be in safe globals
        EQUALS(hasDofile, false);
        EQUALS(hasLoadfile, false);
        EQUALS(hasOs, false);
        EQUALS(hasIo, false);
        EQUALS(hasDebug, false);
    });

    // ==================== RegisterBuiltinName / GetFrameworkBuiltinNames ====================

    IT("registers and retrieves builtin names", {
        // Register some test builtins
        EnvironmentSandbox::RegisterBuiltinName("TestBuiltin1");
        EnvironmentSandbox::RegisterBuiltinName("TestBuiltin2");

        auto builtins = EnvironmentSandbox::GetFrameworkBuiltinNames();

        bool hasBuiltin1 = false;
        bool hasBuiltin2 = false;

        for (const auto &name : builtins) {
            if (name == "TestBuiltin1") hasBuiltin1 = true;
            if (name == "TestBuiltin2") hasBuiltin2 = true;
        }

        EQUALS(hasBuiltin1, true);
        EQUALS(hasBuiltin2, true);
    });

    IT("does not register duplicate builtin names", {
        size_t beforeCount = EnvironmentSandbox::GetFrameworkBuiltinNames().size();

        EnvironmentSandbox::RegisterBuiltinName("UniqueBuiltin");
        EnvironmentSandbox::RegisterBuiltinName("UniqueBuiltin"); // Duplicate

        size_t afterCount = EnvironmentSandbox::GetFrameworkBuiltinNames().size();

        // Should only have increased by 1
        EQUALS(afterCount - beforeCount, 1u);
    });

    // ==================== SetupClientSandbox ====================

    IT("disables dofile", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::SetupClientSandbox(*env);

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "dofile('test.lua')", "test", error);

        EQUALS(success, false);
        NEQUALS(error.find("disabled"), std::string::npos);
    });

    IT("disables loadfile", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::SetupClientSandbox(*env);

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "loadfile('test.lua')", "test", error);

        EQUALS(success, false);
        NEQUALS(error.find("disabled"), std::string::npos);
    });

    IT("disables load", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::SetupClientSandbox(*env);

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "load('return 1')", "test", error);

        EQUALS(success, false);
        NEQUALS(error.find("disabled"), std::string::npos);
    });

    IT("disables require", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::package);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::SetupClientSandbox(*env);

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "require('os')", "test", error);

        EQUALS(success, false);
        NEQUALS(error.find("disabled"), std::string::npos);
    });

    IT("disables os.execute when os exists", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::os);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::ShareGlobals(luaState, *env, {"os"});
        EnvironmentSandbox::SetupClientSandbox(*env);

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "os.execute('ls')", "test", error);

        EQUALS(success, false);
        NEQUALS(error.find("disabled"), std::string::npos);
    });

    IT("disables debug library", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::debug);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::ShareGlobals(luaState, *env, {"debug"});
        EnvironmentSandbox::SetupClientSandbox(*env);

        // After disabling, debug should be a function that throws when called
        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "local t = type(debug); return t ~= 'table'", "test", error);

        EQUALS(success, true);
    });

    // ==================== HasOwnKey ====================

    IT("HasOwnKey returns true for directly set keys", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        (*env)["myKey"] = "myValue";

        EQUALS(EnvironmentSandbox::HasOwnKey(*env, "myKey"), true);
    });

    IT("HasOwnKey returns false for inherited keys", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::math);

        // Set a global
        luaState["globalKey"] = 123;

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        // Can read through metatable but it's not an "own" key
        sol::object value = (*env)["globalKey"];
        EQUALS(value.valid(), true);

        // But HasOwnKey should return false since it's inherited
        EQUALS(EnvironmentSandbox::HasOwnKey(*env, "globalKey"), false);
    });

    IT("HasOwnKey returns false for non-existent keys", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        EQUALS(EnvironmentSandbox::HasOwnKey(*env, "nonexistent"), false);
    });

    // ==================== GetOwnKeys ====================

    IT("GetOwnKeys returns all directly set keys", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        (*env)["key1"] = 1;
        (*env)["key2"] = 2;
        (*env)["key3"] = 3;

        auto keys = EnvironmentSandbox::GetOwnKeys(*env);

        // Should have at least __RESOURCE_NAME__ plus our 3 keys
        GREATEREQ(keys.size(), 4u);

        bool hasKey1 = false;
        bool hasKey2 = false;
        bool hasKey3 = false;

        for (const auto &key : keys) {
            if (key == "key1") hasKey1 = true;
            if (key == "key2") hasKey2 = true;
            if (key == "key3") hasKey3 = true;
        }

        EQUALS(hasKey1, true);
        EQUALS(hasKey2, true);
        EQUALS(hasKey3, true);
    });

    IT("GetOwnKeys does not include inherited keys", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        luaState["inheritedKey"] = "inherited";

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        (*env)["ownKey"] = "own";

        auto keys = EnvironmentSandbox::GetOwnKeys(*env);

        bool hasOwnKey       = false;
        bool hasInheritedKey = false;

        for (const auto &key : keys) {
            if (key == "ownKey") hasOwnKey = true;
            if (key == "inheritedKey") hasInheritedKey = true;
        }

        EQUALS(hasOwnKey, true);
        EQUALS(hasInheritedKey, false);
    });

    // ==================== GetValue ====================

    IT("GetValue retrieves environment values", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        (*env)["testKey"] = "testValue";

        sol::object value = EnvironmentSandbox::GetValue(*env, "testKey");

        EQUALS(value.valid(), true);
        STREQUALS(value.as<std::string>().c_str(), "testValue");
    });

    IT("GetValue retrieves inherited values", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::math);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        // math should be accessible through metatable fallback
        sol::object mathObj = EnvironmentSandbox::GetValue(*env, "math");
        EQUALS(mathObj.valid(), true);
        EQUALS(mathObj.is<sol::table>(), true);
    });

    IT("GetValue returns nil for non-existent keys", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        sol::object value = EnvironmentSandbox::GetValue(*env, "nonexistent");

        // nil is valid but not truthy
        EQUALS(value == sol::nil, true);
    });

    // ==================== ClearEnvironment ====================

    IT("ClearEnvironment removes all own keys", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");

        (*env)["key1"] = 1;
        (*env)["key2"] = 2;
        (*env)["key3"] = 3;

        EQUALS(EnvironmentSandbox::HasOwnKey(*env, "key1"), true);
        EQUALS(EnvironmentSandbox::HasOwnKey(*env, "key2"), true);

        EnvironmentSandbox::ClearEnvironment(*env);

        EQUALS(EnvironmentSandbox::HasOwnKey(*env, "key1"), false);
        EQUALS(EnvironmentSandbox::HasOwnKey(*env, "key2"), false);
        EQUALS(EnvironmentSandbox::HasOwnKey(*env, "key3"), false);
        EQUALS(EnvironmentSandbox::HasOwnKey(*env, "__RESOURCE_NAME__"), false);
    });

    IT("ClearEnvironment preserves inherited globals access", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::math);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        (*env)["myKey"] = "myValue";

        EnvironmentSandbox::ClearEnvironment(*env);

        // math should still be accessible via metatable
        sol::object mathObj = (*env)["math"];
        EQUALS(mathObj.valid(), true);
    });

    // ==================== Multiple Environments Isolation ====================

    IT("multiple environments are fully isolated", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env1 = EnvironmentSandbox::CreateEnvironment(luaState, "resource1");
        auto env2 = EnvironmentSandbox::CreateEnvironment(luaState, "resource2");

        std::string error;

        // Define function in env1
        EnvironmentSandbox::ExecuteString(luaState, *env1,
            "function myFunc() return 'from env1' end\n"
            "counter = 1",
            "env1_init", error);

        // Define different function in env2
        EnvironmentSandbox::ExecuteString(luaState, *env2,
            "function myFunc() return 'from env2' end\n"
            "counter = 100",
            "env2_init", error);

        // Each environment should have its own function and counter
        EQUALS((*env1)["counter"].get<int>(), 1);
        EQUALS((*env2)["counter"].get<int>(), 100);

        // Functions should be different
        sol::protected_function func1 = (*env1)["myFunc"];
        sol::protected_function func2 = (*env2)["myFunc"];

        STREQUALS(func1().get<std::string>().c_str(), "from env1");
        STREQUALS(func2().get<std::string>().c_str(), "from env2");
    });

    IT("environments can define same-named functions independently", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base);

        auto env1 = EnvironmentSandbox::CreateEnvironment(luaState, "res1");
        auto env2 = EnvironmentSandbox::CreateEnvironment(luaState, "res2");
        auto env3 = EnvironmentSandbox::CreateEnvironment(luaState, "res3");

        std::string error;

        EnvironmentSandbox::ExecuteString(luaState, *env1, "function getData() return 1 end", "c1", error);
        EnvironmentSandbox::ExecuteString(luaState, *env2, "function getData() return 2 end", "c2", error);
        EnvironmentSandbox::ExecuteString(luaState, *env3, "function getData() return 3 end", "c3", error);

        sol::protected_function f1 = (*env1)["getData"];
        sol::protected_function f2 = (*env2)["getData"];
        sol::protected_function f3 = (*env3)["getData"];

        EQUALS(f1().get<int>(), 1);
        EQUALS(f2().get<int>(), 2);
        EQUALS(f3().get<int>(), 3);
    });

    IT("client sandbox disables require (contrast with server)", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::package);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::SetupClientSandbox(*env);

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "require('anything')", "test", error);

        EQUALS(success, false);
        // Client sandbox SHOULD show "disabled"
        EQUALS(error.find("disabled") != std::string::npos, true);
    });

    // ==================== SetupServerSandbox ====================

    IT("server sandbox allows require function", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::package);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::SetupServerSandbox(luaState, *env, ".");

        // require should be callable (not disabled)
        // Try calling it - should fail with "module not found", not "disabled"
        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "require('nonexistent')", "test", error);

        EQUALS(success, false);
        // Should NOT contain "disabled" - that would mean require was blocked
        EQUALS(error.find("disabled") == std::string::npos, true);
    });

    IT("server sandbox disables dofile", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::package);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::SetupServerSandbox(luaState, *env, ".");

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "dofile('test.lua')", "test", error);

        EQUALS(success, false);
    });

    IT("server sandbox disables loadfile", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::package);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::SetupServerSandbox(luaState, *env, ".");

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "loadfile('test.lua')", "test", error);

        EQUALS(success, false);
    });

    IT("server sandbox disables os.execute", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::os, sol::lib::package);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::ShareGlobals(luaState, *env, {"os"});
        EnvironmentSandbox::SetupServerSandbox(luaState, *env, ".");

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "os.execute('ls')", "test", error);

        EQUALS(success, false);
    });

    IT("server sandbox rejects path traversal in require", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::package);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::SetupServerSandbox(luaState, *env, ".");

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "require('../../../etc/passwd')", "test", error);

        EQUALS(success, false);
        // Error should mention path traversal
        EQUALS(error.find("path traversal") != std::string::npos, true);
    });

    IT("server sandbox rejects absolute paths in require", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::package);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::SetupServerSandbox(luaState, *env, ".");

        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "require('/etc/passwd')", "test", error);

        EQUALS(success, false);
        // Error should mention absolute paths
        EQUALS(error.find("absolute paths") != std::string::npos, true);
    });

    IT("server sandbox restricts package.path", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::package);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        // Use current directory which exists on all platforms
        EnvironmentSandbox::SetupServerSandbox(luaState, *env, ".");

        // Get the package.path from the global state (where it was modified)
        sol::table package = luaState["package"];
        std::string path = package["path"].get<std::string>();

        // Path should contain ?.lua pattern (Lua module search pattern)
        EQUALS(path.find("?.lua") != std::string::npos, true);
        // Should not contain system paths (check both - each only applies to its platform)
        // On Unix, should not contain /usr/
        EQUALS(path.find("/usr/") == std::string::npos, true);
        // On Windows, should not contain Windows system directories
        EQUALS(path.find("\\Windows\\") == std::string::npos, true);
    });

    IT("server sandbox disables package.loadlib", {
        sol::state luaState;
        luaState.open_libraries(sol::lib::base, sol::lib::package);

        auto env = EnvironmentSandbox::CreateEnvironment(luaState, "test");
        EnvironmentSandbox::SetupServerSandbox(luaState, *env, ".");

        // loadlib should be disabled
        sol::table package = luaState["package"];
        sol::object loadlib = package["loadlib"];

        // Try to call it - should throw
        std::string error;
        bool success = EnvironmentSandbox::ExecuteString(luaState, *env, "package.loadlib('test.so', 'init')", "test", error);

        EQUALS(success, false);
    });
})
