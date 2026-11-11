/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_runtime.h"

#include <lauxlib.h>
#include <lualib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

// What a script finds in its state. Nothing here reaches a file, a process, the environment or bytecode:
//
//   libraries   base, coroutine, table, string, utf8, math, and os cut down to its clock functions. No io, no
//               debug, no package, so no require until the runtime provides its own.
//   removed     dofile, loadfile, string.dump
//   replaced    load            string chunks only, always compiled as text
//               collectgarbage  answers "count" only
//               print           goes to the runtime's print handler, as do warnings
//               setmetatable    refuses a metatable with __gc
//               pcall, xpcall, coroutine.create, wrap, resume, close
//                               cannot be used to run past the deadline of an entry
//
// The last two are there for the deadline, which is a count hook: Lua runs no hook inside a finalizer, nor
// in what an error raised by a hook sets off before a protected call takes it, which is the message handler of an
// xpcall and the __close handlers of a coroutine that has no protected call. And a coroutine counts its own
// instructions, so work spread over many short ones is never counted at all. Hence:
//
//   - a metatable with __gc is refused, so no script code runs as a finalizer;
//   - a coroutine's function runs under a protected call, which is where Lua switches hooks back on;
//   - once the entry is being aborted, pcall, xpcall, coroutine.resume, coroutine.close and the function
//     coroutine.wrap gives raise instead of returning, an xpcall message handler is not called, and no coroutine
//     is resumed or closed. The thread they raise on is stopped at every instruction from then on, as the hook
//     does to the thread it catches (LuaRuntime::RaiseDeadline);
//   - whenever one coroutine gives control back to another, the clock is read.
//
// With no deadline on the state these behave as Lua's own, apart from the finalizer and from one thing the
// protected call brings: the pending to-be-closed variables of a coroutine that fails are closed at once, not
// when the coroutine is closed. pcall, xpcall, setmetatable and the four coroutine functions follow lbaselib.c and
// lcorolib.c of Lua 5.5 (Copyright (C) 1994-2025 Lua.org, PUC-Rio, MIT license).

namespace Framework::Scripting {
    namespace {
        constexpr int kLibraries = LUA_GLIBK | LUA_COLIBK | LUA_TABLIBK | LUA_STRLIBK | LUA_UTF8LIBK | LUA_MATHLIBK | LUA_OSLIBK;

        constexpr std::array<const char *, 4> kOsFunctions = {"time", "clock", "date", "difftime"};

        // load(chunk [, chunkname [, mode [, env]]]) as Lua's, for a string chunk. The mode is not read: a chunk is
        // compiled as text, so one that starts as bytecode fails the way Lua reports it for mode "t".
        int Load(lua_State *L) {
            if (lua_type(L, 1) != LUA_TSTRING) {
                return luaL_error(L, "load: only a string chunk can be loaded");
            }
            size_t length         = 0;
            const char *chunk     = lua_tolstring(L, 1, &length);
            const char *chunkName = luaL_optstring(L, 2, chunk);
            if (luaL_loadbufferx(L, chunk, length, chunkName, "t") != LUA_OK) {
                luaL_pushfail(L);
                lua_insert(L, -2);
                return 2;
            }
            if (!lua_isnone(L, 4)) {
                lua_pushvalue(L, 4);
                // The first upvalue of a chunk is its environment. A chunk without one leaves the value behind.
                if (lua_setupvalue(L, -2, 1) == nullptr) {
                    lua_pop(L, 1);
                }
            }
            return 1;
        }

        // A script reads how much it holds and leaves the collector alone.
        int CollectGarbage(lua_State *L) {
            if (lua_type(L, 1) != LUA_TSTRING || std::strcmp(lua_tostring(L, 1), "count") != 0) {
                return luaL_error(L, "collectgarbage: only \"count\" is available");
            }
            const int kilobytes = lua_gc(L, LUA_GCCOUNT);
            if (kilobytes == -1) {
                // Lua answers nothing while it collects, which is where a finalizer runs.
                luaL_pushfail(L);
                return 1;
            }
            lua_pushnumber(L, static_cast<lua_Number>(kilobytes) + static_cast<lua_Number>(lua_gc(L, LUA_GCCOUNTB)) / 1024);
            return 1;
        }

