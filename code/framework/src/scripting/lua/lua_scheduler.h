/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <function2/function2.hpp>
#include <lua.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace Framework::Scripting {
    class LuaRuntime;

    // The coroutines and the timers of one LuaRuntime.
    //
    // Coroutines. What the host starts in the state (a file, an event handler, a timer callback) runs in a coroutine
    // of its own, so that it can wait: sleep(), and whatever host function is built on Suspend, hands control back to
    // the host, and the coroutine goes on from a later Tick or Resume. Each stretch a coroutine runs, from its start
    // or a resume to its end or its next wait, is one entry into the state: a protected call under the runtime's
    // deadline. A coroutine is never reused; one that ended is closed and left to the collector.
    //
    // Waiting works on a coroutine the scheduler started, and nowhere else. On a coroutine a script made itself
    // (coroutine.wrap(function() sleep(10) end)()) it raises "cannot wait here: called inside a coroutine the script
    // created", and where Lua cannot yield, as on the main thread or under table.sort, it raises "cannot wait here".
    // To a script the coroutine it runs in is the main thread of plain Lua: coroutine.yield() with no coroutine of
    // its own around it ends it with "<position>: attempt to yield from outside a coroutine". A script can still
    // hand that coroutine (coroutine.running()) to another one and resume or close it from there while it waits.
    // That harms nothing but the script: a coroutine ended that way never tells its `done`.
    //
    // Timers. setTimeout, setInterval, clearTimeout and clearInterval as JavaScript has them, sleep(ms) and
    // spawn(fn, ...). Time is what Tick is given and nothing else: a delay counts from the last tick, or from the
    // creation of the scheduler before the first. A tick fires what is due as it starts, in the order it came due;
    // what a callback arms waits for the next tick, however short its delay. An interval is armed again from the
    // time it was due, not from the time it ran, so it does not drift, and one that has fallen behind fires once
    // on each tick until it has caught up.
    //
    // Errors. A coroutine that fails tells exactly one party, with the message and the traceback of what it
    // raised: the `done` it was started with, or else the error handler. A coroutine the deadline stops fails the
    // same way; when it was started from inside another entry, by spawn() for one, that entry is stopped as well.
    //
    // `done`, `push` and the error handler run inside Start, Resume or Tick. They must not throw, and must not
    // destroy the runtime. Destroying the runtime drops what is pending: no timer fires, no coroutine is resumed
    // or closed, and neither `done` nor the error handler is called.
    class LuaScheduler final {
      public:
        // Called once, when the coroutine has ended: `ok` with its first result on top of `thread`'s stack (nil
        // when it returned nothing), or not `ok` with the error text and no thread. It runs as host code, outside
        // the entry: it reads the result with raw access.
        using Finished = fu2::unique_function<void(bool ok, lua_State *thread, const std::string &error)>;
        // Pushes onto `thread` the values a resumed coroutine receives and returns how many. A few fit as it is;
        // more need lua_checkstack. It runs in a protected call, so it may raise.
        using Push         = fu2::unique_function<int(lua_State *thread)>;
        using ErrorHandler = fu2::function<void(const std::string &message) const>;

        // Timers a state may have pending at once, as V8Engine::kMaxTimers.
        static constexpr size_t kMaxTimers = 10000;

        // Entries into the state that may be in progress inside one another, as when a spawned function spawns.
        // Each takes a few kilobytes of the C stack, and Lua's own count would let 200 of them through. Past it
        // a coroutine is not started, and one that was to be resumed fails.
        static constexpr int kMaxNesting = 64;

        explicit LuaScheduler(LuaRuntime &runtime);
        ~LuaScheduler();

        LuaScheduler(const LuaScheduler &)            = delete;
        LuaScheduler &operator=(const LuaScheduler &) = delete;

        // Runs the function and `nargs` arguments on top of the main stack in a new coroutine, to its end or its
        // first wait. `done` is called when it ends, which may be before this returns.
        void Start(int nargs, Finished done = {});

        // For a C function a script calls, to make the coroutine it runs on wait:
        //
        //     const uint64_t token = scheduler.Suspend(L, "Api.method");
        //     ... hand the token to whatever calls Resume(token, ...) later ...
        //     return LuaScheduler::Wait(L);
        //
        // Suspend raises where the script cannot wait, with `function` in front of the message. Resume makes the
        // call return what `push` pushes; ResumeWithError makes it raise `message`, which a pcall around the call
        // catches. A token is good for one resume. Resuming with one that is unknown or used does nothing, and
        // resuming before Wait was reached is kept until it is. lua_yield in place of Wait does not do: the
        // scheduler takes it for the script's own coroutine.yield().
        uint64_t Suspend(lua_State *thread, const char *function = nullptr);
        static int Wait(lua_State *thread);
        void Resume(uint64_t token, Push push);
        void ResumeWithError(uint64_t token, const std::string &message);

        // Sets the time to `now`, fires the timers that are due and wakes the coroutines whose sleep is over.
        void Tick(std::chrono::steady_clock::time_point now);

        // For a runtime that is to be destroyed once it has stopped executing: from now on no timer fires and no
        // coroutine that waits is resumed, by Tick or by Resume. What is running goes on to its end or its next
        // wait.
        void Stop();

        // Timeouts still to fire and intervals, less the ones a script cleared. A sleep is not one.
        size_t PendingTimers() const {
            return _timers.size();
        }

        // Coroutines that wait for a Tick or a Resume.
        size_t SuspendedCoroutines() const;

        // Receives the error of a coroutine that was started without a `done`. The default, which an empty handler
        // restores, writes "[resource] message" to the scripting logger.
        void SetErrorHandler(ErrorHandler handler);

      private:
        friend class LuaRuntime;

        // What a script calls. Defined in lua_scheduler.cpp.
        struct Globals;

        // One entry into the state: what it is to run, and how that went.
        struct Entry;

        // What a resume brings: values, or with `raise` the one message to raise.
        struct Wakeup final {
            Push push;
            bool raise = false;
        };

        struct Coroutine final {
            Finished done;
            // What Suspend last gave out, until a resume takes it.
            uint64_t token = 0;
            // Set by Wait until the coroutine is resumed: its yield is a wait, not a script's coroutine.yield().
            bool waiting = false;
            // A resume that came before the coroutine yielded.
            std::optional<Wakeup> early;
            // The resume in progress brought an error, for Resumed to raise.
            bool raise = false;
            // What the function that called Wait had on its stack: the values of a resume come above.
            int floor = 0;
        };

        // When something is due, then the order it was armed in.
        using Due = std::pair<std::chrono::steady_clock::time_point, uint64_t>;

        struct Timer final {
            Due due;
            std::chrono::milliseconds interval {0}; // 0: fires once
            int nargs = 0;                          // the extra arguments its callback gets
        };

        // What is due: the timer with that id, or the coroutine a sleep() left waiting on that token.
        struct Pending final {
            uint64_t timer = 0;
            uint64_t token = 0;
        };

        // Creates the tables the scheduler keeps in the registry and the globals. Raises on failure, so the runtime
        // calls it in a protected call.
        void Install(lua_State *L);

        // Start, for a caller that takes the failure itself: with `error`, a coroutine that fails before its first
        // wait reports there and to nobody else. False when it failed.
        bool Begin(int nargs, Finished done, std::string *error);

        // Runs `entry` through LuaRuntime::Execute, over the `values` on top of the main stack that belong to it,
        // and ends the coroutine that did not come out of it waiting.
        bool Run(Entry &entry, int values, std::string *error);
        static int Enter(lua_State *L);

        lua_State *NewCoroutine(lua_State *L, int values, Finished &done);
        void Drive(lua_State *L, Entry &entry, int nargs);
        static int Deliver(Coroutine &coroutine, lua_State *thread, Wakeup &wakeup);
        void Wake(uint64_t token, Wakeup wakeup);
        void Report(Finished &done, const std::string &message, std::string *error);
        void Retire(lua_State *thread);

        Due Arm(std::chrono::steady_clock::time_point at, Pending pending);
        void Fire(uint64_t id);
        void Clear(lua_State *L, uint64_t id);
        static void PushCallback(lua_State *L, uint64_t id, int nargs);
        static void Forget(lua_State *L, uint64_t id);

        // What a coroutine runs, and where it goes on after a wait.
        static int Body(lua_State *L);
        static int FinishBody(lua_State *L, int status, lua_KContext context);
        static int Resumed(lua_State *L, int status, lua_KContext context);

        LuaRuntime &_runtime;
        ErrorHandler _errorHandler; // empty: the log

        // Every coroutine that has not ended, by its thread. The registry holds the thread for as long.
        std::unordered_map<lua_State *, Coroutine> _coroutines;
        std::unordered_map<uint64_t, lua_State *> _tokens;
        uint64_t _lastToken = 0;
        // The innermost coroutine being run.
        lua_State *_running = nullptr;

        // The callback and the arguments of a timer are in the registry, under its id.
        std::unordered_map<uint64_t, Timer> _timers;
        std::map<Due, Pending> _queue;
        uint64_t _lastTimer = 0;
        uint64_t _lastArmed = 0;
        std::chrono::steady_clock::time_point _now;
        bool _stopped = false;
    };
} // namespace Framework::Scripting
