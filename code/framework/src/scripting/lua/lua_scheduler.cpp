/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_scheduler.h"

#include "lua_runtime.h"

#include <lauxlib.h>

#include <algorithm>
#include <vector>

// How a coroutine runs.
//
// Every stretch goes through LuaRuntime::Execute: a protected call on the main thread of Enter, which resumes the
// coroutine. That arms the deadline as for any entry, refuses an entry nested in one whose time is up, and takes
// what Lua raises while the scheduler sets a coroutine up: with the memory cap reached, a tick reports "not enough
// memory" and the state goes on. The coroutine itself runs Body, the script's function under a protected call, for
// the reasons lua_sandbox.cpp gives for coroutine.create: an error leaves it with hooks on and its to-be-closed
// variables closed. What the scheduler does with no protected call around it allocates nothing in the state and
// runs nothing of the script, but for the one case Retire names.

namespace Framework::Scripting {
    namespace {
        // The registry keeps, under these addresses, the thread of every coroutine that has not ended and the
        // { callback, arguments... } of every timer. Not const: equal constants may be given one address.
        char kThreads = 0;
        char kTimers  = 0;

        // The longest a timer waits: what fits the 32 bits JavaScript gives a delay, 24 days.
        constexpr lua_Number kMaxDelayMs = 2147483647;

        constexpr const char *kStrayYield = "attempt to yield from outside a coroutine";

        // A delay as the JavaScript timers read it: 0 when it is missing, negative or not a number. Whole
        // milliseconds.
        std::chrono::milliseconds Delay(lua_State *L, int index) {
            const lua_Number milliseconds = lua_tonumber(L, index);
            if (!(milliseconds >= 1)) {
                return std::chrono::milliseconds(0);
            }
            return std::chrono::milliseconds(static_cast<int64_t>(std::min(milliseconds, kMaxDelayMs)));
        }
    } // namespace

    struct LuaScheduler::Entry final {
        enum class End {
            Failed,
            Finished,
            Waiting
        };

        LuaScheduler *scheduler = nullptr;

        // The coroutine to resume, and with what. Without one the entry starts a coroutine, which is then set
        // here: for the callback of `timer`, or else for the function and the arguments on the stack.
        lua_State *thread = nullptr;
        Wakeup wakeup;
        uint64_t timer = 0;
        int timerArgs  = 0;
        Finished done;

        End end = End::Failed;
        std::string error;
        // What a __close handler raised when the coroutine had to be closed in the entry.
        std::string closing;
    };

    class LuaScheduler::Working final {
      public:
        explicit Working(LuaScheduler &scheduler): _scheduler(scheduler) {
            ++_scheduler._working;
        }

        ~Working() {
            --_scheduler._working;
        }

        Working(const Working &)            = delete;
        Working &operator=(const Working &) = delete;

      private:
        LuaScheduler &_scheduler;
    };

    struct LuaScheduler::Globals final {
        static LuaScheduler &Of(lua_State *L) {
            return LuaRuntime::From(L)->Scheduler();
        }

        static int SetTimer(lua_State *L, const char *name, bool repeats) {
            if (lua_type(L, 1) != LUA_TFUNCTION) {
                return luaL_error(L, "%s: callback must be a function", name);
            }
            LuaScheduler &self = Of(L);
            if (self._timers.size() >= kMaxTimers) {
                return luaL_error(L, "%s: too many timers (the limit is %d)", name, static_cast<int>(kMaxTimers));
            }
            std::chrono::milliseconds delay = Delay(L, 2);
            if (repeats && delay.count() < 1) {
                delay = std::chrono::milliseconds(1);
            }

            const int nargs = std::max(lua_gettop(L) - 2, 0);
            lua_createtable(L, nargs + 1, 0);
            lua_pushvalue(L, 1);
            lua_rawseti(L, -2, 1);
            for (int i = 1; i <= nargs; ++i) {
                lua_pushvalue(L, 2 + i);
                lua_rawseti(L, -2, 1 + i);
            }
            // Whatever raises does so before the timer exists.
            const uint64_t id = self._lastTimer + 1;
            lua_rawgetp(L, LUA_REGISTRYINDEX, &kTimers);
            lua_insert(L, -2);
            lua_rawseti(L, -2, static_cast<lua_Integer>(id));

            self._lastTimer = id;
            Timer timer;
            timer.due      = self.Arm(self._now + delay, Pending {id, 0});
            timer.interval = repeats ? delay : std::chrono::milliseconds(0);
            timer.nargs    = nargs;
            self._timers.emplace(id, timer);

            lua_pushinteger(L, static_cast<lua_Integer>(id));
            return 1;
        }

