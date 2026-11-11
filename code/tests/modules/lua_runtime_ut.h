/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_test_helpers.h"

#include "scripting/lua/lua_runtime.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace LuaRuntimeTest {
    using Framework::Scripting::LuaRuntime;
    using Framework::Scripting::LuaRuntimeOptions;

    static_assert(!std::is_copy_constructible_v<LuaRuntime> && !std::is_copy_assignable_v<LuaRuntime>, "a LuaRuntime is not copyable");
    static_assert(!std::is_move_constructible_v<LuaRuntime> && !std::is_move_assignable_v<LuaRuntime>, "a LuaRuntime is not movable");

    inline LuaRuntimeOptions Options(size_t memoryLimitBytes = 0, int deadlineMs = 0) {
        LuaRuntimeOptions options;
        options.resourceName     = "test";
        options.memoryLimitBytes = memoryLimitBytes;
        options.entryDeadline    = std::chrono::milliseconds(deadlineMs);
        return options;
    }

    inline std::string FirstLine(const std::string &text) {
        return text.substr(0, text.find('\n'));
    }

    inline bool Contains(const std::string &text, std::string_view part) {
        return text.find(part) != std::string::npos;
    }

    // Runs `source` as a chunk. Empty when it ran, the error text otherwise.
    inline std::string Run(LuaRuntime &runtime, std::string_view source, std::string_view chunkName = "=test") {
        std::string error;
        return runtime.RunString(source, chunkName, error) ? std::string() : error;
    }

    // The value of `expression` as tostring() spells it, or "<error> " and the first line of the error.
    inline std::string Eval(LuaRuntime &runtime, const std::string &expression) {
        lua_State *L            = runtime.State();
        const std::string chunk = "return " + expression;
        std::string error;
        if (luaL_loadbufferx(L, chunk.data(), chunk.size(), "=eval", "t") != LUA_OK) {
            error = lua_tostring(L, -1);
            lua_pop(L, 1);
            return "<error> " + error;
        }
        if (!runtime.Call(0, 1, error)) {
            return "<error> " + FirstLine(error);
        }
        size_t length    = 0;
        const char *text = luaL_tolstring(L, -1, &length);
        const std::string result(text, length);
        lua_pop(L, 2);
        return result;
    }

    // The keys of the table `expression` gives, sorted and joined with commas.
    inline std::string Keys(LuaRuntime &runtime, const std::string &expression) {
        return Eval(runtime, "(function(t) local keys = {} for key in pairs(t) do keys[#keys + 1] = key end table.sort(keys) return table.concat(keys, ',') end)(" + expression + ")");
    }

    inline int AppendChunk(lua_State *, const void *data, size_t size, void *bytes) {
        if (size > 0) {
            static_cast<std::string *>(bytes)->append(static_cast<const char *>(data), size);
        }
        return 0;
    }

    // `source` compiled by an unrestricted state: a chunk a state that takes bytecode would load.
    inline std::string Bytecode(const std::string &source) {
        LuaTest::State plain;
        std::string bytes;
        if (luaL_loadbufferx(plain.L, source.data(), source.size(), "=dumped", "t") == LUA_OK) {
            lua_dump(plain.L, AppendChunk, &bytes, 0);
        }
        return bytes;
    }

    inline void SetGlobalString(LuaRuntime &runtime, const char *name, const std::string &value) {
        lua_pushlstring(runtime.State(), value.data(), value.size());
        lua_setglobal(runtime.State(), name);
    }

    // Collects what the runtime prints. The lines are shared with the handler, which the runtime keeps to its end.
    struct Printed final {
        std::shared_ptr<std::vector<std::string>> lines = std::make_shared<std::vector<std::string>>();

        explicit Printed(LuaRuntime &runtime) {
            runtime.SetPrintHandler([lines = lines](const std::string &line) {
                lines->push_back(line);
            });
        }
    };

    inline int64_t ElapsedMs(std::chrono::steady_clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    }

    static LuaRuntime *g_seenRuntime = nullptr;
    static bool g_seenOnMainThread   = false;

    // Records the runtime found from the thread that called it.
    inline int Probe(lua_State *L) {
        g_seenRuntime      = LuaRuntime::From(L);
        g_seenOnMainThread = lua_pushthread(L) == 1;
        return 0;
    }

    static std::string g_nestedError;

    // Runs its argument as a chunk in a nested entry and returns whether it ran.
    inline int Reenter(lua_State *L) {
        size_t length      = 0;
        const char *source = luaL_checklstring(L, 1, &length);
        const bool ran     = LuaRuntime::From(L)->RunString(std::string_view(source, length), "=nested", g_nestedError);
        lua_pushboolean(L, ran);
        return 1;
    }

    // Milliseconds on the clock the deadline is measured with.
    inline int NowMs(lua_State *L) {
        lua_pushinteger(L, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        return 1;
    }

    inline void SetGlobalFunction(LuaRuntime &runtime, const char *name, lua_CFunction function) {
        lua_pushcfunction(runtime.State(), function);
        lua_setglobal(runtime.State(), name);
    }
} // namespace LuaRuntimeTest