        // setmetatable(table, metatable) as Lua's, for a metatable without __gc. Lua looks the field up raw and
        // only when the metatable is set, so this is the one place a finalizer can come from.
        int SetMetatable(lua_State *L) {
            const int type = lua_type(L, 2);
            luaL_checktype(L, 1, LUA_TTABLE);
            luaL_argexpected(L, type == LUA_TNIL || type == LUA_TTABLE, 2, "nil or table");
            if (luaL_getmetafield(L, 1, "__metatable") != LUA_TNIL) {
                return luaL_error(L, "cannot change a protected metatable");
            }
            if (type == LUA_TTABLE) {
                lua_pushstring(L, "__gc");
                if (lua_rawget(L, 2) != LUA_TNIL) {
                    return luaL_error(L, "setmetatable: __gc is not available");
                }
            }
            lua_settop(L, 2);
            lua_setmetatable(L, 1);
            return 1;
        }

        void RemoveField(lua_State *L, int table, const char *name) {
            table = lua_absindex(L, table);
            lua_pushnil(L);
            lua_setfield(L, table, name);
        }

        // Removes every field of the table on top of the stack that `allowed` does not name.
        template <size_t N>
        void KeepFields(lua_State *L, const std::array<const char *, N> &allowed) {
            const int table = lua_gettop(L);
            lua_pushnil(L);
            while (lua_next(L, table) != 0) {
                lua_pop(L, 1);
                const bool keep = lua_type(L, -1) == LUA_TSTRING && std::any_of(allowed.begin(), allowed.end(), [name = lua_tostring(L, -1)](const char *allowedName) {
                    return std::strcmp(name, allowedName) == 0;
                });
                if (!keep) {
                    // Clearing a field during a traversal is allowed.
                    lua_pushvalue(L, -1);
                    lua_pushnil(L);
                    lua_rawset(L, table);
                }
            }
        }

        // What coroutine.status tells apart.
        enum class CoroutineState {
            Running,
            Dead,
            Suspended,
            Normal
        };

        CoroutineState StateOf(lua_State *L, lua_State *coroutine) {
            if (L == coroutine) {
                return CoroutineState::Running;
            }
            switch (lua_status(coroutine)) {
            case LUA_YIELD: return CoroutineState::Suspended;
            case LUA_OK: {
                lua_Debug activation;
                if (lua_getstack(coroutine, 0, &activation)) {
                    return CoroutineState::Normal;
                }
                // Not started yet when its function is still there.
                return lua_gettop(coroutine) == 0 ? CoroutineState::Dead : CoroutineState::Suspended;
            }
            default: return CoroutineState::Dead;
            }
        }

        lua_State *CheckCoroutine(lua_State *L) {
            lua_State *coroutine = lua_tothread(L, 1);
            luaL_argexpected(L, coroutine, 1, "thread");
            return coroutine;
        }

        // Resumes `coroutine` with the top `nargs` values of `L`. Leaves on `L` what it yielded or returned and
        // gives their number, or leaves the error and gives -1.
        int Resume(lua_State *L, lua_State *coroutine, int nargs) {
            if (!lua_checkstack(coroutine, nargs)) {
                lua_pushstring(L, "too many arguments to resume");
                return -1;
            }
            lua_xmove(L, coroutine, nargs);
            int nresults     = 0;
            const int status = lua_resume(coroutine, L, nargs, &nresults);
            if (status != LUA_OK && status != LUA_YIELD) {
                lua_xmove(coroutine, L, 1);
                return -1;
            }
            if (!lua_checkstack(L, nresults + 1)) {
                lua_pop(coroutine, nresults);
                lua_pushstring(L, "too many results to resume");
                return -1;
            }
            lua_xmove(coroutine, L, nresults);
            return nresults;
        }
    } // namespace

    struct LuaRuntime::Sandbox final {
        // Raises the deadline error from a function a script called. What the function had on its stack is of no
        // use any more, and dropping it makes room for the message.
        static int Abort(lua_State *L) {
            lua_settop(L, 0);
            return RaiseDeadline(L, 1);
        }

        // Whether the entry is already being aborted. This does not read the clock: it is asked where a script
        // stays on its own thread, which the hook watches.
        static bool Aborting(lua_State *L) {
            return From(L)->_expired;
        }

        // --- pcall and xpcall ---

        static int FinishProtectedCall(lua_State *L, int status, lua_KContext extra) {
            if (Aborting(L)) {
                return Abort(L);
            }
            if (status != LUA_OK && status != LUA_YIELD) {
                lua_pushboolean(L, 0);
                lua_pushvalue(L, -2);
                return 2;
            }
            return lua_gettop(L) - static_cast<int>(extra);
        }

