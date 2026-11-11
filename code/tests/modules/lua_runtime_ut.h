/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_test_helpers.h"

#include "logging/logger.h"
#include "scripting/lua/lua_runtime.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
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

    // Runs two nested entries one after the other: a loop the deadline cuts short, then a chunk that would set a
    // global. Leaves the error of the second in g_nestedError.
    inline int ReenterTwice(lua_State *L) {
        LuaRuntime *runtime = LuaRuntime::From(L);
        std::string first;
        runtime->RunString("spin()", "=first", first);
        g_nestedError.clear();
        runtime->RunString("second = true", "=second", g_nestedError);
        return 0;
    }

    // What the suffix of a cut line or error is.
    constexpr std::string_view kTruncated = " ...(truncated)";

    // Gives the state busy(), true for a second after its first call, and spin(), which loops while it is. A script
    // under test loops on these and never on `true`, so that a deadline that does not hold fails the test instead
    // of hanging it.
    inline void AddSpin(LuaRuntime &runtime) {
        SetGlobalFunction(runtime, "now", NowMs);
        Run(runtime, "local stop function busy() stop = stop or now() + 1000 return now() < stop end function spin() while busy() do end end", "=spin");
    }

    // Runs `source` in a fresh state with a 50 ms deadline and gives the first line of its error. Says so when the
    // entry was not cut short in time, and when the script got as far as setting the global `survived`.
    inline std::string RunPastDeadline(const std::string &source) {
        LuaRuntime runtime(Options(0, 50));
        AddSpin(runtime);
        SetGlobalFunction(runtime, "reenter", Reenter);
        const auto start    = std::chrono::steady_clock::now();
        std::string outcome = FirstLine(Run(runtime, source));
        if (ElapsedMs(start) >= 500) {
            outcome += " (not cut short)";
        }
        if (Eval(runtime, "survived") != "nil") {
            outcome += " (the script went on)";
        }
        return outcome;
    }

    // Breaks into a state that is still running after ten seconds, the way the Lua interpreter's Ctrl-C does, so
    // that a `while true do end` the deadline fails to stop fails its test instead of hanging the suite.
    class Watchdog final {
      public:
        explicit Watchdog(LuaRuntime &runtime)
            : _thread([this, L = runtime.State()] {
                std::unique_lock lock(_mutex);
                if (!_wake.wait_for(lock, std::chrono::seconds(10), [this] {
                        return _finished;
                    })) {
                    lua_sethook(L, Break, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
                }
            }) {}

        ~Watchdog() {
            {
                const std::lock_guard lock(_mutex);
                _finished = true;
            }
            _wake.notify_one();
            _thread.join();
        }

      private:
        static void Break(lua_State *L, lua_Debug *) {
            lua_sethook(L, nullptr, 0, 0);
            luaL_error(L, "watchdog: the entry was still running");
        }

        std::mutex _mutex;
        std::condition_variable _wake;
        bool _finished = false;
        std::thread _thread;
    };

    // The lines the scripting logger writes while this lives, with logging switched on for that long.
    class LogCapture final {
      public:
        LogCapture() {
            auto *logging = Framework::Logging::GetInstance();
            logging->SetLogFolder((std::filesystem::temp_directory_path() / "framework-lua-runtime-ut").string());
            logging->SetConsoleLevel(spdlog::level::off);
            logging->PauseLogging(false);
            logging->SetLogForwarder(
                [lines = _lines](int, const std::string &name, const std::string &message) {
                    if (name == FRAMEWORK_INNER_SCRIPTING) {
                        const std::lock_guard lock(lines->mutex);
                        lines->text.push_back(message);
                    }
                },
                spdlog::level::info);
        }

        ~LogCapture() {
            auto *logging = Framework::Logging::GetInstance();
            logging->SetLogForwarder(nullptr);
            logging->PauseLogging(true);
        }

        // What was logged so far, once the log has caught up with its queue.
        std::vector<std::string> Lines() {
            const std::string marker = "LogCapture marker " + std::to_string(++_markers);
            const auto logger        = Framework::Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING);
            logger->info(marker);
            for (int i = 0; i < 5000; ++i) {
                logger->flush();
                {
                    const std::lock_guard lock(_lines->mutex);
                    if (std::find(_lines->text.begin(), _lines->text.end(), marker) != _lines->text.end()) {
                        return _lines->text;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return {};
        }

      private:
        struct Shared {
            std::mutex mutex;
            std::vector<std::string> text;
        };

        std::shared_ptr<Shared> _lines = std::make_shared<Shared>();
        int _markers                   = 0;
    };

    inline size_t CountStartingWith(const std::vector<std::string> &lines, std::string_view prefix) {
        return static_cast<size_t>(std::count_if(lines.begin(), lines.end(), [prefix](const std::string &line) {
            return line.starts_with(prefix);
        }));
    }

    // Lua's own pcall, xpcall, setmetatable and coroutine functions are replaced. Each of these must give in the
    // runtime what it gives in a state with the functions Lua ships.
    inline const std::vector<std::string> &ReplacedFunctionCases() {
        static const std::vector<std::string> cases = {
            // pcall
            "select('#', pcall(function() return 1, 2, 3 end))",
            "select(2, pcall(function(a, b) return a + b end, 2, 3))",
            "pcall(error, 'boom')",
            "select(2, pcall(error, 'boom'))",
            "type(select(2, pcall(error, {})))",
            "select(2, pcall(error))",
            "pcall()",
            "select(2, pcall(1))",
            "select(2, pcall(pcall, pcall, error, 'deep'))",
            "select(4, pcall(pcall, pcall, error, 'deep'))",
            // xpcall
            "select(2, xpcall(error, function(m) return 'handled ' .. m end, 'x'))",
            "select('#', xpcall(error, function(m) return 1, 2, 3 end, 'x'))",
            "xpcall(function() return 'fine' end, print)",
            "select(2, xpcall(function(...) return select('#', ...) end, print, 1, 2, 3))",
            "xpcall(error)",
            "xpcall(error, 1)",
            "xpcall()",
            "select(2, xpcall(error, function() error('again') end, 'x'))",
            "select(2, xpcall(function() local t = nil return t.x end, function(m) return 'h: ' .. m end))",
            // Yields through pcall and xpcall.
            "(function() local co = coroutine.wrap(function() return pcall(function() return coroutine.yield('out') + 1 end) end) local first = co() local ok, v = co(41) return first .. ' ' .. tostring(ok) .. ' ' .. v end)()",
            "(function() local co = coroutine.wrap(function() return pcall(function() coroutine.yield() error('late') end) end) co() local ok, m = co() return tostring(ok) .. ' ' .. m end)()",
            "(function() local co = coroutine.wrap(function() return xpcall(function() coroutine.yield() error('late') end, function(m) return 'h: ' .. m end) end) co() local ok, m = co() return tostring(ok) .. ' ' .. m end)()",
            "(function() local co = coroutine.wrap(function() return xpcall(function(a) return coroutine.yield(a) * 2 end, print, 5) end) local first = co() local ok, v = co(21) return first .. ' ' .. tostring(ok) .. ' ' .. v end)()",
            // setmetatable
            "(function() local t = {} return setmetatable(t, {}) == t end)()",
            "getmetatable(setmetatable({}, nil))",
            "getmetatable(setmetatable({}, { __metatable = 'locked' }))",
            "setmetatable(setmetatable({}, { __metatable = 'locked' }), {})",
            "setmetatable(1, {})",
            "setmetatable({}, 1)",
            "setmetatable({})",
            "setmetatable()",
            "setmetatable({}, { __index = function(_, key) return key .. '!' end }).field",
            // coroutine.create and coroutine.resume
            "type(coroutine.create(print))",
            "coroutine.create(1)",
            "coroutine.create()",
            "coroutine.resume(coroutine.create(function(a) return a * 2 end), 21)",
            "select(2, coroutine.resume(coroutine.create(function(a) return a * 2 end), 21))",
            "select('#', coroutine.resume(coroutine.create(function() return 1, 2, 3 end)))",
            "select('#', coroutine.resume(coroutine.create(function() end)))",
            "select(2, coroutine.resume(coroutine.create(function() error('boom') end)))",
            "type(select(2, coroutine.resume(coroutine.create(function() error({}) end))))",
            "select(2, coroutine.resume(coroutine.create(function() error() end)))",
            "(function() local co = coroutine.create(function() end) coroutine.resume(co) return select(2, coroutine.resume(co)) end)()",
            "(function() local co co = coroutine.create(function() return select(2, coroutine.resume(co)) end) return select(2, coroutine.resume(co)) end)()",
            "coroutine.resume(1)",
            "coroutine.resume()",
            "(function() local co co = coroutine.create(function() coroutine.yield(coroutine.status(co)) end) local seen = { coroutine.status(co) } seen[#seen + 1] = select(2, coroutine.resume(co)) seen[#seen + 1] = coroutine.status(co) "
            "coroutine.resume(co) seen[#seen + 1] = coroutine.status(co) return table.concat(seen, ',') end)()",
            "(function() local co = coroutine.create(function(a) local b = coroutine.yield(a + 1) local c = coroutine.yield(b + 1) return c + 1 end) local seen = {} for i = 1, 4 do local ok, v = coroutine.resume(co, i * 10) "
            "seen[#seen + 1] = tostring(ok) .. ':' .. tostring(v) end return table.concat(seen, ',') end)()",
            "coroutine.wrap(function() return coroutine.isyieldable() end)()",
            "coroutine.wrap(function() return select(2, coroutine.running()) end)()",
            "coroutine.isyieldable()",
            // coroutine.wrap
            "type(coroutine.wrap(print))",
            "coroutine.wrap(1)",
            "coroutine.wrap(function(a) local b = coroutine.yield(a + 1) return b * 2 end)(1)",
            "(function() local sum = 0 for v in coroutine.wrap(function() for i = 1, 3 do coroutine.yield(i) end end) do sum = sum + v end return sum end)()",
            "select(2, pcall(coroutine.wrap(function() error('boom') end)))",
            "type(select(2, pcall(coroutine.wrap(function() error({}) end))))",
            "(function() local resume = coroutine.wrap(function() end) resume() return select(2, pcall(resume)) end)()",
            "(function() local resume resume = coroutine.wrap(function() return select(2, pcall(resume)) end) return resume() end)()",
            "(function() local log = {} local resume = coroutine.wrap(function() local x <close> = setmetatable({}, { __close = function() log[#log + 1] = 'closed' end }) error('boom') end) "
            "log[#log + 1] = tostring(pcall(resume)) return table.concat(log, ',') end)()",
            // coroutine.close
            "coroutine.close(coroutine.create(function() end))",
            "(function() local co = coroutine.create(function() coroutine.yield() end) coroutine.resume(co) return tostring(coroutine.close(co)) .. ' ' .. coroutine.status(co) end)()",
            "(function() local co = coroutine.create(function() error('boom') end) coroutine.resume(co) local ok, m = coroutine.close(co) return tostring(ok) .. ' ' .. m end)()",
            "(function() local log = {} local co = coroutine.create(function() local x <close> = setmetatable({}, { __close = function() log[#log + 1] = 'closed' end }) coroutine.yield() end) coroutine.resume(co) "
            "log[#log + 1] = tostring(coroutine.close(co)) return table.concat(log, ',') end)()",
            "(function() local co = coroutine.create(function() local x <close> = setmetatable({}, { __close = function() error('in close') end }) coroutine.yield() end) coroutine.resume(co) "
            "local ok, m = coroutine.close(co) return tostring(ok) .. ' ' .. m end)()",
            "(function() local co = coroutine.create(function() coroutine.close() return 'unreached' end) local ok, v = coroutine.resume(co) return tostring(ok) .. ' ' .. tostring(v) .. ' ' .. coroutine.status(co) end)()",
            "(function() local co co = coroutine.create(function() return coroutine.wrap(function() return select(2, pcall(coroutine.close, co)) end)() end) return select(2, coroutine.resume(co)) end)()",
            "coroutine.close()",
            "coroutine.close(1)",
        };
        return cases;
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

    IT("leaves as many results as the caller asks for", {
        LuaRuntime runtime(Options());
        lua_State *L = runtime.State();
        EQUALS(Run(runtime, "function few() return 1, 2 end"), "");

        std::string error;
        lua_getglobal(L, "few");
        EQUALS(runtime.Call(0, 200, error), true);
        EQUALS(lua_gettop(L), 200);
        EQUALS(lua_tointeger(L, 2), 2);
        EQUALS(lua_isnil(L, 200), 1);
        lua_settop(L, 0);
    });

    IT("turns a Lua error raised outside a protected call into an exception", {
        LuaRuntime runtime(Options());
        lua_State *L = runtime.State();
        std::string caught;
        lua_pushstring(L, "raised by host code");
        try {
            lua_error(L);
        }
        catch (const std::runtime_error &exception) {
            caught = exception.what();
        }
        EQUALS(caught, "LuaRuntime: unprotected Lua error: raised by host code");
    });

    IT("lets host code read what a script owns without running the script", {
        LuaRuntime runtime(Options());
        lua_State *L = runtime.State();
        EQUALS(Run(runtime, "setmetatable(_G, { __index = function(_, name) error('script code ran for ' .. name) end })"), "");

        lua_pushglobaltable(L);
        lua_pushstring(L, "missing");
        const int type = lua_rawget(L, -2);
        EQUALS(type, LUA_TNIL);
        lua_pop(L, 2);

        // The plain read runs the metamethod, outside any entry: no protected call, no deadline.
        std::string caught;
        try {
            lua_getglobal(L, "missing");
        }
        catch (const std::runtime_error &exception) {
            caught = exception.what();
        }
        EQUALS(caught, "LuaRuntime: unprotected Lua error: test:1: script code ran for missing");
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

    IT("offers exactly the library functions this was written against", {
        // A function a later Lua adds shows up here, to be looked at before a script can call it.
        LuaRuntime runtime(Options());
        EQUALS(Keys(runtime, "string"), "byte,char,find,format,gmatch,gsub,len,lower,match,pack,packsize,rep,reverse,sub,unpack,upper");
        EQUALS(Keys(runtime, "table"), "concat,create,insert,move,pack,remove,sort,unpack");
        EQUALS(Keys(runtime, "coroutine"), "close,create,isyieldable,resume,running,status,wrap,yield");
        EQUALS(Keys(runtime, "math"), "abs,acos,asin,atan,ceil,cos,deg,exp,floor,fmod,frexp,huge,ldexp,log,max,maxinteger,min,mininteger,modf,pi,rad,random,randomseed,sin,sqrt,tan,tointeger,type,ult");
        EQUALS(Keys(runtime, "utf8"), "char,charpattern,codepoint,codes,len,offset");
        EQUALS(Keys(runtime, "getmetatable('')"), "__add,__div,__idiv,__index,__mod,__mul,__pow,__sub,__unm");
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

    IT("cuts a printed line at the bound, whatever the script holds", {
        LuaRuntime runtime(Options(1024 * 1024));
        Printed printed(runtime);
        EQUALS(Run(runtime, "local s = ('x'):rep(1000) print(table.unpack(setmetatable({}, { __index = function() return s end }), 1, 5000))"), "");
        EQUALS(printed.lines->size(), 1u);
        const std::string &line = (*printed.lines)[0];
        EQUALS(line.size(), LuaRuntime::kMaxLineBytes + kTruncated.size());
        EQUALS(line.ends_with(kTruncated), true);
        EQUALS(line.starts_with(std::string(1000, 'x') + "\t" + std::string(1000, 'x') + "\t"), true);

        EQUALS(Run(runtime, "print(('y'):rep(" + std::to_string(LuaRuntime::kMaxLineBytes) + "))"), "");
        EQUALS((*printed.lines)[1], std::string(LuaRuntime::kMaxLineBytes, 'y'));
        EQUALS(Run(runtime, "print(('y'):rep(" + std::to_string(LuaRuntime::kMaxLineBytes) + "), '')"), "");
        EQUALS((*printed.lines)[2], std::string(LuaRuntime::kMaxLineBytes, 'y') + std::string(kTruncated));
    });

    IT("does not spell the arguments of print past the bound", {
        LuaRuntime runtime(Options());
        Printed printed(runtime);
        EQUALS(Run(runtime, "print(('z'):rep(5000), setmetatable({}, { __tostring = function() spelled = true return 'late' end }))"), "");
        EQUALS(printed.lines->size(), 1u);
        EQUALS(Eval(runtime, "spelled"), "nil");
    });

    IT("cuts a warning at the bound", {
        LuaRuntime runtime(Options());
        Printed printed(runtime);
        EQUALS(Run(runtime, "local piece = ('w'):rep(3000) warn(piece, piece, piece) warn('short')"), "");
        EQUALS(printed.lines->size(), 2u);
        EQUALS((*printed.lines)[0].size(), LuaRuntime::kMaxLineBytes + kTruncated.size());
        EQUALS((*printed.lines)[0].starts_with("warning: www"), true);
        EQUALS((*printed.lines)[0].ends_with(kTruncated), true);
        EQUALS((*printed.lines)[1], "warning: short");
    });

    IT("puts a space where a line of output has a control character", {
        LuaRuntime runtime(Options());
        Printed printed(runtime);
        EQUALS(Run(runtime, "print('one\\ntwo\\r\\27[31mred\\0nul\\127del', 'tab')"), "");
        EQUALS(Run(runtime, "print('csi\\u{9b}31m \\u{e9}t\\u{e9}')"), "");
        EQUALS(Run(runtime, "warn('forged\\n[admin] line')"), "");
        EQUALS(printed.lines->size(), 3u);
        EQUALS((*printed.lines)[0], "one two  [31mred nul del\ttab");
        EQUALS((*printed.lines)[1], "csi  31m \xc3\xa9t\xc3\xa9");
        EQUALS((*printed.lines)[2], "warning: forged [admin] line");
    });

    IT("cuts an error text at the bound and keeps its traceback", {
        LuaRuntime runtime(Options());
        const std::string raised = Run(runtime, "local function fail() error(('e'):rep(100000) .. '\\nsecond line', 0) end\nfail()", "@shop/main.lua");
        LESSEREQ(raised.size(), LuaRuntime::kMaxErrorBytes + kTruncated.size());
        EQUALS(raised.starts_with("eeee"), true);
        EQUALS(Contains(raised, std::string(kTruncated) + "\nstack traceback:\n"), true);
        EQUALS(Contains(raised, "shop/main.lua:2: in main chunk"), true);

        // The traceback names the function, however long a name the script gave it.
        const std::string name(20000, 'f');
        const std::string traced = Run(runtime, "local function " + name + "() error('boom') end " + name + "()");
        EQUALS(traced.size(), LuaRuntime::kMaxErrorBytes + kTruncated.size());
        EQUALS(traced.starts_with("test:1: boom\nstack traceback:\n"), true);
        EQUALS(traced.ends_with(kTruncated), true);

        // A syntax error quotes the token it stopped at.
        const std::string syntax = Run(runtime, "x = 1 '" + std::string(100000, 's') + "'");
        EQUALS(syntax.size(), LuaRuntime::kMaxErrorBytes + kTruncated.size());
        EQUALS(syntax.starts_with("test:1: unexpected symbol near ''sss"), true);
        EQUALS(syntax.ends_with(kTruncated), true);

        const std::string object = Run(runtime, "error(setmetatable({}, { __tostring = function() return ('o'):rep(100000) end }))");
        LESSEREQ(object.size(), LuaRuntime::kMaxErrorBytes + kTruncated.size());
        EQUALS(Contains(object, "\nstack traceback:\n"), true);
    });

    IT("cuts the text of an unprotected error at the bound", {
        LuaRuntime runtime(Options());
        lua_State *L = runtime.State();
        const std::string huge(100000, 'p');
        size_t length = 0;
        lua_pushlstring(L, huge.data(), huge.size());
        try {
            lua_error(L);
        }
        catch (const std::runtime_error &exception) {
            length = std::string_view(exception.what()).size();
        }
        GREATER(length, 1000u);
        LESSER(length, LuaRuntime::kMaxErrorBytes + 100);
    });

    IT("writes to the log at most the lines a second allows, and says once that it dropped some", {
        LogCapture log;
        LuaRuntimeOptions options = Options();
        options.resourceName      = "flood";
        LuaRuntime runtime(options);
        LuaRuntime quiet(Options());

        EQUALS(Run(runtime, "for i = 1, 1000 do print('line ' .. i) end warn('also counted')"), "");
        EQUALS(Run(quiet, "print('from another state')"), "");
        std::vector<std::string> lines = log.Lines();
        EQUALS(CountStartingWith(lines, "[flood] line "), static_cast<size_t>(LuaRuntime::kMaxLinesPerSecond));
        EQUALS(CountStartingWith(lines, "[flood] line 200"), 1u);
        EQUALS(CountStartingWith(lines, "[flood] line 201"), 0u);
        EQUALS(CountStartingWith(lines, "[flood] warning"), 0u);
        EQUALS(CountStartingWith(lines, "[flood] output suppressed"), 1u);
        EQUALS(CountStartingWith(lines, "[test] from another state"), 1u);

        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        EQUALS(Run(runtime, "print('a second later')"), "");
        lines = log.Lines();
        EQUALS(CountStartingWith(lines, "[flood] a second later"), 1u);
        EQUALS(CountStartingWith(lines, "[flood] output suppressed"), 1u);
    });

    IT("hands an error Lua cannot raise to the print handler", {
        LuaRuntime runtime(Options());
        Printed printed(runtime);
        // A finalizer only host code can set.
        lua_State *L = runtime.State();
        lua_newtable(L);
        lua_newtable(L);
        EQUALS(luaL_loadstring(L, "error('in a finalizer')"), LUA_OK);
        lua_setfield(L, -2, "__gc");
        lua_setmetatable(L, -2);
        lua_pop(L, 1);
        lua_gc(L, LUA_GCCOLLECT);
        EQUALS(printed.lines->size(), 1u);
        EQUALS((*printed.lines)[0], "warning: error in __gc ([string \"error('in a finalizer')\"]:1: in a finalizer)");
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

    IT("refuses the buffer of a string function at the cap without collecting first", {
        // The limit lua_runtime.h describes. Lua 5.5 allocates the result of string.rep, table.concat and their
        // like outside its collector's reach, so the garbage of earlier rounds is still there when the cap is hit.
        // Should this start to pass, Lua collects first and the paragraph in the header can go.
        constexpr size_t cap = 1024 * 1024;
        LuaRuntime runtime(Options(cap));
        EQUALS(Run(runtime, "for i = 1, 50 do local s = ('x'):rep(300000) rounds = i end"), "not enough memory");
        LESSER(std::stoi(Eval(runtime, "rounds")), 50);
        LESSEREQ(runtime.MemoryInUse(), cap);

        // A string Lua allocates itself is collected in time, and the state goes on.
        EQUALS(Run(runtime, "for i = 1, 50 do local s = ('x'):rep(1000) for j = 1, 8 do s = s .. s end rounds = #s end"), "");
        EQUALS(Eval(runtime, "rounds"), "256000");
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
        size_t blamed  = 0;
        for (size_t cap = 256; cap <= needed / 2; cap += 256) {
            try {
                LuaRuntime runtime(Options(cap));
                ++created;
            }
            catch (const std::runtime_error &exception) {
                ++refused;
                blamed += Contains(exception.what(), "'test'") ? 1 : 0;
            }
        }
        EQUALS(created, 0u);
        GREATER(refused, 10u);
        EQUALS(blamed, refused);

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
        const Watchdog watchdog(runtime);
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
        const Watchdog watchdog(runtime);
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
        const Watchdog watchdog(runtime);
        EQUALS(Contains(Run(runtime, "while true do end"), "script ran too long"), true);
        EQUALS(Run(runtime, "for i = 1, 200000 do end"), "");

        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        EQUALS(Run(runtime, "for i = 1, 200000 do end"), "");
        EQUALS(lua_gethookcount(runtime.State()), 10000);
    });

    IT("keeps the outer deadline armed after a nested entry", {
        LuaRuntime runtime(Options(0, 50));
        AddSpin(runtime);
        SetGlobalFunction(runtime, "reenter", Reenter);
        const auto start        = std::chrono::steady_clock::now();
        const std::string error = Run(runtime, "nested = reenter('inner = 1')\nspin()\nsurvived = true");
        LESSER(ElapsedMs(start), 500);
        EQUALS(FirstLine(error), "spin:1: script ran too long");
        EQUALS(Eval(runtime, "nested"), "true");
        EQUALS(Eval(runtime, "inner"), "1");
        EQUALS(Eval(runtime, "survived"), "nil");
    });

    IT("measures a nested entry against the outer deadline", {
        LuaRuntime runtime(Options(0, 600));
        AddSpin(runtime);
        SetGlobalFunction(runtime, "reenter", Reenter);
        g_nestedError.clear();
        const auto start        = std::chrono::steady_clock::now();
        const std::string error = Run(runtime, "local start = now()\nwhile now() - start < 450 do end\nreenter('spin()')\nsurvived = true");
        const int64_t elapsed   = ElapsedMs(start);
        // The nested entry starts 450 ms in and ends with the outer one at 600 ms. With a deadline of its own it
        // would run until 1050 ms.
        GREATEREQ(elapsed, 600);
        LESSER(elapsed, 1000);
        EQUALS(FirstLine(g_nestedError), "spin:1: script ran too long");
        EQUALS(FirstLine(error), "spin:1: script ran too long");
        EQUALS(Eval(runtime, "survived"), "nil");
    });

    IT("does not start a nested entry once the deadline has passed", {
        LuaRuntime runtime(Options(0, 50));
        AddSpin(runtime);
        SetGlobalFunction(runtime, "reenter_twice", ReenterTwice);
        const std::string error = Run(runtime, "reenter_twice()\nsurvived = true");
        EQUALS(FirstLine(error), "spin:1: script ran too long");
        EQUALS(g_nestedError, "script ran too long");
        EQUALS(Eval(runtime, "second"), "nil");
        EQUALS(Eval(runtime, "survived"), "nil");
    });

    // Each of these could keep an entry going past its deadline with the functions Lua ships.

    IT("aborts an entry that catches the deadline error", {
        EQUALS(RunPastDeadline("spin() survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("while busy() do pcall(spin) end survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("pcall(pcall, pcall, spin) survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("while busy() do xpcall(spin, print) end survived = true"), "spin:1: script ran too long");
    });

    IT("names where the script was when its time ran out", {
        LuaRuntime runtime(Options(0, 50));
        AddSpin(runtime);
        const std::string error = Run(runtime, "local function work()\n  spin()\nend\npcall(work)", "@shop/main.lua");
        EQUALS(FirstLine(error), "spin:1: script ran too long");
        EQUALS(Contains(error, "shop/main.lua:4: in main chunk"), true);
    });

    IT("does not call an xpcall message handler once the deadline has passed", {
        EQUALS(RunPastDeadline("xpcall(spin, spin) survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("xpcall(spin, function() survived = true end)"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("xpcall(xpcall, spin, spin, spin) survived = true"), "spin:1: script ran too long");
    });

    IT("aborts an entry that spends the deadline in a coroutine", {
        EQUALS(RunPastDeadline("coroutine.wrap(spin)() survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("coroutine.resume(coroutine.create(spin)) survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("local co = coroutine.create(spin) while busy() do coroutine.resume(co) end survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("while busy() do pcall(coroutine.wrap(spin)) end survived = true"), "spin:1: script ran too long");
    });

    IT("aborts an entry that catches the deadline error across coroutines", {
        EQUALS(RunPastDeadline("local function mid() while busy() do pcall(coroutine.wrap(spin)) end end while busy() do pcall(coroutine.wrap(mid)) end survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("local function mid() while busy() do coroutine.resume(coroutine.create(spin)) end end while busy() do coroutine.resume(coroutine.create(mid)) end survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("local function tower() while busy() do pcall(coroutine.wrap(tower)) end end tower() survived = true"), "test:1: script ran too long");
    });

    IT("aborts an entry that spreads its work over coroutines too short to be counted", {
        // No thread here runs the 10 000 instructions that make its own hook look at the clock.
        const std::string outcome = RunPastDeadline("local function leaf() for i = 1, 3000 do end end\n"
                                                    "local function mid() for i = 1, 500 do coroutine.wrap(leaf)() end end\n"
                                                    "while busy() do coroutine.wrap(mid)() end survived = true");
        EQUALS(outcome.starts_with("test:") && outcome.ends_with(": script ran too long"), true);
    });

    IT("runs the __close handlers of a coroutine under the deadline", {
        // A coroutine that is closed while it waits, then coroutines the deadline killed.
        EQUALS(RunPastDeadline("local co = coroutine.create(function() local x <close> = setmetatable({}, { __close = spin }) coroutine.yield() end) coroutine.resume(co) coroutine.close(co) survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("coroutine.wrap(function() local x <close> = setmetatable({}, { __close = spin }) spin() end)() survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("local co = coroutine.create(function() local x <close> = setmetatable({}, { __close = spin }) spin() end) coroutine.resume(co) coroutine.close(co) survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("local x <close> = setmetatable({}, { __close = function() spin() survived = true end }) spin()"), "spin:1: script ran too long");
    });

    IT("leaves nothing of a coroutine the deadline killed for a later entry to run without a deadline", {
        // Lua keeps hooks off on a thread the hook kills. Its __close handlers must not be left for
        // coroutine.close to run in that state.
        LuaRuntime runtime(Options(0, 50));
        AddSpin(runtime);
        const std::string killed = Run(runtime, "co = coroutine.create(function() local x <close> = setmetatable({}, { __close = function() spin() handled = true end }) spin() end)\ncoroutine.resume(co)");
        EQUALS(FirstLine(killed), "spin:1: script ran too long");

        const auto start = std::chrono::steady_clock::now();
        EQUALS(Run(runtime, "closed, message = coroutine.close(co)"), "");
        LESSER(ElapsedMs(start), 500);
        EQUALS(Eval(runtime, "closed"), "false");
        EQUALS(Eval(runtime, "message"), "spin:1: script ran too long");
        EQUALS(Eval(runtime, "handled"), "nil");
    });

    IT("resumes no coroutine once the deadline has passed", {
        // The thread that ran out of time is stopped at every instruction, but the function coroutine.wrap gives
        // is a C function, and it is called all the same as a __close handler.
        EQUALS(RunPastDeadline("local function closing(handler) return setmetatable({}, { __close = handler }) end\n"
                               "local a <close> = closing(coroutine.wrap(function() survived = true end))\n"
                               "local b <close> = closing(function() coroutine.resume(coroutine.create(function() survived = true end)) end)\n"
                               "spin()"),
            "spin:1: script ran too long");
    });

    IT("aborts an entry that runs out of time in a callback or a metamethod", {
        EQUALS(RunPastDeadline("('x'):gsub('x', spin) survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("table.sort({ 3, 2, 1 }, spin) survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("local t = setmetatable({}, { __index = spin }) local v = t.field survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("print(setmetatable({}, { __tostring = spin })) survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("for value in coroutine.wrap(function() while busy() do coroutine.yield(1) end end) do end survived = true"), "test:1: script ran too long");
        EQUALS(RunPastDeadline("coroutine.wrap(pcall)(spin) survived = true"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("xpcall(spin, coroutine.wrap(function() survived = true end))"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("local x <close> = setmetatable({}, { __close = pcall, __call = function() survived = true end }) spin()"), "spin:1: script ran too long");
    });

    IT("aborts an entry that runs out of time spelling its own error", {
        EQUALS(RunPastDeadline("error(setmetatable({}, { __tostring = spin }))"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("pcall(error, setmetatable({}, { __tostring = spin })) spin() survived = true"), "spin:1: script ran too long");
    });

    IT("runs no __close handler of the thread that ran out of time", {
        EQUALS(RunPastDeadline("local x <close> = setmetatable({}, { __close = function() survived = true end }) spin()"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("coroutine.wrap(function() local x <close> = setmetatable({}, { __close = function() survived = true end }) spin() end)()"), "spin:1: script ran too long");
    });

    IT("lets a thread do nothing with coroutines after host code took the deadline error for it", {
        // reenter() fails at the deadline and returns as if nothing had happened, to a coroutine that is not the
        // thread that ran out of time.
        EQUALS(RunPastDeadline("coroutine.wrap(function() reenter('spin()') coroutine.resume(coroutine.create(function() survived = true end)) end)()"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("coroutine.wrap(function() reenter('spin()') coroutine.wrap(function() survived = true end)() end)()"), "spin:1: script ran too long");
        EQUALS(RunPastDeadline("local idle = coroutine.create(function() local x <close> = setmetatable({}, { __close = function() survived = true end }) coroutine.yield() end)\n"
                               "coroutine.resume(idle)\n"
                               "coroutine.wrap(function() reenter('spin()') coroutine.close(idle) end)()"),
            "spin:1: script ran too long");
        EQUALS(RunPastDeadline("coroutine.wrap(function() reenter('spin()') pcall(function() end) survived = true end)()"), "spin:1: script ran too long");
    });

    IT("works as before in the entry after one that was aborted", {
        LuaRuntime runtime(Options(0, 50));
        AddSpin(runtime);
        EQUALS(FirstLine(Run(runtime, "pcall(coroutine.wrap(spin))")), "spin:1: script ran too long");
        EQUALS(Eval(runtime, "pcall(error, 'x')"), "false");
        EQUALS(Eval(runtime, "select(2, xpcall(error, function(m) return 'handled ' .. m end, 'x'))"), "handled x");
        EQUALS(Eval(runtime, "coroutine.wrap(function(a) return coroutine.yield(a) end)(7)"), "7");
        EQUALS(Eval(runtime, "select(2, coroutine.resume(coroutine.create(function() return 8 end)))"), "8");
        EQUALS(lua_gethookcount(runtime.State()), 10000);
    });

    // --- The functions that replace Lua's ---

    IT("gives from pcall, xpcall, setmetatable and the coroutine functions what Lua's own give", {
        size_t different = 0;
        for (const std::string &expression : ReplacedFunctionCases()) {
            LuaTest::State plain;
            const std::string expected = LuaTest::Eval(plain.L, expression);
            // Without a deadline, and with one that is far away.
            for (const int deadlineMs : {0, 60000}) {
                LuaRuntime runtime(Options(0, deadlineMs));
                const std::string actual = Eval(runtime, expression);
                if (actual != expected) {
                    ++different;
                    printf("   %s\n     Lua:     %s\n     runtime: %s\n", expression.c_str(), expected.c_str(), actual.c_str());
                }
            }
        }
        EQUALS(different, 0u);
        GREATER(ReplacedFunctionCases().size(), 60u);
    });

    IT("refuses a metatable that has a finalizer", {
        for (const int deadlineMs : {0, 50}) {
            LuaRuntime runtime(Options(0, deadlineMs));
            EQUALS(Eval(runtime, "setmetatable({}, { __gc = function() end })"), "<error> eval:1: setmetatable: __gc is not available");
            EQUALS(Eval(runtime, "setmetatable({}, { __gc = false })"), "<error> eval:1: setmetatable: __gc is not available");
            EQUALS(Eval(runtime, "setmetatable({}, setmetatable({ __gc = print }, { __index = error }))"), "<error> eval:1: setmetatable: __gc is not available");
            EQUALS(Eval(runtime, "getmetatable(setmetatable({}, setmetatable({}, { __index = { __gc = print } }))).__gc == print"), "true");
        }
    });

    IT("runs no finalizer a script adds to a metatable later", {
        LuaRuntime runtime(Options());
        EQUALS(Run(runtime, "local meta = {}\nlocal object = setmetatable({}, meta)\nmeta.__gc = function() finalized = true end\nobject = nil"), "");
        lua_gc(runtime.State(), LUA_GCCOLLECT);
        lua_gc(runtime.State(), LUA_GCCOLLECT);
        EQUALS(Eval(runtime, "finalized"), "nil");
    });

    IT("closes the pending variables of a coroutine as soon as it fails", {
        // Lua's own coroutine keeps them until it is closed. Here its function runs under a protected call, which
        // closes them where the deadline still applies.
        const std::string expression = "(function() local closed = false local co = coroutine.create(function() local x <close> = setmetatable({}, { __close = function() closed = true end }) error('boom') end) "
                                       "coroutine.resume(co) return closed end)()";
        LuaTest::State plain;
        EQUALS(LuaTest::Eval(plain.L, expression), "false");
        LuaRuntime runtime(Options());
        EQUALS(Eval(runtime, expression), "true");
    });
});
