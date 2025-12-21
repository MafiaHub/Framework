/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/client_engine.h"

MODULE(client_engine, {
    using namespace Framework::Scripting;

    IT("can allocate and deallocate a valid client engine instance", {
        ClientEngine *pEngine = new ClientEngine;

        EQUALS(pEngine->Init(nullptr), EngineError::ENGINE_NONE);
        NEQUALS(pEngine->GetLuaEngine(), nullptr);
        NEQUALS(pEngine->GetLuaEngine()->lua_state(), nullptr);

        EQUALS(pEngine->Shutdown(), EngineError::ENGINE_NONE);
        delete pEngine;
    });

    IT("invokes SDK register callback during init", {
        ClientEngine engine;
        bool callbackInvoked = false;
        Engine *callbackEngine = nullptr;

        auto callback = [&callbackInvoked, &callbackEngine](SDKRegisterWrapper<Engine> sdk) {
            callbackInvoked = true;
            callbackEngine = sdk.GetEngine();
        };

        EQUALS(engine.Init(callback), EngineError::ENGINE_NONE);
        EQUALS(callbackInvoked, true);
        NEQUALS(callbackEngine, nullptr);

        engine.Shutdown();
    });

    IT("loads standard Lua libraries", {
        ClientEngine engine;
        EQUALS(engine.Init(nullptr), EngineError::ENGINE_NONE);

        auto lua = engine.GetLuaEngine();

        bool hasBase = false;
        bool hasString = false;
        bool hasTable = false;
        bool hasMath = false;
        bool hasCoroutine = false;
        bool hasUtf8 = false;
        bool hasPackage = false;
        std::string stringUpperResult;
        int tableSize = 0;
        int mathResult = 0;

        {
            auto result = lua->safe_script("return type(print)", sol::script_pass_on_error);
            hasBase = result.valid() && result.get<std::string>() == "function";
        }
        {
            auto result = lua->safe_script("return string.upper('hello')", sol::script_pass_on_error);
            if (result.valid()) {
                hasString = true;
                stringUpperResult = result.get<std::string>();
            }
        }
        {
            auto result = lua->safe_script("local t = {1,2,3}; table.insert(t, 4); return #t", sol::script_pass_on_error);
            if (result.valid()) {
                hasTable = true;
                tableSize = result.get<int>();
            }
        }
        {
            auto result = lua->safe_script("return math.abs(-5)", sol::script_pass_on_error);
            if (result.valid()) {
                hasMath = true;
                mathResult = result.get<int>();
            }
        }
        {
            auto result = lua->safe_script("return type(coroutine.create)", sol::script_pass_on_error);
            hasCoroutine = result.valid() && result.get<std::string>() == "function";
        }
        {
            auto result = lua->safe_script("return type(utf8.len)", sol::script_pass_on_error);
            hasUtf8 = result.valid() && result.get<std::string>() == "function";
        }
        {
            auto result = lua->safe_script("return type(package)", sol::script_pass_on_error);
            hasPackage = result.valid() && result.get<std::string>() == "table";
        }

        engine.Shutdown();

        EQUALS(hasBase, true);
        EQUALS(hasString, true);
        STREQUALS(stringUpperResult.c_str(), "HELLO");
        EQUALS(hasTable, true);
        EQUALS(tableSize, 4);
        EQUALS(hasMath, true);
        EQUALS(mathResult, 5);
        EQUALS(hasCoroutine, true);
        EQUALS(hasUtf8, true);
        EQUALS(hasPackage, true);
    });

    IT("can execute basic Lua code", {
        ClientEngine engine;
        EQUALS(engine.Init(nullptr), EngineError::ENGINE_NONE);

        auto lua = engine.GetLuaEngine();

        int arithmeticResult = 0;
        int variableResult = 0;
        int functionResult = 0;
        int tableResult = 0;

        {
            auto result = lua->safe_script("return 2 + 2", sol::script_pass_on_error);
            if (result.valid()) {
                arithmeticResult = result.get<int>();
            }
        }
        {
            auto result = lua->safe_script("local x = 10; return x * 2", sol::script_pass_on_error);
            if (result.valid()) {
                variableResult = result.get<int>();
            }
        }
        {
            auto result = lua->safe_script("local function add(a, b) return a + b end; return add(3, 7)", sol::script_pass_on_error);
            if (result.valid()) {
                functionResult = result.get<int>();
            }
        }
        {
            auto result = lua->safe_script("local t = {a = 1, b = 2}; return t.a + t.b", sol::script_pass_on_error);
            if (result.valid()) {
                tableResult = result.get<int>();
            }
        }

        engine.Shutdown();

        EQUALS(arithmeticResult, 4);
        EQUALS(variableResult, 20);
        EQUALS(functionResult, 10);
        EQUALS(tableResult, 3);
    });

    IT("shutdown is idempotent", {
        ClientEngine engine;
        EQUALS(engine.Init(nullptr), EngineError::ENGINE_NONE);

        EQUALS(engine.Shutdown(), EngineError::ENGINE_NONE);
        EQUALS(engine.Shutdown(), EngineError::ENGINE_NONE);
    });

    IT("update does not crash when not initialized", {
        ClientEngine engine;
        engine.Update();
    });

    IT("update does not crash after shutdown", {
        ClientEngine engine;
        EQUALS(engine.Init(nullptr), EngineError::ENGINE_NONE);
        EQUALS(engine.Shutdown(), EngineError::ENGINE_NONE);

        engine.Update();
    });

    IT("registers common SDK during init", {
        ClientEngine engine;
        EQUALS(engine.Init(nullptr), EngineError::ENGINE_NONE);

        auto lua = engine.GetLuaEngine();

        bool hasEvent = false;
        bool hasVector3 = false;
        bool hasJSON = false;

        {
            auto result = lua->safe_script("return type(Event)", sol::script_pass_on_error);
            hasEvent = result.valid() && result.get<std::string>() == "table";
        }
        {
            auto result = lua->safe_script("return type(Vector3)", sol::script_pass_on_error);
            hasVector3 = result.valid() && result.get<std::string>() == "table";
        }
        {
            auto result = lua->safe_script("return type(JSON)", sol::script_pass_on_error);
            hasJSON = result.valid() && result.get<std::string>() == "table";
        }

        engine.Shutdown();

        EQUALS(hasEvent, true);
        EQUALS(hasVector3, true);
        EQUALS(hasJSON, true);
    });

    IT("invokes unload proc on shutdown", {
        ClientEngine engine;
        bool unloadCalled = false;

        engine.SetOnUnloadProc([&unloadCalled]() {
            unloadCalled = true;
        });

        EQUALS(engine.Init(nullptr), EngineError::ENGINE_NONE);
        EQUALS(unloadCalled, false);

        EQUALS(engine.Shutdown(), EngineError::ENGINE_NONE);
        EQUALS(unloadCalled, true);
    });
})
