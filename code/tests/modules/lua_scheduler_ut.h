/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_runtime_ut.h"

#include "scripting/lua/lua_runtime.h"
#include "scripting/lua/lua_scheduler.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace LuaSchedulerTest {
    using namespace LuaRuntimeTest;
    using Framework::Scripting::LuaScheduler;

    // A runtime on a clock the test moves, with note(value) to append to the global `log`, and what its error
    // handler received.
    struct Scripted final {
        LuaRuntime runtime;
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        std::chrono::steady_clock::time_point start      = std::chrono::steady_clock::time_point() + std::chrono::hours(1);

        explicit Scripted(LuaRuntimeOptions options = Options()): runtime(std::move(options)) {
            runtime.Scheduler().SetErrorHandler([errors = errors](const std::string &message) {
                errors->push_back(message);
            });
            runtime.Tick(start);
            LuaRuntimeTest::Run(runtime, "log = {} function note(value) log[#log + 1] = tostring(value) end", "=prelude");
        }

        // Ticks at `ms` after the start.
        void At(int ms) {
            runtime.Tick(start + std::chrono::milliseconds(ms));
        }

        LuaScheduler &Scheduler() {
            return runtime.Scheduler();
        }

        std::string Log() {
            return Eval(runtime, "table.concat(log, ',')");
        }

        std::string Run(std::string_view source) {
            return LuaRuntimeTest::Run(runtime, source);
        }
    };

    static uint64_t g_token = 0;

    // wait(): makes the coroutine that calls it wait, and leaves the token in g_token.
    inline int Wait(lua_State *L) {
        g_token = LuaRuntime::From(L)->Scheduler().Suspend(L, "wait");
        return LuaScheduler::Wait(L);
    }

    // settled(): waits for something that is over before it gets to wait.
    inline int Settled(lua_State *L) {
        LuaScheduler &scheduler = LuaRuntime::From(L)->Scheduler();
        const uint64_t token    = scheduler.Suspend(L, "settled");
        scheduler.Resume(token, [](lua_State *thread) {
            lua_pushstring(thread, "early");
            return 1;
        });
        return LuaScheduler::Wait(L);
    }

    // Waits without having asked for a token.
    inline int WaitOnly(lua_State *L) {
        return LuaScheduler::Wait(L);
    }

    // failed(): waits for something that has failed before it gets to wait.
    inline int Failed(lua_State *L) {
        LuaScheduler &scheduler = LuaRuntime::From(L)->Scheduler();
        scheduler.ResumeWithError(scheduler.Suspend(L, "failed"), "failed early");
        return LuaScheduler::Wait(L);
    }

    // pending(): the timers the scheduler has pending.
    inline int Pending(lua_State *L) {
        lua_pushinteger(L, static_cast<lua_Integer>(LuaRuntime::From(L)->Scheduler().PendingTimers()));
        return 1;
    }

    // What a `done` was told.
    struct Outcome final {
        int calls = 0;
        bool ok   = false;
        std::string result; // the first result, as tostring() spells it
        std::string error;
        bool hadThread = false;
    };

    inline LuaScheduler::Finished Into(std::shared_ptr<Outcome> outcome);

    static uint64_t g_waiting = 0;
    static std::shared_ptr<Outcome> g_started;

    // resume_waiting(): resumes the coroutine that waits on g_waiting, from inside the entry of its caller.
    inline int ResumeWaiting(lua_State *L) {
        LuaRuntime::From(L)->Scheduler().Resume(g_waiting, {});
        return 0;
    }

    // Gives the state wait(), and at_depth(n), which calls resume_waiting() with n entries nested in one another.
    inline void AddNesting(LuaRuntime &runtime) {
        SetGlobalFunction(runtime, "wait", Wait);
        SetGlobalFunction(runtime, "resume_waiting", ResumeWaiting);
        Run(runtime, "function at_depth(target, level) level = level or 1 if level == target then resume_waiting() else spawn(at_depth, target, level + 1) end end", "=nesting");
    }

    // Host code that takes the deadline error of a nested spin() and goes on in the state all the same: it starts
    // note('started') into g_started and resumes the coroutine that waits on g_waiting.
    inline int GoOnPastDeadline(lua_State *L) {
        LuaRuntime *runtime = LuaRuntime::From(L);
        std::string swallowed;
        runtime->RunString("spin()", "=nested", swallowed);
        lua_getglobal(L, "note");
        lua_pushstring(L, "started");
        lua_xmove(L, runtime->State(), 2);
        runtime->Scheduler().Start(1, Into(g_started));
        runtime->Scheduler().Resume(g_waiting, {});
        return 0;
    }

    inline LuaScheduler::Finished Into(std::shared_ptr<Outcome> outcome) {
        return [outcome = std::move(outcome)](bool ok, lua_State *thread, const std::string &error) {
            ++outcome->calls;
            outcome->ok        = ok;
            outcome->error     = error;
            outcome->hadThread = thread != nullptr;
            if (ok) {
                outcome->result = luaL_tolstring(thread, -1, nullptr);
                lua_pop(thread, 1);
            }
        };
    }

    // Starts the global function `name` with the integer arguments given.
    inline std::shared_ptr<Outcome> StartGlobal(Scripted &scripted, const char *name, std::initializer_list<int> arguments = {}) {
        auto outcome = std::make_shared<Outcome>();
        lua_State *L = scripted.runtime.State();
        lua_getglobal(L, name);
        for (const int argument : arguments) {
            lua_pushinteger(L, argument);
        }
        scripted.Scheduler().Start(static_cast<int>(arguments.size()), Into(outcome));
        return outcome;
    }
} // namespace LuaSchedulerTest