        static int SetTimeout(lua_State *L) {
            return SetTimer(L, "setTimeout", false);
        }

        static int SetInterval(lua_State *L) {
            return SetTimer(L, "setInterval", true);
        }

        // clearTimeout and clearInterval. What is not the id of a pending timer is ignored, as in JavaScript.
        static int ClearTimer(lua_State *L) {
            int isInteger        = 0;
            const lua_Integer id = lua_type(L, 1) == LUA_TNUMBER ? lua_tointegerx(L, 1, &isInteger) : 0;
            if (isInteger != 0 && id > 0) {
                Of(L).Clear(L, static_cast<uint64_t>(id));
            }
            return 0;
        }

        static int Sleep(lua_State *L) {
            LuaScheduler &self   = Of(L);
            const auto delay     = Delay(L, 1);
            const uint64_t token = self.Suspend(L, "sleep");
            self.Arm(self._now + delay, Pending {0, token});
            return Wait(L);
        }

        // The function runs before spawn returns, as far as its first wait. What it raises is its own: the caller
        // is not told, unless it was the deadline.
        static int Spawn(lua_State *L) {
            if (lua_type(L, 1) != LUA_TFUNCTION) {
                return luaL_error(L, "spawn: callback must be a function");
            }
            LuaScheduler &self = Of(L);
            lua_State *main    = self._runtime.State();
            const int values   = lua_gettop(L);
            if (!lua_checkstack(main, values)) {
                return luaL_error(L, "spawn: too many arguments");
            }
            lua_xmove(L, main, values);
            self.Start(values - 1);
            if (self._runtime.PastDeadline()) {
                return LuaRuntime::RaiseDeadline(L, 1);
            }
            return 0;
        }
    };

    LuaScheduler::LuaScheduler(LuaRuntime &runtime): _runtime(runtime), _now(std::chrono::steady_clock::now()) {}

    LuaScheduler::~LuaScheduler() = default;

