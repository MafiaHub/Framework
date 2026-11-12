/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_test_helpers.h"

#include "scripting/bind/sol.h"
#include "scripting/lua/lua_runtime.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace SolSmokeTest {
    struct Counter final {
        int value = 0;

        int Add(int amount) {
            value += amount;
            return value;
        }

        int GetValue() const {
            return value;
        }

        void SetValue(int next) {
            value = next;
        }
    };

    // Throws from its constructor and counts what is destroyed.
    struct Thrower final {
        static inline int destroyed = 0;

        explicit Thrower(int) {
            throw std::runtime_error("no object");
        }

        ~Thrower() {
            ++destroyed;
        }
    };

    inline int PushThrower(lua_State *L) {
        bool threw = false;
        try {
            sol::stack::push<Thrower>(L, 1);
        }
        catch (const std::exception &) {
            threw = true;
        }
        lua_pushboolean(L, threw ? 1 : 0);
        return 1;
    }

    inline bool g_unwound = false;

    struct UnwindFlag final {
        ~UnwindFlag() {
            g_unwound = true;
        }
    };

    inline Framework::Scripting::LuaRuntimeOptions Options() {
        Framework::Scripting::LuaRuntimeOptions options;
        options.resourceName = "sol_smoke";
        return options;
    }

    // How many globals of the state have a name that starts with "sol.".
    inline int CountSolGlobals(lua_State *L) {
        int count = 0;
        lua_pushglobaltable(L);
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING && std::string(lua_tostring(L, -2)).rfind("sol.", 0) == 0) {
                ++count;
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return count;
    }
} // namespace SolSmokeTest