        static int ProtectedCall(lua_State *L) {
            luaL_checkany(L, 1);
            lua_pushboolean(L, 1);
            lua_insert(L, 1);
            const int status = lua_pcallk(L, lua_gettop(L) - 2, LUA_MULTRET, 0, 0, FinishProtectedCall);
            return FinishProtectedCall(L, status, 0);
        }

        // Stands between Lua and the message handler of an xpcall. The error that aborts an entry is raised by the
        // hook, and Lua would run the handler with hooks off: it gets the error as it is instead.
        static int MessageHandler(lua_State *L) {
            if (Aborting(L)) {
                return 1;
            }
            lua_pushvalue(L, lua_upvalueindex(1));
            lua_insert(L, 1);
            lua_call(L, 1, 1);
            return 1;
        }

        static int ProtectedCallWithHandler(lua_State *L) {
            const int count = lua_gettop(L);
            luaL_checktype(L, 2, LUA_TFUNCTION);
            if (From(L)->HasDeadline()) {
                lua_pushvalue(L, 2);
                lua_pushcclosure(L, MessageHandler, 1);
                lua_replace(L, 2);
            }
            lua_pushboolean(L, 1);
            lua_pushvalue(L, 1);
            lua_rotate(L, 3, 2);
            const int status = lua_pcallk(L, count - 2, LUA_MULTRET, 2, 2, FinishProtectedCall);
            return FinishProtectedCall(L, status, 2);
        }

        // --- Coroutines ---

        static int FinishCoroutineFunction(lua_State *L, int status, lua_KContext) {
            if (status != LUA_OK && status != LUA_YIELD) {
                return lua_error(L);
            }
            return lua_gettop(L);
        }

        // What a coroutine runs: the script's function, under a protected call. An error then leaves the
        // coroutine from here with hooks on and its to-be-closed variables closed; one the hook raised straight
        // out of the coroutine would leave Lua running its __close handlers with hooks off.
        static int CoroutineFunction(lua_State *L) {
            const int nargs = lua_gettop(L);
            lua_pushvalue(L, lua_upvalueindex(1));
            lua_insert(L, 1);
            const int status = lua_pcallk(L, nargs, LUA_MULTRET, 0, 0, FinishCoroutineFunction);
            return FinishCoroutineFunction(L, status, 0);
        }

        static int CoroutineCreate(lua_State *L) {
            luaL_checktype(L, 1, LUA_TFUNCTION);
            lua_State *coroutine = lua_newthread(L);
            lua_pushvalue(L, 1);
            lua_pushcclosure(L, CoroutineFunction, 1);
            lua_xmove(L, coroutine, 1);
            return 1;
        }

        // A coroutine has its own instruction count, so a script could go on for ever in coroutines that each
        // stop short of it. Control coming back from one is where that is caught, by the clock.
        static bool CameBackTooLate(lua_State *L) {
            return From(L)->PastDeadline();
        }

        static int CoroutineResume(lua_State *L) {
            lua_State *coroutine = CheckCoroutine(L);
            if (Aborting(L)) {
                return Abort(L);
            }
            const int nresults = Resume(L, coroutine, lua_gettop(L) - 1);
            if (CameBackTooLate(L)) {
                return Abort(L);
            }
            if (nresults < 0) {
                lua_pushboolean(L, 0);
                lua_insert(L, -2);
                return 2;
            }
            lua_pushboolean(L, 1);
            lua_insert(L, -(nresults + 1));
            return nresults + 1;
        }

        // The function coroutine.wrap gives.
        static int WrappedResume(lua_State *L) {
            lua_State *coroutine = lua_tothread(L, lua_upvalueindex(1));
            if (Aborting(L)) {
                return Abort(L);
            }
            const int nresults = Resume(L, coroutine, lua_gettop(L));
            if (CameBackTooLate(L)) {
                return Abort(L);
            }
            if (nresults < 0) {
                int status = lua_status(coroutine);
                if (status != LUA_OK && status != LUA_YIELD) {
                    // The coroutine failed. Closing it leaves the error on its stack.
                    status = lua_closethread(coroutine, L);
                    lua_xmove(coroutine, L, 1);
                }
                if (status != LUA_ERRMEM && lua_type(L, -1) == LUA_TSTRING) {
                    luaL_where(L, 1);
                    lua_insert(L, -2);
                    lua_concat(L, 2);
                }
                return lua_error(L);
            }
            return nresults;
        }

        static int CoroutineWrap(lua_State *L) {
            CoroutineCreate(L);
            lua_pushcclosure(L, WrappedResume, 1);
            return 1;
        }