MODULE(lua_runtime, {
    using namespace LuaRuntimeTest;

    // --- Running ---

    IT("runs a chunk and keeps its globals", {
        LuaRuntime runtime(Options());
        EQUALS(Run(runtime, "x = 1 + 1"), "");
        EQUALS(Eval(runtime, "x"), "2");
        EQUALS(Eval(runtime, "math.type(x)"), "integer");
        EQUALS(lua_gettop(runtime.State()), 0);
    });

    IT("keeps the options it was created with", {
        LuaRuntimeOptions options = Options(0, 250);
        options.resourceName      = "inventory";
        options.side              = Framework::Scripting::Bind::Side::Client;
        options.rootPath          = "/resources/inventory";
        LuaRuntime runtime(options);
        EQUALS(runtime.Options().resourceName, "inventory");
        EQUALS(runtime.Options().side == Framework::Scripting::Bind::Side::Client, true);
        EQUALS(runtime.Options().rootPath, "/resources/inventory");
        EQUALS(runtime.Options().entryDeadline.count(), 250);
        EQUALS(LuaRuntimeOptions().side == Framework::Scripting::Bind::Side::Server, true);
    });

    IT("reports a syntax error with the chunk name and the line", {
        LuaRuntime runtime(Options());
        const std::string error = Run(runtime, "local a = 1\nlocal b = = 2", "@shop/main.lua");
        EQUALS(error, "shop/main.lua:2: unexpected symbol near '='");
        EQUALS(lua_gettop(runtime.State()), 0);
    });

    IT("reports a runtime error with the chunk name, the line and a traceback", {
        LuaRuntime runtime(Options());
        const std::string error = Run(runtime, "local function fail()\n  error('boom')\nend\nfail()", "@shop/main.lua");
        EQUALS(FirstLine(error), "shop/main.lua:2: boom");
        EQUALS(Contains(error, "\nstack traceback:\n"), true);
        EQUALS(Contains(error, "shop/main.lua:2: in local 'fail'"), true);
        EQUALS(Contains(error, "shop/main.lua:4: in main chunk"), true);
        EQUALS(lua_gettop(runtime.State()), 0);
    });

    IT("names a chunk as the caller spells it", {
        LuaRuntime runtime(Options());
        EQUALS(FirstLine(Run(runtime, "error('boom')", "=console")), "console:1: boom");
    });

    IT("renders an error that is not a string as tostring() would", {
        LuaRuntime runtime(Options());
        EQUALS(FirstLine(Run(runtime, "error(setmetatable({}, { __tostring = function() return 'custom' end }))")), "custom");
        STRCEQUALS(Run(runtime, "error({})").c_str(), "table: ", 7);
        EQUALS(FirstLine(Run(runtime, "error()")), "nil");
        EQUALS(Contains(Run(runtime, "error({})"), "\nstack traceback:\n"), true);
    });

    IT("keeps running chunks after one failed", {
        LuaRuntime runtime(Options());
        EQUALS(Run(runtime, "error('boom')") == "", false);
        EQUALS(Run(runtime, "y = 3"), "");
        EQUALS(Eval(runtime, "y"), "3");
    });

    IT("calls the function on the main stack and leaves its results", {
        LuaRuntime runtime(Options());
        lua_State *L = runtime.State();
        EQUALS(Run(runtime, "function add(a, b) return a + b, 'extra' end"), "");

        std::string error;
        lua_getglobal(L, "add");
        lua_pushinteger(L, 2);
        lua_pushinteger(L, 3);
        EQUALS(runtime.Call(2, 1, error), true);
        EQUALS(lua_gettop(L), 1);
        EQUALS(lua_tointeger(L, -1), 5);
        lua_pop(L, 1);

        lua_getglobal(L, "add");
        lua_pushinteger(L, 2);
        lua_pushinteger(L, 3);
        EQUALS(runtime.Call(2, 2, error), true);
        EQUALS(lua_gettop(L), 2);
        STREQUALS(lua_tostring(L, -1), "extra");
        lua_pop(L, 2);
    });

    IT("returns the error of a failed call and leaves the stack as it was before the function", {
        LuaRuntime runtime(Options());
        lua_State *L = runtime.State();
        EQUALS(Run(runtime, "function fail(what) error(what) end", "@lib.lua"), "");

        std::string error;
        lua_pushinteger(L, 7);
        lua_getglobal(L, "fail");
        lua_pushstring(L, "bad");
        EQUALS(runtime.Call(1, 1, error), false);
        EQUALS(FirstLine(error), "lib.lua:1: bad");
        EQUALS(Contains(error, "\nstack traceback:\n"), true);
        EQUALS(lua_gettop(L), 1);
        EQUALS(lua_tointeger(L, -1), 7);
    });

    // --- The sandbox ---

    IT("offers exactly the allowed globals", {
        LuaRuntime runtime(Options());
        EQUALS(Keys(runtime, "_G"), "_G,_VERSION,assert,collectgarbage,coroutine,error,getmetatable,ipairs,load,math,next,os,pairs,pcall,print,rawequal,rawget,rawlen,rawset,"
                                    "select,setmetatable,string,table,tonumber,tostring,type,utf8,warn,xpcall");
        EQUALS(Eval(runtime, "_VERSION"), "Lua 5.5");
    });

    IT("has no io, debug, package or file loading", {
        LuaRuntime runtime(Options());
        for (const char *name : {"io", "debug", "package", "require", "dofile", "loadfile", "string.dump", "os.execute", "os.getenv", "os.exit", "os.remove", "os.rename", "os.tmpname", "os.setlocale"}) {
            EQUALS(Eval(runtime, std::string("type(") + name + ")"), "nil");
        }
        EQUALS(Eval(runtime, "type(('').dump)"), "nil");
    });

    IT("cuts os down to time, clock, date and difftime", {
        LuaRuntime runtime(Options());
        EQUALS(Keys(runtime, "os"), "clock,date,difftime,time");
        EQUALS(Eval(runtime, "math.type(os.time())"), "integer");
        EQUALS(Eval(runtime, "type(os.clock())"), "number");
        EQUALS(Eval(runtime, "os.date('!%Y-%m-%d', 0)"), "1970-01-01");
        EQUALS(Eval(runtime, "os.difftime(5, 2)"), "3.0");
    });

    IT("keeps the string, table, math, utf8 and coroutine libraries", {
        LuaRuntime runtime(Options());
        EQUALS(Eval(runtime, "('%d-%s'):format(4, 'x')"), "4-x");
        EQUALS(Eval(runtime, "table.concat({ 1, 2, 3 }, '+')"), "1+2+3");
        EQUALS(Eval(runtime, "math.max(1, 9, 4)"), "9");
        EQUALS(Eval(runtime, "utf8.len('h\\u{e9}')"), "2");
        EQUALS(Eval(runtime, "coroutine.wrap(function() coroutine.yield(1) end)()"), "1");
        EQUALS(Eval(runtime, "select(2, pcall(error, 'caught'))"), "caught");
    });

    IT("loads a string chunk", {
        LuaRuntime runtime(Options());
        EQUALS(Eval(runtime, "load('return 1')()"), "1");
        EQUALS(Eval(runtime, "load('return x', '=env', 't', { x = 5 })()"), "5");
        EQUALS(Eval(runtime, "select(2, pcall(load('error(\"in chunk\")', '=named')))"), "named:1: in chunk");
        EQUALS(Eval(runtime, "select(2, load('return ='))"), "[string \"return =\"]:1: unexpected symbol near '='");
    });

    IT("loads text whatever mode the caller asks for", {
        LuaRuntime runtime(Options());
        EQUALS(Eval(runtime, "load('return 2', '=chunk', 'b')()"), "2");
        EQUALS(Eval(runtime, "load('return 3', '=chunk', 'bt')()"), "3");
    });

    IT("refuses to load a chunk that is not a string", {
        LuaRuntime runtime(Options());
        EQUALS(Eval(runtime, "load(function() return 'return 1' end)"), "<error> eval:1: load: only a string chunk can be loaded");
        EQUALS(Eval(runtime, "load(12)"), "<error> eval:1: load: only a string chunk can be loaded");
        EQUALS(Eval(runtime, "load()"), "<error> eval:1: load: only a string chunk can be loaded");
    });

    IT("refuses to load a binary chunk", {
        LuaRuntime runtime(Options());
        EQUALS(Eval(runtime, "load('\\27Lua')"), "nil");
        EQUALS(Eval(runtime, "select(2, load('\\27Lua'))"), "attempt to load a binary chunk (mode is 't')");
        EQUALS(Eval(runtime, "select(2, load('\\27Lua', '=chunk', 'b'))"), "attempt to load a binary chunk (mode is 't')");

        const std::string bytecode = Bytecode("return 1");
        LuaTest::State plain;
        EQUALS(luaL_loadbufferx(plain.L, bytecode.data(), bytecode.size(), "=dumped", "b"), LUA_OK);
        SetGlobalString(runtime, "bytecode", bytecode);
        EQUALS(Eval(runtime, "load(bytecode, '=chunk', 'b')"), "nil");
        EQUALS(Eval(runtime, "select(2, load(bytecode, '=chunk', 'b'))"), "attempt to load a binary chunk (mode is 't')");
    });

    IT("refuses to run a binary chunk", {
        LuaRuntime runtime(Options());
        const std::string bytecode = Bytecode("ran = true");
        GREATER(bytecode.size(), 4u);
        EQUALS(Contains(Run(runtime, bytecode), "attempt to load a binary chunk"), true);
        EQUALS(Contains(Run(runtime, "\x1bLua"), "attempt to load a binary chunk"), true);
        EQUALS(Eval(runtime, "ran"), "nil");
    });

    IT("answers collectgarbage('count') and nothing else", {
        LuaRuntime runtime(Options());
        EQUALS(Eval(runtime, "math.type(collectgarbage('count'))"), "float");
        const double counted = std::stod(Eval(runtime, "collectgarbage('count')")) * 1024;
        GREATER(counted, static_cast<double>(runtime.MemoryInUse()) * 0.9);
        LESSER(counted, static_cast<double>(runtime.MemoryInUse()) * 1.1);
        EQUALS(Eval(runtime, "collectgarbage('stop')"), "<error> eval:1: collectgarbage: only \"count\" is available");
        EQUALS(Eval(runtime, "collectgarbage('collect')"), "<error> eval:1: collectgarbage: only \"count\" is available");
        EQUALS(Eval(runtime, "collectgarbage()"), "<error> eval:1: collectgarbage: only \"count\" is available");
        EQUALS(Eval(runtime, "collectgarbage(1)"), "<error> eval:1: collectgarbage: only \"count\" is available");
    });

    // --- Output ---

    IT("hands print a line with its arguments joined by tabs", {
        LuaRuntime runtime(Options());
        Printed printed(runtime);
        EQUALS(Run(runtime, "print('a', 1)"), "");
        EQUALS(Run(runtime, "print()"), "");
        EQUALS(Run(runtime, "print(nil, true, 1.5, setmetatable({}, { __tostring = function() return 'spelled' end }))"), "");
        EQUALS(printed.lines->size(), 3u);
        EQUALS((*printed.lines)[0], "a\t1");
        EQUALS((*printed.lines)[1], "");
        EQUALS((*printed.lines)[2], "nil\ttrue\t1.5\tspelled");
    });

    IT("prints nothing when an argument cannot be spelled", {
        LuaRuntime runtime(Options());
        Printed printed(runtime);
        EQUALS(FirstLine(Run(runtime, "print('a', setmetatable({}, { __tostring = function() error('no text') end }))")), "test:1: no text");
        EQUALS(printed.lines->size(), 0u);
    });

    IT("prints to the scripting logger by default", {
        LuaRuntime runtime(Options());
        EQUALS(Run(runtime, "print('to the log')"), "");
        Printed printed(runtime);
        runtime.SetPrintHandler({});
        EQUALS(Run(runtime, "print('to the log again')"), "");
        EQUALS(printed.lines->size(), 0u);
    });

    IT("hands a warning to the print handler", {
        LuaRuntime runtime(Options());
        Printed printed(runtime);
        EQUALS(Run(runtime, "warn('low ', 'fuel') warn('@on') warn('@off') warn('@', 'second')"), "");
        EQUALS(printed.lines->size(), 2u);
        EQUALS((*printed.lines)[0], "warning: low fuel");
        EQUALS((*printed.lines)[1], "warning: @second");
    });

    IT("hands an error Lua cannot raise to the print handler", {
        LuaRuntime runtime(Options());
        Printed printed(runtime);
        EQUALS(Run(runtime, "setmetatable({}, { __gc = function() error('in a finalizer') end })"), "");
        lua_gc(runtime.State(), LUA_GCCOLLECT);
        EQUALS(printed.lines->size(), 1u);
        EQUALS((*printed.lines)[0], "warning: error in __gc (test:1: in a finalizer)");
    });

    // --- Finding the runtime ---

    IT("is found from its main thread and from a coroutine", {
        LuaRuntime runtime(Options());
        LuaRuntime other(Options());
        lua_State *L = runtime.State();
        EQUALS(LuaRuntime::From(L) == &runtime, true);
        EQUALS(LuaRuntime::From(other.State()) == &other, true);

        lua_State *thread = lua_newthread(L);
        EQUALS(LuaRuntime::From(thread) == &runtime, true);
        lua_pop(L, 1);

        SetGlobalFunction(runtime, "probe", Probe);
        g_seenRuntime = nullptr;
        EQUALS(Run(runtime, "coroutine.wrap(function() probe() end)()"), "");
        EQUALS(g_seenRuntime == &runtime, true);
        EQUALS(g_seenOnMainThread, false);

        EQUALS(Run(runtime, "probe()"), "");
        EQUALS(g_seenRuntime == &runtime, true);
        EQUALS(g_seenOnMainThread, true);
    });

    // --- Memory ---

    IT("accounts for the memory the state holds", {
        LuaRuntime runtime(Options());
        const size_t empty = runtime.MemoryInUse();
        GREATER(empty, 0u);
        EQUALS(Run(runtime, "blob = ('x'):rep(100000)"), "");
        const size_t holding = runtime.MemoryInUse();
        GREATEREQ(holding, empty + 100000);

        EQUALS(Run(runtime, "blob = nil"), "");
        lua_gc(runtime.State(), LUA_GCCOLLECT);
        LESSER(runtime.MemoryInUse(), holding - 90000);
    });

    IT("fails an allocation past the cap and stays usable", {
        constexpr size_t cap = 1024 * 1024;
        LuaRuntime runtime(Options(cap));
        const std::string error = Run(runtime, "local t = {} for i = 1, 1e7 do t[i] = i end");
        EQUALS(Contains(error, "not enough memory"), true);
        LESSEREQ(runtime.MemoryInUse(), cap);

        EQUALS(Run(runtime, "return 1"), "");
        EQUALS(Run(runtime, "kept = {} for i = 1, 1000 do kept[i] = i end"), "");
        EQUALS(Eval(runtime, "#kept"), "1000");
        LESSEREQ(runtime.MemoryInUse(), cap);
    });

    IT("lets a script catch the memory error and go on", {
        constexpr size_t cap = 1024 * 1024;
        LuaRuntime runtime(Options(cap));
        EQUALS(Eval(runtime, "pcall(string.rep, 'x', 4 * 1024 * 1024)"), "false");
        EQUALS(Eval(runtime, "select(2, pcall(string.rep, 'x', 4 * 1024 * 1024)) == 'not enough memory'"), "true");
        EQUALS(Eval(runtime, "#('x'):rep(1000)"), "1000");
        LESSEREQ(runtime.MemoryInUse(), cap);
    });

    IT("cannot be created under a cap smaller than an empty state", {
        size_t needed = 0;
        {
            LuaRuntime runtime(Options());
            needed = runtime.MemoryInUse();
        }

        // Every cap up to half of it, so that creation fails at each of its stages.
        size_t created = 0;
        size_t refused = 0;
        for (size_t cap = 256; cap <= needed / 2; cap += 256) {
            try {
                LuaRuntime runtime(Options(cap));
                ++created;
            }
            catch (const std::runtime_error &) {
                ++refused;
            }
        }
        EQUALS(created, 0u);
        GREATER(refused, 10u);

        LuaRuntime roomy(Options(needed * 2));
        EQUALS(Run(roomy, "x = 1"), "");
    });

    // --- Time ---

    IT("installs a hook only when there is a deadline", {
        LuaRuntime unlimited(Options());
        EQUALS(lua_gethook(unlimited.State()) == nullptr, true);
        EQUALS(lua_gethookmask(unlimited.State()), 0);

        LuaRuntime limited(Options(0, 50));
        EQUALS(lua_gethook(limited.State()) == nullptr, false);
        EQUALS(lua_gethookmask(limited.State()), LUA_MASKCOUNT);
        EQUALS(lua_gethookcount(limited.State()), 10000);
    });

    IT("aborts an entry that runs past the deadline", {
        LuaRuntime runtime(Options(0, 50));
        const auto start        = std::chrono::steady_clock::now();
        const std::string error = Run(runtime, "local n = 0\nwhile true do n = n + 1 end", "@loop.lua");
        const int64_t elapsed   = ElapsedMs(start);
        EQUALS(FirstLine(error), "loop.lua:2: script ran too long");
        EQUALS(Contains(error, "\nstack traceback:\n"), true);
        GREATEREQ(elapsed, 50);
        LESSER(elapsed, 2000);
        EQUALS(lua_gettop(runtime.State()), 0);
    });

    IT("aborts a call that runs past the deadline", {
        LuaRuntime runtime(Options(0, 50));
        lua_State *L = runtime.State();
        EQUALS(Run(runtime, "function spin() while true do end end"), "");

        std::string error;
        const auto start = std::chrono::steady_clock::now();
        lua_getglobal(L, "spin");
        EQUALS(runtime.Call(0, 0, error), false);
        LESSER(ElapsedMs(start), 2000);
        EQUALS(FirstLine(error), "test:1: script ran too long");
    });

    IT("gives every entry its own deadline", {
        LuaRuntime runtime(Options(0, 50));
        EQUALS(Contains(Run(runtime, "while true do end"), "script ran too long"), true);
        EQUALS(Run(runtime, "for i = 1, 200000 do end"), "");

        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        EQUALS(Run(runtime, "for i = 1, 200000 do end"), "");
        EQUALS(lua_gethookcount(runtime.State()), 10000);
    });

    // From here on a script spins until a second has passed, not for ever, so that a deadline that does not hold
    // fails its test.

    IT("aborts an entry that catches the deadline error", {
        LuaRuntime runtime(Options(0, 50));
        SetGlobalFunction(runtime, "now", NowMs);
        const auto start        = std::chrono::steady_clock::now();
        const std::string error = Run(runtime, "caught = 0\nlocal stop = now() + 1000\nwhile now() < stop do\n  pcall(function() while now() < stop do end end)\n  caught = caught + 1\nend");
        LESSER(ElapsedMs(start), 500);
        EQUALS(FirstLine(error), "test:5: script ran too long");
        EQUALS(Eval(runtime, "caught"), "0");
    });

    IT("aborts an entry that spends the deadline in a coroutine", {
        LuaRuntime runtime(Options(0, 50));
        SetGlobalFunction(runtime, "now", NowMs);
        const auto start        = std::chrono::steady_clock::now();
        const std::string error = Run(runtime, "local stop = now() + 1000\ncoroutine.wrap(function() while now() < stop do end end)()");
        LESSER(ElapsedMs(start), 500);
        // coroutine.wrap adds the position of its caller to the message.
        EQUALS(Contains(FirstLine(error), "test:2: script ran too long"), true);

        const auto resumed        = std::chrono::steady_clock::now();
        const std::string resumer = Run(runtime, "local stop = now() + 1000\nlocal co = coroutine.create(function() while now() < stop do end end)\nwhile now() < stop do coroutine.resume(co) end");
        LESSER(ElapsedMs(resumed), 500);
        EQUALS(FirstLine(resumer), "test:3: script ran too long");
    });

    IT("keeps the outer deadline armed after a nested entry", {
        LuaRuntime runtime(Options(0, 50));
        SetGlobalFunction(runtime, "reenter", Reenter);
        SetGlobalFunction(runtime, "now", NowMs);
        const auto start        = std::chrono::steady_clock::now();
        const std::string error = Run(runtime, "nested = reenter('inner = 1')\nlocal stop = now() + 1000\nwhile now() < stop do end");
        LESSER(ElapsedMs(start), 500);
        EQUALS(FirstLine(error), "test:3: script ran too long");
        EQUALS(Eval(runtime, "nested"), "true");
        EQUALS(Eval(runtime, "inner"), "1");
    });

    IT("measures a nested entry against the outer deadline", {
        LuaRuntime runtime(Options(0, 200));
        SetGlobalFunction(runtime, "reenter", Reenter);
        SetGlobalFunction(runtime, "now", NowMs);
        g_nestedError.clear();
        const auto start        = std::chrono::steady_clock::now();
        const std::string error = Run(runtime, "local start = now()\nwhile now() - start < 120 do end\nreenter('local stop = now() + 1000 while now() < stop do end')\nafter = true");
        const int64_t elapsed   = ElapsedMs(start);
        // A nested entry with a deadline of its own would run until 320 ms.
        GREATEREQ(elapsed, 200);
        LESSER(elapsed, 300);
        EQUALS(FirstLine(g_nestedError), "nested:1: script ran too long");
        EQUALS(Contains(error, "script ran too long"), true);
        EQUALS(Eval(runtime, "after"), "nil");
    });
});