    void LuaScheduler::Install(lua_State *L) {
        lua_newtable(L);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &kThreads);
        lua_newtable(L);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &kTimers);

        lua_pushglobaltable(L);
        static const luaL_Reg globals[] = {
            {"setTimeout", Globals::SetTimeout},
            {"setInterval", Globals::SetInterval},
            {"clearTimeout", Globals::ClearTimer},
            {"clearInterval", Globals::ClearTimer},
            {"sleep", Globals::Sleep},
            {"spawn", Globals::Spawn},
            {nullptr, nullptr},
        };
        luaL_setfuncs(L, globals, 0);
        lua_pop(L, 1);
    }

    void LuaScheduler::Start(int nargs, Finished done) {
        Begin(nargs, std::move(done), nullptr);
    }

    bool LuaScheduler::Begin(int nargs, Finished done, std::string *error) {
        Entry entry;
        entry.done = std::move(done);
        return Run(entry, nargs + 1, error);
    }

    uint64_t LuaScheduler::Suspend(lua_State *thread, const char *function) {
        const char *name      = function != nullptr ? function : "";
        const char *separator = function != nullptr ? ": " : "";
        if (!lua_isyieldable(thread)) {
            luaL_error(thread, "%s%scannot wait here", name, separator);
            return 0;
        }
        const auto found = _coroutines.find(thread);
        if (found == _coroutines.end()) {
            luaL_error(thread, "%s%scannot wait here: called inside a coroutine the script created", name, separator);
            return 0;
        }

        // A token the coroutine never waited on, and what was resumed with it, are of no use any more.
        Coroutine &coroutine = found->second;
        if (coroutine.token != 0) {
            _tokens.erase(coroutine.token);
        }
        coroutine.early.reset();
        coroutine.token = ++_lastToken;
        _tokens.emplace(coroutine.token, thread);
        return coroutine.token;
    }

    int LuaScheduler::Wait(lua_State *thread) {
        LuaScheduler &self = LuaRuntime::From(thread)->Scheduler();
        const auto found   = self._coroutines.find(thread);
        if (found == self._coroutines.end() || (found->second.token == 0 && !found->second.early)) {
            return luaL_error(thread, "LuaScheduler::Wait: Suspend was not called");
        }
        Coroutine &coroutine = found->second;
        coroutine.waiting    = true;
        coroutine.raise      = false;
        coroutine.floor      = lua_gettop(thread);
        return lua_yieldk(thread, 0, 0, Resumed);
    }

    // Where the function that called Wait goes on: it returns what the resume put on its stack, or raises it
    // with the position of its caller in front.
    int LuaScheduler::Resumed(lua_State *L, int, lua_KContext) {
        Coroutine &coroutine = LuaRuntime::From(L)->Scheduler()._coroutines.find(L)->second;
        if (coroutine.raise) {
            coroutine.raise = false;
            luaL_where(L, 1);
            lua_insert(L, -2);
            lua_concat(L, 2);
            return lua_error(L);
        }
        return lua_gettop(L) - coroutine.floor;
    }

    void LuaScheduler::Resume(uint64_t token, Push push) {
        Wake(token, Wakeup {std::move(push), false});
    }

    void LuaScheduler::ResumeWithError(uint64_t token, const std::string &message) {
        Wake(token, Wakeup {[message](lua_State *thread) {
                                lua_pushlstring(thread, message.data(), message.size());
                                return 1;
                            },
                        true});
    }

    void LuaScheduler::Wake(uint64_t token, Wakeup wakeup) {
        const auto found = _tokens.find(token);
        if (_stopped || found == _tokens.end()) {
            return;
        }
        lua_State *thread = found->second;
        _tokens.erase(found);
        const auto record = _coroutines.find(thread);
        if (record == _coroutines.end()) {
            return;
        }
        Coroutine &coroutine = record->second;
        coroutine.token      = 0;
        if (lua_status(thread) != LUA_YIELD) {
            // The function that suspended it has not yielded yet. Drive hands this over when it does.
            coroutine.early = std::move(wakeup);
            return;
        }

        Entry entry;
        entry.thread = thread;
        entry.wakeup = std::move(wakeup);
        Run(entry, 0, nullptr);
    }

    size_t LuaScheduler::SuspendedCoroutines() const {
        return static_cast<size_t>(std::count_if(_coroutines.begin(), _coroutines.end(), [](const auto &coroutine) {
            return lua_status(coroutine.first) == LUA_YIELD;
        }));
    }

    void LuaScheduler::SetErrorHandler(ErrorHandler handler) {
        _errorHandler = std::move(handler);
    }

    bool LuaScheduler::Run(Entry &entry, int values, std::string *error) {
        // To the end: what is told below may ask for the runtime to be destroyed, with the coroutine still to
        // let go of.
        const Working working(*this);
        lua_State *L    = _runtime.State();
        entry.scheduler = this;
        if (_runtime._entryDepth >= kMaxNesting) {
            lua_pop(L, values);
            entry.error = "too many nested calls into the script (the limit is " + std::to_string(kMaxNesting) + ")";
        }
        else if (!lua_checkstack(L, 2)) {
            lua_pop(L, values);
            entry.error = "cannot grow the Lua stack";
        }
        else {
            lua_pushcfunction(L, Enter);
            lua_pushlightuserdata(L, &entry);
            lua_rotate(L, -(values + 2), 2);
            std::string raised;
            if (!_runtime.Execute(values + 1, 0, raised)) {
                entry.end   = Entry::End::Failed;
                entry.error = std::move(raised);
            }
        }

        if (entry.end == Entry::End::Waiting) {
            return true;
        }
        if (entry.thread == nullptr) {
            // No coroutine came of it.
            Report(entry.done, entry.error, error);
            return false;
        }

        // The coroutine has ended. `done` may start and resume others, so nothing of the map is held across it.
        const bool finished  = entry.end == Entry::End::Finished;
        Coroutine &coroutine = _coroutines.find(entry.thread)->second;
        Finished done        = std::move(coroutine.done);
        if (coroutine.token != 0) {
            _tokens.erase(coroutine.token);
            coroutine.token = 0;
        }
        if (!finished) {
            Report(done, entry.error, error);
        }
        else if (done) {
            done(true, entry.thread, entry.error);
        }
        Retire(entry.thread);
        if (!entry.closing.empty()) {
            Unhandled(entry.closing);
        }
        return finished;
    }

    // What Execute calls, on the main thread: `L` holds the entry and, for a coroutine to start, its function and
    // arguments.
    int LuaScheduler::Enter(lua_State *L) {
        Entry &entry       = *static_cast<Entry *>(lua_touserdata(L, 1));
        LuaScheduler &self = *entry.scheduler;
        int nargs          = 0;
        if (entry.thread != nullptr) {
            // What `push` raises it raises on a thread that is not running: Lua ends that coroutine, and the
            // error comes out of this entry.
            nargs = Deliver(self._coroutines.find(entry.thread)->second, entry.thread, entry.wakeup);
        }
        else {
            if (entry.timer != 0) {
                PushCallback(L, entry.timer, entry.timerArgs);
            }
            nargs        = lua_gettop(L) - 1;
            entry.thread = self.NewCoroutine(L, nargs, entry.done);
        }
        self.Drive(L, entry, nargs);
        return 0;
    }

    // Moves the function and its arguments, the `values` on top of `L`, to a new thread that runs them under
    // Body. `done` is taken once nothing can raise any more.
    lua_State *LuaScheduler::NewCoroutine(lua_State *L, int values, Finished &done) {
        lua_State *thread = lua_newthread(L);
        if (!lua_checkstack(thread, values + 1)) {
            luaL_error(L, "too many arguments");
        }
        lua_rawgetp(L, LUA_REGISTRYINDEX, &kThreads);
        lua_insert(L, -2);
        lua_rawsetp(L, -2, thread);
        lua_pop(L, 1);

        lua_pushcfunction(thread, Body);
        lua_xmove(L, thread, values);
        _coroutines[thread].done = std::move(done);
        return thread;
    }

    // Resumes the coroutine of `entry` with the `nargs` values on top of its stack, until it ends or waits.
    void LuaScheduler::Drive(lua_State *L, Entry &entry, int nargs) {
        // The coroutine inside which this one was started or resumed, if any. Put back however this is left.
        struct Running final {
            lua_State *&running;
            lua_State *outer;

            ~Running() {
                running = outer;
            }
        };

        lua_State *thread    = entry.thread;
        Coroutine &coroutine = _coroutines.find(thread)->second;
        const Running nesting {_running, std::exchange(_running, thread)};
        for (;;) {
            int nresults     = 0;
            const int status = lua_resume(thread, L, nargs, &nresults);
            if (status == LUA_OK) {
                // Its first result stays on top, for whoever waits on it.
                if (nresults == 0) {
                    lua_pushnil(thread);
                }
                else {
                    lua_pop(thread, nresults - 1);
                }
                entry.end = Entry::End::Finished;
                break;
            }
            if (status != LUA_YIELD) {
                entry.error = LuaRuntime::PopError(thread);
                break;
            }
            if (!coroutine.waiting) {
                // The script called coroutine.yield() with no coroutine of its own around it, where plain Lua is
                // on its main thread and raises. The coroutine cannot be made to raise from here: it is closed,
                // here where the deadline covers its __close handlers. Nothing in this can raise.
                entry.error   = LuaRuntime::Where(thread, 0) + kStrayYield;
                entry.closing = Close(thread);
                break;
            }
            if (!coroutine.early) {
                entry.end = Entry::End::Waiting;
                break;
            }
            // What it waits for was over before it yielded.
            Wakeup wakeup = std::move(*coroutine.early);
            coroutine.early.reset();
            nargs = Deliver(coroutine, thread, wakeup);
        }

        // An entry nested in another shares its deadline. When that has passed, whoever started this must not go
        // on either, even if host code in between takes the failure and returns: the thread it most likely
        // returns to is stopped at its next instruction.
        if (_runtime._entryDepth > 1 && _runtime.PastDeadline()) {
            lua_sethook(nesting.outer != nullptr ? nesting.outer : L, LuaRuntime::DeadlineHook, LUA_MASKCOUNT, 1);
        }
    }

    // Puts on `thread` what a resume brings, for Resumed to return or raise, and gives the number of values.
    int LuaScheduler::Deliver(Coroutine &coroutine, lua_State *thread, Wakeup &wakeup) {
        coroutine.waiting = false;
        coroutine.raise   = wakeup.raise;
        return wakeup.push ? wakeup.push(thread) : 0;
    }

    // An error is told to one party: the caller that asked for it, whoever waits on the coroutine, or the handler.
    void LuaScheduler::Report(Finished &done, const std::string &message, std::string *error) {
        if (error != nullptr) {
            *error = message;
        }
        else if (done) {
            done(false, nullptr, message);
        }
        else {
            Unhandled(message);
        }
    }

    // An error nobody waits on goes to the handler, or to the log at the rate the runtime writes there.
    void LuaScheduler::Unhandled(const std::string &message) {
        if (_errorHandler) {
            _errorHandler(message);
        }
        else {
            _runtime.LogLine(message, true);
        }
    }

    // Closes `thread` and gives what a __close handler raised while it did. Only a coroutine that still waits has
    // such handlers left: one that failed answers with the status it failed with, which is not news.
    std::string LuaScheduler::Close(lua_State *thread) {
        const bool waits = lua_status(thread) == LUA_YIELD;
        if (lua_closethread(thread, _runtime.State()) == LUA_OK || !waits) {
            return {};
        }
        return LuaRuntime::PopError(thread);
    }

    // Lets go of a coroutine that has ended. Closing it runs nothing of the script, unless it still waited: that
    // one was refused its resume, inside another entry, whose deadline its __close handlers then run under. From
    // here on a script may hold the thread, as a dead coroutine.
    void LuaScheduler::Retire(lua_State *thread) {
        lua_State *L             = _runtime.State();
        const std::string raised = Close(thread);
        if (lua_checkstack(L, 2)) {
            lua_rawgetp(L, LUA_REGISTRYINDEX, &kThreads);
            lua_pushnil(L);
            lua_rawsetp(L, -2, thread);
            lua_pop(L, 1);
        }
        // No token outlives the record it leads to.
        const auto found = _coroutines.find(thread);
        if (found->second.token != 0) {
            _tokens.erase(found->second.token);
        }
        _coroutines.erase(found);
        if (!raised.empty()) {
            Unhandled(raised);
        }
    }

    void LuaScheduler::Stop() {
        _stopped = true;
    }

    void LuaScheduler::Tick(std::chrono::steady_clock::time_point now) {
        if (_stopped) {
            return;
        }
        const Working working(*this);
        _now = now;

        // What is due as the tick starts. What a callback arms waits for the next tick, so an interval fires once
        // and a callback that sets a timer of no delay does not hold the tick for ever.
        std::vector<std::pair<Due, Pending>> due;
        for (auto queued = _queue.begin(); queued != _queue.end() && queued->first.first <= now; ++queued) {
            due.emplace_back(*queued);
        }
        for (const auto &[when, pending] : due) {
            // A callback that ran before may have stopped the scheduler.
            if (_stopped) {
                break;
            }
            // Or cleared it.
            if (_queue.erase(when) == 0) {
                continue;
            }
            if (pending.token != 0) {
                Wake(pending.token, Wakeup {});
            }
            else {
                Fire(pending.timer);
            }
        }
    }

    LuaScheduler::Due LuaScheduler::Arm(std::chrono::steady_clock::time_point at, Pending pending) {
        const Due due {at, ++_lastArmed};
        _queue.emplace(due, pending);
        return due;
    }

    // Runs the callback of a timer that came due. The timer is settled first, an interval armed again and a
    // timeout gone, so the callback may clear it, and what the callback raises does not change its schedule.
    void LuaScheduler::Fire(uint64_t id) {
        Timer &timer = _timers.find(id)->second;
        Entry entry;
        entry.timer     = id;
        entry.timerArgs = timer.nargs;
        const bool once = timer.interval.count() == 0;
        if (once) {
            _timers.erase(id);
        }
        else {
            // From the time it was due, so that it keeps its phase. The periods a late tick missed are skipped:
            // this firing stands for them.
            auto next = timer.due.first + timer.interval;
            if (next <= _now) {
                next += ((_now - next) / timer.interval + 1) * timer.interval;
            }
            timer.due = Arm(next, Pending {id, 0});
        }
        Run(entry, 0, nullptr);
        if (once) {
            Forget(_runtime.State(), id);
        }
    }

    void LuaScheduler::Clear(lua_State *L, uint64_t id) {
        const auto found = _timers.find(id);
        if (found == _timers.end()) {
            return;
        }
        _queue.erase(found->second.due);
        _timers.erase(found);
        Forget(L, id);
    }

    // Pushes the callback of a timer and its `nargs` arguments.
    void LuaScheduler::PushCallback(lua_State *L, uint64_t id, int nargs) {
        luaL_checkstack(L, nargs + 3, "too many arguments");
        lua_rawgetp(L, LUA_REGISTRYINDEX, &kTimers);
        lua_rawgeti(L, -1, static_cast<lua_Integer>(id));
        lua_remove(L, -2);
        const int packed = lua_gettop(L);
        for (int i = 1; i <= nargs + 1; ++i) {
            lua_rawgeti(L, packed, i);
        }
        lua_remove(L, packed);
    }

    // Drops what the registry holds for a timer. Clearing a field allocates nothing.
    void LuaScheduler::Forget(lua_State *L, uint64_t id) {
        if (!lua_checkstack(L, 2)) {
            return;
        }
        lua_rawgetp(L, LUA_REGISTRYINDEX, &kTimers);
        lua_pushnil(L);
        lua_rawseti(L, -2, static_cast<lua_Integer>(id));
        lua_pop(L, 1);
    }

    // What a coroutine runs: the function with its arguments, under a protected call that adds the traceback to
    // an error and raises it again, out of the coroutine.
    int LuaScheduler::Body(lua_State *L) {
        const int nargs = lua_gettop(L) - 1;
        lua_pushcfunction(L, LuaRuntime::Traceback);
        lua_insert(L, 1);
        const int status = lua_pcallk(L, nargs, LUA_MULTRET, 1, 0, FinishBody);
        return FinishBody(L, status, 0);
    }

    int LuaScheduler::FinishBody(lua_State *L, int status, lua_KContext) {
        if (status != LUA_OK && status != LUA_YIELD) {
            return lua_error(L);
        }
        return lua_gettop(L) - 1;
    }
} // namespace Framework::Scripting