MODULE(sol_smoke, {
    using namespace Framework::Scripting;

    IT("builds against Lua 5.5 compiled as C++", {
        EQUALS(SOL_LUA_VERSION, 505);
        EQUALS(LUA_VERSION_NUM, 505);
    });

    IT("views a state a LuaRuntime made without touching its panic handler or its globals", {
        LuaRuntime runtime(SolSmokeTest::Options());
        lua_State *L = runtime.State();

        const lua_CFunction panic = lua_atpanic(L, nullptr);
        lua_atpanic(L, panic);

        sol::state_view lua(L);
        sol::stack::push(L, 42);
        EQUALS(sol::stack::check<int>(L, -1), true);
        EQUALS(sol::stack::pop<int>(L), 42);
        EQUALS(lua_gettop(L), 0);

        const lua_CFunction after = lua_atpanic(L, nullptr);
        lua_atpanic(L, after);
        EQUALS(after == panic, true);
        EQUALS(SolSmokeTest::CountSolGlobals(L), 0);
        // The sandbox is as it was: sol opened no library.
        EQUALS(LuaTest::Eval(L, "io == nil and debug == nil and dofile == nil"), "true");
    });

    IT("binds a usertype with a method and a property", {
        LuaRuntime runtime(SolSmokeTest::Options());
        lua_State *L = runtime.State();
        sol::state_view lua(L);

        lua.new_usertype<SolSmokeTest::Counter>("Counter", "add", &SolSmokeTest::Counter::Add, "value", sol::property(&SolSmokeTest::Counter::GetValue, &SolSmokeTest::Counter::SetValue));

        EQUALS(LuaTest::Eval(L, "c = Counter.new(); c.value = 40; return c:add(2)"), "42");
        EQUALS(LuaTest::Eval(L, "c.value"), "42");
    });

    // Why Bind::Class does not materialise a class as a sol usertype: sol keeps what a usertype is made of in a
    // userdata that only a global of the state holds, and finds it again through that global, unchecked. A script
    // reads, replaces and drops its globals.
    IT("keeps the storage of a usertype in a global a script reaches", {
        LuaRuntime runtime(SolSmokeTest::Options());
        lua_State *L = runtime.State();
        sol::state_view lua(L);

        EQUALS(SolSmokeTest::CountSolGlobals(L), 0);
        lua.new_usertype<SolSmokeTest::Counter>("Counter", "add", &SolSmokeTest::Counter::Add);
        EQUALS(SolSmokeTest::CountSolGlobals(L), 1);
        EQUALS(LuaTest::Eval(L, "for name, value in pairs(_G) do if name:sub(1, 4) == 'sol.' then return type(value) end end"), "userdata");
    });

    IT("turns a C++ exception from a bound function into a Lua error and leaves the state usable", {
        LuaRuntime runtime(SolSmokeTest::Options());
        lua_State *L = runtime.State();
        sol::state_view lua(L);

        lua.set_function("fail", []() -> int {
            throw std::runtime_error("boom from C++");
        });

        EQUALS(LuaTest::Eval(L, "fail()"), "<error> boom from C++");
        EQUALS(LuaTest::Eval(L, "select(2, pcall(fail))"), "boom from C++");
        EQUALS(LuaTest::Eval(L, "1 + 1"), "2");
        EQUALS(lua_gettop(L), 0);
    });

    IT("runs destructors when a Lua error unwinds through a bound function", {
        LuaRuntime runtime(SolSmokeTest::Options());
        lua_State *L = runtime.State();
        sol::state_view lua(L);

        lua.set_function("raise", [](lua_State *state) -> int {
            SolSmokeTest::UnwindFlag flag;
            return luaL_error(state, "raised in Lua");
        });
        // With the safeties on sol::function is a protected call; the unsafe one is lua_call.
        lua.set_function("callback", [](sol::unsafe_function function) {
            SolSmokeTest::UnwindFlag flag;
            function();
        });

        SolSmokeTest::g_unwound = false;
        EQUALS(LuaTest::Eval(L, "raise()"), "<error> eval:1: raised in Lua");
        EQUALS(SolSmokeTest::g_unwound, true);

        // The error of a script, raised below a C++ frame that called back into Lua unprotected.
        SolSmokeTest::g_unwound = false;
        EQUALS(LuaTest::Eval(L, "callback(function() error('from script', 0) end)"), "<error> from script");
        EQUALS(SolSmokeTest::g_unwound, true);
        EQUALS(LuaTest::Eval(L, "1 + 1"), "2");
    });

    // --- What sol does differently from what the binding layer pins ---
    //
    // Each of these is a default of sol 3.5.0 that no configuration macro changes and that a test of the binding
    // layer pins the other way. They are why the part of the Lua backend named in each stays hand-written; a sol
    // that changes one of them fails here first.

    IT("accepts a userdata without a metatable as an object of any class", {
        LuaTest::State state;
        lua_State *L = state.L;

        lua_newuserdatauv(L, 4, 0);
        EQUALS(sol::stack::check<SolSmokeTest::Counter>(L, -1, sol::no_panic), true);
    });

    IT("gives a pushed object its metatable before it constructs it", {
        LuaTest::State state;
        lua_State *L = state.L;

        SolSmokeTest::Thrower::destroyed = 0;
        lua_pushcfunction(L, SolSmokeTest::PushThrower);
        EQUALS(lua_pcall(L, 0, 1, 0), LUA_OK);
        EQUALS(lua_toboolean(L, -1), 1);
        lua_settop(L, 0);
        lua_gc(L, LUA_GCCOLLECT);
        // The finalizer ran on an object no constructor finished.
        EQUALS(SolSmokeTest::Thrower::destroyed, 1);
    });

    IT("reads integers without the range of their type and refuses a float with an integral value", {
        LuaTest::State state;
        lua_State *L = state.L;

        lua_pushinteger(L, 300);
        EQUALS(sol::stack::check<uint8_t>(L, -1, sol::no_panic), true);
        lua_pushnumber(L, 2.0);
        EQUALS(sol::stack::check<int>(L, -1, sol::no_panic), false);
    });

    IT("refuses to push an unsigned integer past the largest lua_Integer", {
        LuaTest::State state;
        lua_State *L = state.L;

        // The type model has it cross as its two's complement.
        std::string error;
        try {
            sol::stack::push(L, std::numeric_limits<uint64_t>::max());
        }
        catch (const sol::error &exception) {
            error = exception.what();
        }
        EQUALS(error, "integer value will be misrepresented in lua");
    });

    IT("takes any table for a vector", {
        LuaTest::State state;
        lua_State *L = state.L;

        EQUALS(luaL_dostring(L, "return { 1, nil, 3, key = 4 }"), LUA_OK);
        EQUALS(sol::stack::check<std::vector<int>>(L, -1, sol::no_panic), true);
    });

    IT("words the error of a wrong argument and of a wrong receiver its own way", {
        LuaTest::State state;
        lua_State *L = state.L;
        sol::state_view lua(L);
        lua.new_usertype<SolSmokeTest::Counter>("Counter", "add", &SolSmokeTest::Counter::Add);

        // The binding layer pins "bad argument #1 to 'Counter:add' (integer expected, got string)" and "bad
        // receiver for 'Counter:add' (Counter expected, got number)".
        EQUALS(LuaTest::Eval(L, "Counter.new():add('x')"), "<error> eval:1: stack index 2, expected number, received string: not a numeric type that fits exactly an integer (number maybe has significant decimals) (bad argument into 'int(int)')");
        EQUALS(LuaTest::Eval(L, "Counter.add(1, 2)"), "<error> eval:1: sol: received nil for 'self' argument (use ':' for accessing member functions, make sure member variables are preceeded by the actual object with '.' syntax)");
    });

    IT("lets a script extend a class and read the metatable of an object", {
        LuaTest::State state;
        lua_State *L = state.L;
        sol::state_view lua(L);
        lua.new_usertype<SolSmokeTest::Counter>("Counter", "add", &SolSmokeTest::Counter::Add);

        // The binding layer pins a read-only class table and getmetatable(object) == false.
        EQUALS(LuaTest::Eval(L, "Counter.extra = 1; return Counter.extra"), "1");
        EQUALS(LuaTest::Eval(L, "type(getmetatable(Counter.new()))"), "table");
        EQUALS(LuaTest::Eval(L, "type(getmetatable(Counter.new()).__gc)"), "function");
    });
});