        static int CoroutineClose(lua_State *L) {
            lua_State *coroutine = lua_isnone(L, 1) ? L : CheckCoroutine(L);
            if (Aborting(L)) {
                return Abort(L);
            }
            switch (StateOf(L, coroutine)) {
            case CoroutineState::Dead:
            case CoroutineState::Suspended: {
                // This runs the __close handlers of the coroutine, on the coroutine.
                const int status = lua_closethread(coroutine, L);
                if (CameBackTooLate(L)) {
                    return Abort(L);
                }
                if (status == LUA_OK) {
                    lua_pushboolean(L, 1);
                    return 1;
                }
                lua_pushboolean(L, 0);
                lua_xmove(coroutine, L, 1);
                return 2;
            }
            case CoroutineState::Normal: return luaL_error(L, "cannot close a normal coroutine");
            case CoroutineState::Running:
                lua_geti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
                if (lua_tothread(L, -1) == coroutine) {
                    return luaL_error(L, "cannot close main thread");
                }
                // A coroutine closing itself ends here: Lua unwinds it to its resumer.
                lua_closethread(coroutine, L);
                break;
            }
            return 0;
        }

        // --- Output ---

        // print(...) as Lua's, to the print handler: the arguments spelled as tostring() would, joined with tabs,
        // up to the longest line a script may write. An argument whose __tostring raises unwinds this frame, and
        // nothing is printed.
        static int Print(lua_State *L) {
            const int count = lua_gettop(L);
            std::string line;
            bool whole = true;
            for (int i = 1; i <= count && whole; ++i) {
                size_t length    = 0;
                const char *text = luaL_tolstring(L, i, &length);
                whole            = (i == 1 || AppendBounded(line, "\t", 1, kMaxLineBytes)) && AppendBounded(line, text, length, kMaxLineBytes);
                lua_pop(L, 1);
            }
            if (!whole) {
                line += kTruncated;
            }
            From(L)->WriteLine(std::move(line));
            return 0;
        }

        // Lua hands a warning over in pieces, from warn() and for an error it cannot raise, such as one in a
        // finalizer. A whole message that starts with '@' is by convention a command to the warning system ("@on",
        // "@off"); warnings are always shown here, so those are dropped.
        static void Warn(void *runtime, const char *message, int toContinue) {
            auto *self = static_cast<LuaRuntime *>(runtime);
            if (!self->_warningContinues) {
                if (toContinue == 0 && message[0] == '@') {
                    return;
                }
                self->_warning      = "warning: ";
                self->_warningWhole = true;
            }
            self->_warningWhole     = self->_warningWhole && AppendBounded(self->_warning, message, std::strlen(message), kMaxLineBytes);
            self->_warningContinues = toContinue != 0;
            if (!self->_warningContinues) {
                if (!self->_warningWhole) {
                    self->_warning += kTruncated;
                }
                self->WriteLine(std::move(self->_warning));
                self->_warning.clear();
            }
        }
    };

    int LuaRuntime::OpenSandbox(lua_State *L) {
        luaL_openselectedlibs(L, kLibraries, 0);

        lua_pushglobaltable(L);
        const int globals = lua_gettop(L);
        RemoveField(L, globals, "dofile");
        RemoveField(L, globals, "loadfile");
        static const luaL_Reg base[] = {
            {"load", Load},
            {"collectgarbage", CollectGarbage},
            {"print", Sandbox::Print},
            {"setmetatable", SetMetatable},
            {"pcall", Sandbox::ProtectedCall},
            {"xpcall", Sandbox::ProtectedCallWithHandler},
            {nullptr, nullptr},
        };
        luaL_setfuncs(L, base, 0);

        lua_getfield(L, globals, LUA_COLIBNAME);
        static const luaL_Reg coroutines[] = {
            {"create", Sandbox::CoroutineCreate},
            {"wrap", Sandbox::CoroutineWrap},
            {"resume", Sandbox::CoroutineResume},
            {"close", Sandbox::CoroutineClose},
            {nullptr, nullptr},
        };
        luaL_setfuncs(L, coroutines, 0);

        lua_getfield(L, globals, LUA_STRLIBNAME);
        RemoveField(L, -1, "dump");
        lua_getfield(L, globals, LUA_OSLIBNAME);
        KeepFields(L, kOsFunctions);
        lua_pop(L, 4);

        lua_setwarnf(L, Sandbox::Warn, From(L));
        return 0;
    }
} // namespace Framework::Scripting