MODULE(lua_scheduler, {
    using namespace LuaSchedulerTest;

    // --- Timers ---

    IT("fires timeouts in the order they come due", {
        Scripted scripted;
        EQUALS(scripted.Run("setTimeout(function() note('late') end, 20)\n"
                            "setTimeout(function() note('early') end, 10)\n"
                            "setTimeout(function() note('early too') end, 10)"),
            "");
        EQUALS(scripted.Scheduler().PendingTimers(), 3u);
        scripted.At(9);
        EQUALS(scripted.Log(), "");
        scripted.At(10);
        EQUALS(scripted.Log(), "early,early too");
        EQUALS(scripted.Scheduler().PendingTimers(), 1u);
        scripted.At(25);
        EQUALS(scripted.Log(), "early,early too,late");
        EQUALS(scripted.Scheduler().PendingTimers(), 0u);
        scripted.At(100);
        EQUALS(scripted.Log(), "early,early too,late");
        EQUALS(scripted.errors->size(), 0u);
        EQUALS(lua_gettop(scripted.runtime.State()), 0);
    });

    IT("fires in one tick what came due since the last, in due order", {
        Scripted scripted;
        EQUALS(scripted.Run("setTimeout(function() note('c') end, 30)\n"
                            "setTimeout(function() note('a') end, 10)\n"
                            "setInterval(function() note('b') end, 20)"),
            "");
        scripted.At(35);
        EQUALS(scripted.Log(), "a,b,c");
    });

    IT("passes the extra arguments to the callback", {
        Scripted scripted;
        EQUALS(scripted.Run("local function show(...) local a, b, c = ... note(select('#', ...) .. tostring(a) .. tostring(b) .. tostring(c)) end\n"
                            "setTimeout(show, 10, 1, nil, 'x')\n"
                            "setTimeout(show, 10)\n"
                            "setInterval(show, 10, 'i')"),
            "");
        scripted.At(10);
        EQUALS(scripted.Log(), "31nilx,0nilnilnil,1inilnil");
        scripted.At(20);
        EQUALS(scripted.Log(), "31nilx,0nilnilnil,1inilnil,1inilnil");
    });

    IT("gives a timer a positive integer id of its own", {
        Scripted scripted;
        EQUALS(scripted.Run("a = setTimeout(function() end, 10) b = setInterval(function() end, 10) c = setTimeout(function() end, 10)"), "");
        EQUALS(Eval(scripted.runtime, "math.type(a) .. math.type(b)"), "integerinteger");
        EQUALS(Eval(scripted.runtime, "a > 0 and b > a and c > b"), "true");
        scripted.At(10);
        EQUALS(scripted.Run("d = setTimeout(function() end, 10)"), "");
        EQUALS(Eval(scripted.runtime, "d > c"), "true");
    });

    IT("refuses a callback that is not a function", {
        Scripted scripted;
        EQUALS(Eval(scripted.runtime, "setTimeout(1, 10)"), "<error> eval:1: setTimeout: callback must be a function");
        EQUALS(Eval(scripted.runtime, "setTimeout()"), "<error> eval:1: setTimeout: callback must be a function");
        EQUALS(Eval(scripted.runtime, "setInterval('note', 10)"), "<error> eval:1: setInterval: callback must be a function");
        EQUALS(Eval(scripted.runtime, "setInterval(setmetatable({}, { __call = note }), 10)"), "<error> eval:1: setInterval: callback must be a function");
        EQUALS(scripted.Scheduler().PendingTimers(), 0u);
    });

    IT("reads a delay that is missing, negative or not a number as 0", {
        Scripted scripted;
        EQUALS(scripted.Run("setTimeout(function() note('missing') end)\n"
                            "setTimeout(function() note('negative') end, -50)\n"
                            "setTimeout(function() note('text') end, 'soon')\n"
                            "setTimeout(function() note('nan') end, 0 / 0)\n"
                            "setTimeout(function() note('fraction') end, 0.9)\n"
                            "setTimeout(function() note('later') end, 1)"),
            "");
        EQUALS(scripted.Log(), "");
        scripted.At(0);
        EQUALS(scripted.Log(), "missing,negative,text,nan,fraction");
    });

    IT("takes a huge delay", {
        Scripted scripted;
        EQUALS(scripted.Run("setTimeout(function() note('far') end, math.huge) setTimeout(function() note('far') end, math.maxinteger) setInterval(function() note('far') end, 1e300)"), "");
        scripted.At(1000000);
        EQUALS(scripted.Log(), "");
        EQUALS(scripted.Scheduler().PendingTimers(), 3u);
    });

    IT("fires an interval until it is cleared from inside its callback", {
        Scripted scripted;
        EQUALS(scripted.Run("count = 0\n"
                            "id = setInterval(function() count = count + 1 if count == 3 then clearInterval(id) end end, 10)"),
            "");
        for (int ms = 5; ms <= 60; ms += 5) {
            scripted.At(ms);
        }
        EQUALS(Eval(scripted.runtime, "count"), "3");
        EQUALS(scripted.Scheduler().PendingTimers(), 0u);
        EQUALS(scripted.errors->size(), 0u);
    });

    IT("arms an interval again from the time it was due", {
        Scripted scripted;
        EQUALS(scripted.Run("count = 0 setInterval(function() count = count + 1 end, 100)"), "");
        scripted.At(99);
        EQUALS(Eval(scripted.runtime, "count"), "0");
        scripted.At(105);
        EQUALS(Eval(scripted.runtime, "count"), "1");
        scripted.At(199);
        EQUALS(Eval(scripted.runtime, "count"), "1");
        scripted.At(200);
        EQUALS(Eval(scripted.runtime, "count"), "2");
    });

    IT("fires an interval that fell behind once, and skips the periods it missed", {
        Scripted scripted;
        EQUALS(scripted.Run("count = 0 setInterval(function() count = count + 1 end, 10)"), "");
        // Due at 10, 20 and 30 by now: one firing, and the next at 40.
        for (const int ms : {35, 35, 39}) {
            scripted.At(ms);
            EQUALS(Eval(scripted.runtime, "count"), "1");
        }
        scripted.At(40);
        EQUALS(Eval(scripted.runtime, "count"), "2");
        // A tick on a period boundary fires for it, and the next is one period on.
        scripted.At(100);
        EQUALS(Eval(scripted.runtime, "count"), "3");
        scripted.At(109);
        EQUALS(Eval(scripted.runtime, "count"), "3");
        scripted.At(110);
        EQUALS(Eval(scripted.runtime, "count"), "4");
    });

    IT("counts a timeout as pending until it fires and an interval until it is cleared", {
        Scripted scripted;
        SetGlobalFunction(scripted.runtime, "pending", Pending);
        EQUALS(scripted.Run("setTimeout(function() note(pending()) end, 10)\n"
                            "id = setInterval(function() note(pending()) clearInterval(id) note(pending()) end, 10)"),
            "");
        EQUALS(scripted.Scheduler().PendingTimers(), 2u);
        scripted.At(10);
        EQUALS(scripted.Log(), "1,1,0");
    });

    IT("treats an interval shorter than a millisecond as one millisecond", {
        Scripted scripted;
        EQUALS(scripted.Run("count = 0 setInterval(function() count = count + 1 end, 0) setInterval(function() count = count + 1 end)"), "");
        scripted.At(0);
        EQUALS(Eval(scripted.runtime, "count"), "0");
        scripted.At(1);
        EQUALS(Eval(scripted.runtime, "count"), "2");
        scripted.At(2);
        EQUALS(Eval(scripted.runtime, "count"), "4");
    });

    IT("keeps what a callback arms for the next tick", {
        Scripted scripted;
        EQUALS(scripted.Run("local function again() note('fired') setTimeout(again, 0) end setTimeout(again, 0)"), "");
        scripted.At(0);
        EQUALS(scripted.Log(), "fired");
        scripted.At(0);
        EQUALS(scripted.Log(), "fired,fired");
        EQUALS(scripted.Scheduler().PendingTimers(), 1u);
    });

    IT("does not fire a timer that was cleared", {
        Scripted scripted;
        EQUALS(scripted.Run("a = setTimeout(function() note('a') end, 10)\n"
                            "b = setInterval(function() note('b') end, 10)\n"
                            "c = setTimeout(function() note('c') end, 10)\n"
                            "clearTimeout(a) clearTimeout(b)"),
            "");
        EQUALS(scripted.Scheduler().PendingTimers(), 1u);
        scripted.At(30);
        EQUALS(scripted.Log(), "c");
    });

    IT("does nothing when asked to clear what is not a pending timer", {
        Scripted scripted;
        EQUALS(scripted.Run("id = setTimeout(function() note('fired') end, 10)\n"
                            "clearTimeout(id + 1) clearTimeout(0) clearTimeout(-1) clearTimeout() clearTimeout('1') clearTimeout({}) clearInterval(nil) clearInterval(1.5)"),
            "");
        EQUALS(scripted.Scheduler().PendingTimers(), 1u);
        scripted.At(10);
        EQUALS(scripted.Log(), "fired");
        EQUALS(scripted.Run("clearTimeout(id) clearTimeout(id)"), "");
    });

    IT("lets a callback clear its own timer and one due on the same tick", {
        Scripted scripted;
        EQUALS(scripted.Run("a = setTimeout(function() note('a') clearTimeout(a) clearTimeout(b) end, 10)\n"
                            "b = setTimeout(function() note('b') end, 10)\n"
                            "c = setTimeout(function() note('c') end, 10)"),
            "");
        scripted.At(10);
        EQUALS(scripted.Log(), "a,c");
        EQUALS(scripted.Scheduler().PendingTimers(), 0u);
        EQUALS(scripted.errors->size(), 0u);
    });

    IT("refuses a timer past the limit", {
        Scripted scripted;
        EQUALS(LuaScheduler::kMaxTimers, 10000u);
        EQUALS(scripted.Run("ids = {} for i = 1, 10000 do ids[i] = setTimeout(function() fired = (fired or 0) + 1 end, 10) end"), "");
        EQUALS(scripted.Scheduler().PendingTimers(), 10000u);
        EQUALS(FirstLine(scripted.Run("setTimeout(function() end, 10)")), "test:1: setTimeout: too many timers (the limit is 10000)");
        EQUALS(FirstLine(scripted.Run("setInterval(function() end, 10)")), "test:1: setInterval: too many timers (the limit is 10000)");
        EQUALS(scripted.Scheduler().PendingTimers(), 10000u);

        // A sleep is not a timer, and a timer that is cleared or has fired leaves room.
        EQUALS(scripted.Run("spawn(function() sleep(10) note('slept') end)"), "");
        EQUALS(scripted.Run("clearTimeout(ids[1]) setTimeout(function() fired = (fired or 0) + 1 end, 10)"), "");
        scripted.At(10);
        EQUALS(Eval(scripted.runtime, "fired"), "10000");
        EQUALS(scripted.Log(), "slept");
        EQUALS(scripted.Scheduler().PendingTimers(), 0u);
        EQUALS(scripted.Run("setTimeout(function() end, 10)"), "");
    });

    IT("hands the error of a timer callback to the error handler and keeps an interval on its schedule", {
        Scripted scripted;
        EQUALS(scripted.Run("count = 0\n"
                            "local function fail() count = count + 1 error('tick ' .. count) end\n"
                            "setInterval(function() fail() end, 10)\n"
                            "setTimeout(function() note('still fired') end, 10)"),
            "");
        scripted.At(10);
        EQUALS(scripted.errors->size(), 1u);
        EQUALS(FirstLine((*scripted.errors)[0]), "test:2: tick 1");
        EQUALS(Contains((*scripted.errors)[0], "\nstack traceback:\n"), true);
        EQUALS(Contains((*scripted.errors)[0], "test:2: in upvalue 'fail'"), true);
        EQUALS(scripted.Log(), "still fired");
        scripted.At(20);
        EQUALS(scripted.errors->size(), 2u);
        EQUALS(FirstLine((*scripted.errors)[1]), "test:2: tick 2");
        EQUALS(scripted.Scheduler().PendingTimers(), 1u);
        EQUALS(lua_gettop(scripted.runtime.State()), 0);
    });

    IT("lets a timer callback wait", {
        Scripted scripted;
        EQUALS(scripted.Run("setInterval(function() note('in') sleep(25) note('out') end, 10)"), "");
        scripted.At(10);
        scripted.At(20);
        EQUALS(scripted.Log(), "in,in");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 2u);
        scripted.At(35);
        EQUALS(scripted.Log(), "in,in,in,out");
    });

    // --- sleep ---

    IT("goes on after a sleep once that much time has been ticked", {
        Scripted scripted;
        EQUALS(scripted.Run("note('before') sleep(50) note('after')"), "");
        EQUALS(scripted.Log(), "before");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 1u);
        EQUALS(scripted.Scheduler().PendingTimers(), 0u);
        scripted.At(20);
        scripted.At(49);
        EQUALS(scripted.Log(), "before");
        scripted.At(50);
        EQUALS(scripted.Log(), "before,after");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
        EQUALS(scripted.errors->size(), 0u);
        EQUALS(lua_gettop(scripted.runtime.State()), 0);
    });

    IT("counts a sleep from the tick the script was started on", {
        Scripted scripted;
        scripted.At(1000);
        EQUALS(scripted.Run("sleep(50) note('woke')"), "");
        scripted.At(1049);
        EQUALS(scripted.Log(), "");
        scripted.At(1050);
        EQUALS(scripted.Log(), "woke");
    });

    IT("wakes a sleep of no time on the next tick", {
        Scripted scripted;
        EQUALS(scripted.Run("for i = 1, 3 do sleep(0) note(i) end sleep() note('missing') sleep(-5) note('negative') sleep('x') note('text')"), "");
        EQUALS(scripted.Log(), "");
        for (const char *expected : {"1", "1,2", "1,2,3", "1,2,3,missing", "1,2,3,missing,negative", "1,2,3,missing,negative,text"}) {
            scripted.At(0);
            EQUALS(scripted.Log(), expected);
        }
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
    });

    IT("returns nothing from sleep", {
        Scripted scripted;
        EQUALS(scripted.Run("count = select('#', sleep(1))"), "");
        scripted.At(1);
        EQUALS(Eval(scripted.runtime, "count"), "0");
    });

    IT("wakes sleeps and fires timers in the one order they came due", {
        Scripted scripted;
        EQUALS(scripted.Run("spawn(function() sleep(20) note('s20') end)\n"
                            "setTimeout(function() note('t10') end, 10)\n"
                            "spawn(function() sleep(10) note('s10') end)\n"
                            "setTimeout(function() note('t20') end, 20)"),
            "");
        scripted.At(30);
        EQUALS(scripted.Log(), "t10,s10,s20,t20");
    });

    IT("lets a script wait under pcall, in a loop and in a function it calls", {
        Scripted scripted;
        EQUALS(scripted.Run("local function nap(ms) sleep(ms) return 'napped' end\n"
                            "local ok, value = pcall(nap, 10)\n"
                            "note(ok) note(value)\n"
                            "for _, ms in ipairs({ 10, 10 }) do note(nap(ms)) end\n"
                            "note(select(2, xpcall(nap, print, 10)))\n"
                            "note(select(2, pcall(function() sleep(10) error('after the wait', 0) end)))"),
            "");
        for (int ms = 10; ms <= 50; ms += 10) {
            scripted.At(ms);
        }
        EQUALS(scripted.Log(), "true,napped,napped,napped,napped,after the wait");
        EQUALS(scripted.errors->size(), 0u);
    });

    IT("refuses to wait where Lua cannot yield", {
        Scripted scripted;
        // Call runs on the main thread.
        EQUALS(Eval(scripted.runtime, "sleep(10)"), "<error> eval:1: sleep: cannot wait here");
        EQUALS(FirstLine(scripted.Run("table.sort({ 3, 2, 1 }, function(a, b) sleep(1) return a < b end)")), "test:1: sleep: cannot wait here");
        EQUALS(FirstLine(scripted.Run("local text = tostring(setmetatable({}, { __tostring = function() sleep(1) return 'x' end }))")), "test:1: sleep: cannot wait here");
        EQUALS(scripted.Run("caught = select(2, pcall(string.gsub, 'x', 'x', function() sleep(1) end)) note('went on')"), "");
        EQUALS(Eval(scripted.runtime, "caught"), "test:1: sleep: cannot wait here");
        EQUALS(scripted.Log(), "went on");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
    });

    IT("refuses to wait in a coroutine the script created", {
        Scripted scripted;
        EQUALS(scripted.Run("ok, message = coroutine.resume(coroutine.create(function() sleep(10) end)) note('went on')"), "");
        EQUALS(Eval(scripted.runtime, "ok"), "false");
        EQUALS(Eval(scripted.runtime, "message"), "test:1: sleep: cannot wait here: called inside a coroutine the script created");
        EQUALS(FirstLine(scripted.Run("for value in coroutine.wrap(function() sleep(10) coroutine.yield(1) end) do end")), "test:1: test:1: sleep: cannot wait here: called inside a coroutine the script created");
        EQUALS(scripted.Log(), "went on");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);

        // The coroutine a script makes yields to the script as it always did.
        EQUALS(scripted.Run("local resume = coroutine.wrap(function() note(coroutine.yield('out')) end) note(resume()) sleep(5) resume('in')"), "");
        scripted.At(5);
        EQUALS(scripted.Log(), "went on,out,in");
    });

    IT("ends a chunk that yields with no coroutine of its own around it", {
        Scripted scripted;
        const std::string error = scripted.Run("local guard <close> = setmetatable({}, { __close = function() note('closed') end })\n"
                                               "note('before')\n"
                                               "coroutine.yield()\n"
                                               "note('after')");
        EQUALS(error, "test:3: attempt to yield from outside a coroutine");
        EQUALS(scripted.Log(), "before,closed");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
        EQUALS(scripted.Run("setTimeout(function() coroutine.yield(1, 2) end, 0)"), "");
        scripted.At(0);
        EQUALS(scripted.errors->size(), 1u);
        EQUALS((*scripted.errors)[0], "test:1: attempt to yield from outside a coroutine");
        EQUALS(scripted.Run("note('usable')"), "");
        EQUALS(scripted.Log(), "before,closed,usable");
    });

    IT("hands what a __close handler raises while a coroutine is closed to the error handler", {
        Scripted scripted;
        AddNesting(scripted.runtime);
        // One that is closed for a stray yield: the chunk's own error goes to the caller.
        const std::string error = scripted.Run("local guard <close> = setmetatable({}, { __close = function() error('in close') end })\n"
                                               "coroutine.yield()");
        EQUALS(error, "test:2: attempt to yield from outside a coroutine");
        EQUALS(scripted.errors->size(), 1u);
        EQUALS((*scripted.errors)[0], "test:1: in close");

        // One that is closed because its resume was refused: its own error goes to its done.
        EQUALS(scripted.Run("function waiter() local guard <close> = setmetatable({}, { __close = function() error('in close too') end }) wait() note('unreached') end"), "");
        const auto waiter = StartGlobal(scripted, "waiter");
        g_waiting         = g_token;
        EQUALS(scripted.Run("at_depth(64)"), "");
        EQUALS(waiter->calls, 1);
        EQUALS(waiter->ok, false);
        EQUALS(waiter->error, "too many nested calls into the script (the limit is 64)");
        EQUALS(scripted.errors->size(), 2u);
        EQUALS((*scripted.errors)[1], "test:1: in close too");
        EQUALS(scripted.Log(), "");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
    });

    // --- spawn ---

    IT("runs a spawned function at once, up to where it first waits", {
        Scripted scripted;
        EQUALS(scripted.Run("local returned = select('#', spawn(function(a, b, ...) note('start ' .. a .. tostring(b) .. select('#', ...)) sleep(10) note('end') end, 'x', nil, nil))\n"
                            "note('spawned ' .. returned)"),
            "");
        EQUALS(scripted.Log(), "start xnil1,spawned 0");
        scripted.At(10);
        EQUALS(scripted.Log(), "start xnil1,spawned 0,end");
    });

    IT("refuses to spawn what is not a function", {
        Scripted scripted;
        EQUALS(Eval(scripted.runtime, "spawn()"), "<error> eval:1: spawn: callback must be a function");
        EQUALS(Eval(scripted.runtime, "spawn('note', 1)"), "<error> eval:1: spawn: callback must be a function");
    });

    IT("hands the error of a spawned function to the error handler, not to the script that spawned it", {
        Scripted scripted;
        EQUALS(scripted.Run("spawn(function() error('in spawned') end)\n"
                            "spawn(function() sleep(10) error('in spawned later') end)\n"
                            "note('went on')"),
            "");
        EQUALS(scripted.Log(), "went on");
        EQUALS(scripted.errors->size(), 1u);
        EQUALS(FirstLine((*scripted.errors)[0]), "test:1: in spawned");
        EQUALS(Contains((*scripted.errors)[0], "\nstack traceback:\n"), true);
        scripted.At(10);
        EQUALS(scripted.errors->size(), 2u);
        EQUALS(FirstLine((*scripted.errors)[1]), "test:2: in spawned later");
    });

    IT("spawns from the main thread and from a coroutine the script created", {
        Scripted scripted;
        EQUALS(Eval(scripted.runtime, "spawn(function() note('from call') sleep(5) note('woke') end)"), "nil");
        EQUALS(scripted.Run("coroutine.wrap(function() spawn(function() note('from wrap') sleep(5) note('woke too') end) end)()"), "");
        EQUALS(scripted.Log(), "from call,from wrap");
        scripted.At(5);
        EQUALS(scripted.Log(), "from call,from wrap,woke,woke too");
        EQUALS(lua_gettop(scripted.runtime.State()), 0);
    });

    IT("refuses to start a coroutine nested too deep", {
        Scripted scripted;
        EQUALS(scripted.Run("local function nest(level) deepest = level spawn(nest, level + 1) end nest(1)"), "");
        EQUALS(Eval(scripted.runtime, "deepest"), std::to_string(LuaScheduler::kMaxNesting));
        EQUALS(scripted.errors->size(), 1u);
        EQUALS((*scripted.errors)[0], "too many nested calls into the script (the limit is 64)");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
        EQUALS(scripted.Run("note('usable')"), "");
        EQUALS(lua_gettop(scripted.runtime.State()), 0);
    });

    IT("resumes a coroutine from inside nested entries, up to the limit", {
        Scripted scripted;
        AddNesting(scripted.runtime);
        EQUALS(scripted.Run("function waiter(name) wait() note(name .. ' went on') return name end"), "");
        for (const int depth : {1, 63}) {
            const auto resumed = StartGlobal(scripted, "waiter", {depth});
            g_waiting          = g_token;
            EQUALS(scripted.Run("at_depth(" + std::to_string(depth) + ")"), "");
            EQUALS(resumed->calls, 1);
            EQUALS(resumed->ok, true);
            EQUALS(resumed->result, std::to_string(depth));
        }
        EQUALS(scripted.Log(), "1 went on,63 went on");

        const auto refused = StartGlobal(scripted, "waiter", {64});
        g_waiting          = g_token;
        EQUALS(scripted.Run("at_depth(64)"), "");
        EQUALS(refused->calls, 1);
        EQUALS(refused->ok, false);
        EQUALS(refused->error, "too many nested calls into the script (the limit is 64)");
        EQUALS(scripted.Log(), "1 went on,63 went on");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
        EQUALS(scripted.errors->size(), 0u);
        EQUALS(lua_gettop(scripted.runtime.State()), 0);
    });

    // --- Start ---

    IT("tells done the first result of a function that ends at once", {
        Scripted scripted;
        EQUALS(scripted.Run("function add(a, b) return a + b, 'second' end function nothing() end"), "");
        const auto added = StartGlobal(scripted, "add", {2, 3});
        EQUALS(added->calls, 1);
        EQUALS(added->ok, true);
        EQUALS(added->result, "5");
        EQUALS(added->error, "");
        const auto nothing = StartGlobal(scripted, "nothing");
        EQUALS(nothing->calls, 1);
        EQUALS(nothing->ok, true);
        EQUALS(nothing->result, "nil");
        EQUALS(lua_gettop(scripted.runtime.State()), 0);
        EQUALS(scripted.errors->size(), 0u);
    });

    IT("tells done the first result of a function that waited, when it ends", {
        Scripted scripted;
        EQUALS(scripted.Run("function double(a) sleep(10) sleep(10) return a * 2 end"), "");
        const auto doubled = StartGlobal(scripted, "double", {21});
        EQUALS(doubled->calls, 0);
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 1u);
        scripted.At(10);
        EQUALS(doubled->calls, 0);
        scripted.At(20);
        EQUALS(doubled->calls, 1);
        EQUALS(doubled->ok, true);
        EQUALS(doubled->result, "42");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
        scripted.At(30);
        EQUALS(doubled->calls, 1);
    });

    IT("tells done the error of a function that fails, and not the error handler", {
        Scripted scripted;
        EQUALS(scripted.Run("function fail(a) error('failed ' .. a) end function fail_later(a) sleep(10) error('failed later ' .. a) end"), "");
        const auto failed = StartGlobal(scripted, "fail", {1});
        EQUALS(failed->calls, 1);
        EQUALS(failed->ok, false);
        EQUALS(failed->hadThread, false);
        EQUALS(FirstLine(failed->error), "test:1: failed 1");
        EQUALS(Contains(failed->error, "\nstack traceback:\n"), true);
        EQUALS(Contains(failed->error, "in function 'fail'"), true);

        const auto later = StartGlobal(scripted, "fail_later", {2});
        EQUALS(later->calls, 0);
        scripted.At(10);
        EQUALS(later->calls, 1);
        EQUALS(later->ok, false);
        EQUALS(FirstLine(later->error), "test:1: failed later 2");
        EQUALS(Contains(later->error, "in function 'fail_later'"), true);
        EQUALS(scripted.errors->size(), 0u);
    });

    IT("hands the error of a function started without done to the error handler", {
        Scripted scripted;
        EQUALS(scripted.Run("function fail() error('nobody waits') end"), "");
        lua_getglobal(scripted.runtime.State(), "fail");
        scripted.Scheduler().Start(0);
        EQUALS(scripted.errors->size(), 1u);
        EQUALS(FirstLine((*scripted.errors)[0]), "test:1: nobody waits");
        EQUALS(lua_gettop(scripted.runtime.State()), 0);
    });

    IT("fails the start of what cannot be called", {
        Scripted scripted;
        lua_pushinteger(scripted.runtime.State(), 5);
        lua_pushinteger(scripted.runtime.State(), 6);
        auto outcome = std::make_shared<Outcome>();
        scripted.Scheduler().Start(1, Into(outcome));
        EQUALS(outcome->calls, 1);
        EQUALS(outcome->ok, false);
        EQUALS(FirstLine(outcome->error), "attempt to call a number value");
        EQUALS(lua_gettop(scripted.runtime.State()), 0);
    });

    IT("lets done start and resume other coroutines", {
        Scripted scripted;
        SetGlobalFunction(scripted.runtime, "wait", Wait);
        EQUALS(scripted.Run("function first() sleep(10) return 'first' end function second(value) note('second got ' .. value) end\n"
                            "note('waiter got ' .. wait())"),
            "");
        lua_getglobal(scripted.runtime.State(), "first");
        scripted.Scheduler().Start(0, [&scripted, waiter = g_token](bool, lua_State *thread, const std::string &) {
            lua_State *main = scripted.runtime.State();
            lua_getglobal(main, "second");
            lua_pushvalue(thread, -1);
            lua_xmove(thread, main, 1);
            scripted.Scheduler().Start(1);
            scripted.Scheduler().Resume(waiter, [thread](lua_State *resumed) {
                lua_pushvalue(thread, -1);
                lua_xmove(thread, resumed, 1);
                return 1;
            });
        });
        scripted.At(10);
        EQUALS(scripted.Log(), "second got first,waiter got first");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
        EQUALS(scripted.errors->size(), 0u);
        EQUALS(lua_gettop(scripted.runtime.State()), 0);
    });

    IT("writes the error nobody takes to the scripting logger", {
        LogCapture log;
        LuaRuntimeOptions options = Options();
        options.resourceName      = "shop";
        LuaRuntime runtime(options);
        EQUALS(Run(runtime, "setTimeout(function() error('unhandled') end, 0)"), "");
        runtime.Tick(std::chrono::steady_clock::now() + std::chrono::seconds(1));
        EQUALS(CountStartingWith(log.Lines(), "[shop] test:1: unhandled\nstack traceback:"), 1u);

        std::vector<std::string> errors;
        runtime.Scheduler().SetErrorHandler([&errors](const std::string &message) {
            errors.push_back(message);
        });
        EQUALS(Run(runtime, "spawn(error, 'handled')"), "");
        EQUALS(errors.size(), 1u);
        runtime.Scheduler().SetErrorHandler({});
        EQUALS(Run(runtime, "spawn(error, 'logged again')"), "");
        EQUALS(errors.size(), 1u);
        EQUALS(CountStartingWith(log.Lines(), "[shop] logged again"), 1u);
    });

    IT("writes the errors nobody takes to the log no faster than it writes lines", {
        LogCapture log;
        LuaRuntimeOptions options = Options();
        options.resourceName      = "noisy";
        LuaRuntime runtime(options);
        EQUALS(Run(runtime, "for i = 1, 500 do spawn(error, 'failed ' .. i) end print('a line')"), "");
        const std::vector<std::string> lines = log.Lines();
        EQUALS(CountStartingWith(lines, "[noisy] failed "), static_cast<size_t>(LuaRuntime::kMaxLinesPerSecond));
        EQUALS(CountStartingWith(lines, "[noisy] failed 200\n"), 1u);
        EQUALS(CountStartingWith(lines, "[noisy] output suppressed"), 1u);
        EQUALS(CountStartingWith(lines, "[noisy] a line"), 0u);
    });

    // --- Chunks ---

    IT("returns from a chunk that waits and hands what it raises later to the error handler", {
        Scripted scripted;
        EQUALS(scripted.Run("note('ran') sleep(10) error('late')"), "");
        EQUALS(scripted.Log(), "ran");
        EQUALS(scripted.errors->size(), 0u);
        scripted.At(10);
        EQUALS(scripted.errors->size(), 1u);
        EQUALS(FirstLine((*scripted.errors)[0]), "test:1: late");
        EQUALS(Contains((*scripted.errors)[0], "test:1: in main chunk"), true);
    });

    IT("returns the error of a chunk that fails before it waits and does not hand it to the error handler", {
        Scripted scripted;
        const std::string error = scripted.Run("note('ran') error('at once') sleep(10)");
        EQUALS(FirstLine(error), "test:1: at once");
        EQUALS(Contains(error, "test:1: in main chunk"), true);
        scripted.At(10);
        EQUALS(scripted.errors->size(), 0u);
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
    });

    IT("runs a chunk in a coroutine and a call on the main thread", {
        Scripted scripted;
        EQUALS(scripted.Run("local thread, main = coroutine.running() note(main) note(coroutine.isyieldable())"), "");
        EQUALS(Eval(scripted.runtime, "select(2, coroutine.running())"), "true");
        EQUALS(scripted.Log(), "false,true");
    });

    // --- Suspend and Resume ---

    IT("resumes a coroutine with the values the host pushes", {
        Scripted scripted;
        SetGlobalFunction(scripted.runtime, "wait", Wait);
        g_token = 0;
        EQUALS(scripted.Run("local a, b, c = wait() note(a) note(b) note(c) note(select('#', wait()))"), "");
        EQUALS(g_token == 0, false);
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 1u);
        EQUALS(scripted.Log(), "");

        const uint64_t first = g_token;
        scripted.Scheduler().Resume(first, [](lua_State *thread) {
            lua_pushinteger(thread, 1);
            lua_pushstring(thread, "two");
            return 2;
        });
        EQUALS(scripted.Log(), "1,two,nil");
        EQUALS(g_token == first, false);

        scripted.Scheduler().Resume(g_token, {});
        EQUALS(scripted.Log(), "1,two,nil,0");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
        EQUALS(scripted.errors->size(), 0u);
        EQUALS(lua_gettop(scripted.runtime.State()), 0);
    });

    IT("does nothing with a token that is unknown or was used", {
        Scripted scripted;
        SetGlobalFunction(scripted.runtime, "wait", Wait);
        int pushes = 0;
        EQUALS(scripted.Run("note(wait()) note(wait())"), "");
        const uint64_t first = g_token;
        scripted.Scheduler().Resume(first + 1000, [&pushes](lua_State *) {
            ++pushes;
            return 0;
        });
        scripted.Scheduler().ResumeWithError(0, "nobody");
        EQUALS(scripted.Log(), "");
        scripted.Scheduler().Resume(first, [&pushes](lua_State *thread) {
            ++pushes;
            lua_pushstring(thread, "once");
            return 1;
        });
        scripted.Scheduler().Resume(first, [&pushes](lua_State *thread) {
            ++pushes;
            lua_pushstring(thread, "twice");
            return 1;
        });
        scripted.Scheduler().ResumeWithError(first, "twice");
        EQUALS(pushes, 1);
        EQUALS(scripted.Log(), "once");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 1u);
        EQUALS(scripted.errors->size(), 0u);
    });

    IT("keeps a resume that comes before the coroutine got to wait", {
        Scripted scripted;
        SetGlobalFunction(scripted.runtime, "settled", Settled);
        EQUALS(scripted.Run("note(settled()) note(select(2, pcall(settled))) note('went on')"), "");
        EQUALS(scripted.Log(), "early,early,went on");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
    });

    IT("keeps an error a coroutine is resumed with before it got to wait", {
        Scripted scripted;
        SetGlobalFunction(scripted.runtime, "failed", Failed);
        // The chunk never got to wait, so its failure is the caller's.
        const std::string error = scripted.Run("note(select(2, pcall(failed)))\n"
                                               "note(select(2, pcall(function() return failed() end)))\n"
                                               "failed()\n"
                                               "note('unreached')");
        EQUALS(FirstLine(error), "test:3: failed early");
        EQUALS(scripted.Log(), "failed early,test:2: failed early");
        EQUALS(scripted.errors->size(), 0u);
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
    });

    IT("raises in the coroutine what it is resumed with as an error", {
        Scripted scripted;
        SetGlobalFunction(scripted.runtime, "wait", Wait);
        EQUALS(scripted.Run("local ok, message = pcall(wait)\n"
                            "note(ok) note(message)\n"
                            "ok, message = pcall(function()\n"
                            "  return wait()\n"
                            "end)\n"
                            "note(ok) note(message)\n"
                            "wait()\n"
                            "note('unreached')"),
            "");
        scripted.Scheduler().ResumeWithError(g_token, "Http.get: no route");
        scripted.Scheduler().ResumeWithError(g_token, "Http.get: no route");
        EQUALS(scripted.Log(), "false,Http.get: no route,false,test:4: Http.get: no route");
        EQUALS(scripted.errors->size(), 0u);

        scripted.Scheduler().ResumeWithError(g_token, "Http.get: nobody caught this");
        EQUALS(scripted.errors->size(), 1u);
        EQUALS(FirstLine((*scripted.errors)[0]), "test:7: Http.get: nobody caught this");
        EQUALS(Contains((*scripted.errors)[0], "test:7: in main chunk"), true);
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
    });

    IT("fails a coroutine whose resume values cannot be pushed", {
        Scripted scripted;
        SetGlobalFunction(scripted.runtime, "wait", Wait);
        EQUALS(scripted.Run("local guard <close> = setmetatable({}, { __close = function() note('closed') end })\n"
                            "note(pcall(wait)) note('unreached')"),
            "");
        scripted.Scheduler().Resume(g_token, [](lua_State *thread) {
            return luaL_error(thread, "no values");
        });
        EQUALS(scripted.errors->size(), 1u);
        EQUALS(FirstLine((*scripted.errors)[0]), "no values");
        EQUALS(scripted.Log(), "closed");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
        EQUALS(scripted.Run("note('usable')"), "");
        EQUALS(lua_gettop(scripted.runtime.State()), 0);
    });

    IT("does not let a script resume or close the coroutine the host started it in", {
        Scripted scripted;
        EQUALS(scripted.Run("local chunk = coroutine.running()\n"
                            "note(select(2, coroutine.resume(chunk)))\n"
                            "spawn(function()\n"
                            "  sleep(10)\n"
                            "  note(coroutine.status(chunk))\n"
                            "  note(coroutine.resume(chunk, 'by script'))\n"
                            "  note(select(2, coroutine.resume(chunk, 'by script')))\n"
                            "  note(select(2, pcall(coroutine.close, chunk)))\n"
                            "  note(select(2, coroutine.wrap(function() return coroutine.resume(chunk) end)()))\n"
                            "end)\n"
                            "note(select('#', sleep(20))) note('went on')"),
            "");
        scripted.At(10);
        EQUALS(scripted.Log(), "cannot resume a coroutine the host started,suspended,false,cannot resume a coroutine the host started,cannot close a coroutine the host started,"
                               "cannot resume a coroutine the host started");
        scripted.At(20);
        EQUALS(Eval(scripted.runtime, "log[#log - 1] .. ',' .. log[#log]"), "0,went on");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
        EQUALS(scripted.errors->size(), 0u);

        // A coroutine the script made is its own to resume and close.
        EQUALS(scripted.Run("log = {} local own = coroutine.create(function() coroutine.yield('yielded') end)\n"
                            "note(select(2, coroutine.resume(own))) note(coroutine.close(own)) note(coroutine.status(own))"),
            "");
        EQUALS(scripted.Log(), "yielded,true,dead");
    });

    IT("does not let a script resume a coroutine the host started while its done runs, or after", {
        Scripted scripted;
        lua_State *L = scripted.runtime.State();
        EQUALS(scripted.Run("function h() me = coroutine.running() return function() sleep(10) note('ran') end end\n"
                            "function x() note(coroutine.resume(me)) note(select(2, coroutine.resume(me))) note(select(2, pcall(coroutine.close, me))) end"),
            "");
        int told = 0;
        lua_getglobal(L, "h");
        scripted.Scheduler().Start(0, [&scripted, &told](bool, lua_State *, const std::string &) {
            ++told;
            lua_getglobal(scripted.runtime.State(), "x");
            scripted.Scheduler().Start(0);
        });
        EQUALS(told, 1);
        EQUALS(scripted.Log(), "false,cannot resume a coroutine the host started,cannot close a coroutine the host started");
        scripted.At(10);
        scripted.At(20);
        EQUALS(scripted.Log(), "false,cannot resume a coroutine the host started,cannot close a coroutine the host started");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
        EQUALS(scripted.errors->size(), 0u);

        // Once it has ended and the host has let go of it, it is a dead coroutine like any other.
        EQUALS(scripted.Run("log = {} note(coroutine.status(me)) note(select(2, coroutine.resume(me))) note(coroutine.close(me))"), "");
        EQUALS(scripted.Log(), "dead,cannot resume dead coroutine,true");
        EQUALS(lua_gettop(L), 0);
    });

    IT("does not let a coroutine the host started close itself", {
        Scripted scripted;
        EQUALS(scripted.Run("function f()\n"
                            "  note(select(2, pcall(coroutine.close)))\n"
                            "  note(select(2, pcall(coroutine.close, coroutine.running())))\n"
                            "  sleep(10)\n"
                            "  return 'went on'\n"
                            "end"),
            "");
        const auto outcome = StartGlobal(scripted, "f");
        EQUALS(scripted.Log(), "cannot close a coroutine the host started,cannot close a coroutine the host started");
        EQUALS(outcome->calls, 0);
        scripted.At(10);
        EQUALS(outcome->calls, 1);
        EQUALS(outcome->ok, true);
        EQUALS(outcome->result, "went on");
        scripted.At(20);
        EQUALS(outcome->calls, 1);

        EQUALS(FirstLine(scripted.Run("coroutine.close() note('unreached')")), "test:1: cannot close a coroutine the host started");
        // A coroutine the script made still closes itself.
        EQUALS(scripted.Run("log = {} coroutine.wrap(function() coroutine.close() note('unreached') end)() note('after')"), "");
        EQUALS(scripted.Log(), "after");
        EQUALS(scripted.errors->size(), 0u);
    });

    IT("names the function that cannot wait", {
        Scripted scripted;
        SetGlobalFunction(scripted.runtime, "wait", Wait);
        EQUALS(Eval(scripted.runtime, "wait()"), "<error> eval:1: wait: cannot wait here");
        EQUALS(scripted.Run("message = select(2, coroutine.resume(coroutine.create(wait)))"), "");
        EQUALS(Eval(scripted.runtime, "message"), "wait: cannot wait here: called inside a coroutine the script created");
    });

    IT("refuses a wait that no Suspend came before", {
        Scripted scripted;
        SetGlobalFunction(scripted.runtime, "wait_only", WaitOnly);
        EQUALS(FirstLine(scripted.Run("wait_only()")), "test:1: LuaScheduler::Wait: Suspend was not called");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
    });

    // --- The deadline ---

    IT("gives every stretch a coroutine runs a deadline of its own", {
        Scripted scripted(Options(0, 150));
        SetGlobalFunction(scripted.runtime, "now", NowMs);
        // Three stretches of 60 ms: none is too long, all of them together are.
        EQUALS(scripted.Run("local function work() local stop = now() + 60 while now() < stop do end end\n"
                            "work() sleep(1) work() sleep(1) work() note('finished')"),
            "");
        scripted.At(1);
        scripted.At(2);
        EQUALS(scripted.Log(), "finished");
        EQUALS(scripted.errors->size(), 0u);
    });

    IT("stops a coroutine that runs too long after a wait", {
        Scripted scripted(Options(0, 50));
        AddSpin(scripted.runtime);
        EQUALS(scripted.Run("sleep(1) spin() survived = true"), "");
        const auto start = std::chrono::steady_clock::now();
        scripted.At(1);
        LESSER(ElapsedMs(start), 500);
        EQUALS(scripted.errors->size(), 1u);
        EQUALS(FirstLine((*scripted.errors)[0]), "spin:1: script ran too long");
        EQUALS(Contains((*scripted.errors)[0], "test:1: in main chunk"), true);
        EQUALS(Eval(scripted.runtime, "survived"), "nil");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);

        EQUALS(scripted.Run("sleep(1) note('usable')"), "");
        scripted.At(2);
        EQUALS(scripted.Log(), "usable");
    });

    IT("stops a timer callback that runs too long and fires the next", {
        Scripted scripted(Options(0, 50));
        AddSpin(scripted.runtime);
        EQUALS(scripted.Run("setTimeout(function() spin() survived = true end, 10) setTimeout(function() note('next') end, 10)"), "");
        scripted.At(10);
        EQUALS(scripted.errors->size(), 1u);
        EQUALS(FirstLine((*scripted.errors)[0]), "spin:1: script ran too long");
        EQUALS(Eval(scripted.runtime, "survived"), "nil");
        EQUALS(scripted.Log(), "next");
    });

    IT("stops the entry a spawned function ran out of time in", {
        for (const char *source : {"spawn(spin) survived = true", "pcall(spawn, spin) survived = true", "coroutine.wrap(function() spawn(spin) survived = true end)() survived = true", "spawn(function() spawn(spin) survived = true end) survived = true"}) {
            Scripted scripted(Options(0, 50));
            AddSpin(scripted.runtime);
            const auto start        = std::chrono::steady_clock::now();
            const std::string error = scripted.Run(source);
            LESSER(ElapsedMs(start), 500);
            EQUALS(FirstLine(error), "spin:1: script ran too long");
            EQUALS(Eval(scripted.runtime, "survived"), "nil");
            GREATEREQ(scripted.errors->size(), 1u);
            EQUALS(FirstLine((*scripted.errors)[0]), "spin:1: script ran too long");
        }
    });

    IT("starts and resumes nothing in an entry whose time is up", {
        Scripted scripted(Options(0, 50));
        AddSpin(scripted.runtime);
        SetGlobalFunction(scripted.runtime, "wait", Wait);
        SetGlobalFunction(scripted.runtime, "go_on_past_deadline", GoOnPastDeadline);
        EQUALS(scripted.Run("note(wait())"), "");
        g_waiting               = g_token;
        g_started               = std::make_shared<Outcome>();
        const std::string error = scripted.Run("go_on_past_deadline() survived = true");
        EQUALS(FirstLine(error), "spin:1: script ran too long");
        EQUALS(Eval(scripted.runtime, "survived"), "nil");
        EQUALS(g_started->calls, 1);
        EQUALS(g_started->ok, false);
        EQUALS(g_started->error, "script ran too long");
        EQUALS(scripted.Log(), "");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
        EQUALS(scripted.errors->size(), 1u);
        EQUALS((*scripted.errors)[0], "script ran too long");
        g_started.reset();
    });

    // --- Lifetime ---

    IT("drops what is pending when the runtime is destroyed", {
        auto printed = std::make_shared<std::vector<std::string>>();
        std::shared_ptr<std::vector<std::string>> errors;
        std::shared_ptr<Outcome> outcome;
        {
            Scripted scripted;
            errors = scripted.errors;
            scripted.runtime.SetPrintHandler([printed](const std::string &line) {
                printed->push_back(line);
            });
            EQUALS(scripted.Run("function nap() local guard <close> = setmetatable({}, { __close = function() print('closed') end }) sleep(100) print('woke') end\n"
                                "spawn(nap) spawn(nap)\n"
                                "setTimeout(function() print('fired') end, 50)\n"
                                "setInterval(function() print('fired') end, 50)"),
                "");
            outcome = StartGlobal(scripted, "nap");
            EQUALS(scripted.Scheduler().SuspendedCoroutines(), 3u);
            EQUALS(scripted.Scheduler().PendingTimers(), 2u);
            scripted.At(10);
        }
        EQUALS(printed->size(), 0u);
        EQUALS(errors->size(), 0u);
        EQUALS(outcome->calls, 0);
    });

    IT("leaves a coroutine that ended to the collector", {
        Scripted scripted;
        lua_State *L = scripted.runtime.State();
        EQUALS(scripted.Run("function nap() sleep(1) return 1 end"), "");
        AddNesting(scripted.runtime);
        // Coroutines that end at once, fail, sleep and are started by a timer; then one that yields with no
        // coroutine around it, one whose resume values cannot be pushed and one whose resume is refused.
        const auto round = [&scripted](int at) {
            scripted.Run("local t = {} for i = 1, 100 do t[i] = i end");
            scripted.Run("spawn(nap) setTimeout(nap, 1) error('failed')");
            StartGlobal(scripted, "nap");
            scripted.At(at);
            scripted.At(at + 1);
            scripted.Run("coroutine.yield()");
            scripted.Run("wait()");
            scripted.Scheduler().Resume(g_token, [](lua_State *thread) {
                return luaL_error(thread, "no values");
            });
            scripted.Run("wait()");
            g_waiting = g_token;
            scripted.Run("at_depth(64)");
        };
        round(10);
        lua_gc(L, LUA_GCCOLLECT);
        const size_t before = scripted.runtime.MemoryInUse();
        for (int i = 0; i < 200; ++i) {
            round(20 + i * 2);
        }
        EQUALS(Eval(scripted.runtime, "#log"), "0");
        EQUALS(scripted.errors->size(), 402u);
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 0u);
        EQUALS(scripted.Scheduler().PendingTimers(), 0u);
        lua_gc(L, LUA_GCCOLLECT);
        LESSER(scripted.runtime.MemoryInUse(), before + 1024);
    });

    IT("goes on in a state that has no memory left", {
        Scripted scripted(Options(1024 * 1024));
        lua_State *L = scripted.runtime.State();
        SetGlobalFunction(scripted.runtime, "wait", Wait);
        EQUALS(scripted.Run("wait() note('unreached')"), "");
        // The chunk holds every byte it could get, and stays alive so that nothing of it is garbage.
        EQUALS(scripted.Run("fired = 0 setInterval(function() fired = fired + 1 end, 10)\n"
                            "pcall(function() while true do hold = { hold } end end)\n"
                            "sleep(1000000)"),
            "");
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 2u);

        // No tick can create the coroutine of the callback, and the error to resume with cannot be pushed. Each
        // is reported, and none takes the state down.
        std::string thrown;
        try {
            for (int tick = 1; tick <= 20; ++tick) {
                scripted.At(tick * 10);
            }
            scripted.Scheduler().ResumeWithError(g_token, std::string(4096, 'x'));
        }
        catch (const std::runtime_error &exception) {
            thrown = exception.what();
        }
        EQUALS(thrown, "");
        EQUALS(scripted.errors->size(), 21u);
        for (const std::string &error : *scripted.errors) {
            EQUALS(error, "not enough memory");
        }
        EQUALS(scripted.Scheduler().SuspendedCoroutines(), 1u);
        EQUALS(scripted.Scheduler().PendingTimers(), 1u);
        EQUALS(lua_gettop(L), 0);

        lua_pushnil(L);
        lua_setglobal(L, "hold");
        lua_gc(L, LUA_GCCOLLECT);
        EQUALS(Eval(scripted.runtime, "fired"), "0");
        scripted.At(210);
        EQUALS(Eval(scripted.runtime, "fired"), "1");
        EQUALS(scripted.Log(), "");
        EQUALS(scripted.errors->size(), 21u);
    });
});
