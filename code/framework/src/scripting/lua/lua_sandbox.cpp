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
    } // namespace

    int LuaRuntime::OpenSandbox(lua_State *L) {
        luaL_openselectedlibs(L, kLibraries, 0);

        lua_pushglobaltable(L);
        const int globals = lua_gettop(L);
        RemoveField(L, globals, "dofile");
        RemoveField(L, globals, "loadfile");
        static const luaL_Reg replaced[] = {{"load", Load}, {"collectgarbage", CollectGarbage}, {"print", Print}, {nullptr, nullptr}};
        luaL_setfuncs(L, replaced, 0);

        lua_getfield(L, globals, LUA_STRLIBNAME);
        RemoveField(L, -1, "dump");
        lua_getfield(L, globals, LUA_OSLIBNAME);
        KeepFields(L, kOsFunctions);
        lua_pop(L, 3);

        lua_setwarnf(L, Warn, From(L));
        return 0;
    }

    // print(...) as Lua's, to the print handler: the arguments spelled as tostring() would, joined with tabs. An
    // argument whose __tostring raises unwinds this frame, and nothing is printed.
    int LuaRuntime::Print(lua_State *L) {
        const int count = lua_gettop(L);
        std::string line;
        for (int i = 1; i <= count; ++i) {
            size_t length    = 0;
            const char *text = luaL_tolstring(L, i, &length);
            if (i > 1) {
                line += '\t';
            }
            line.append(text, length);
            lua_pop(L, 1);
        }
        From(L)->_printHandler(line);
        return 0;
    }

    // Lua hands a warning over in pieces, from warn() and for an error it cannot raise, such as one in a finalizer.
    // A whole message that starts with '@' is by convention a command to the warning system ("@on", "@off");
    // warnings are always shown here, so those are dropped.
    void LuaRuntime::Warn(void *runtime, const char *message, int toContinue) {
        auto *self = static_cast<LuaRuntime *>(runtime);
        if (!self->_warningContinues && toContinue == 0 && message[0] == '@') {
            return;
        }
        self->_warning += message;
        self->_warningContinues = toContinue != 0;
        if (!self->_warningContinues) {
            self->_printHandler("warning: " + self->_warning);
            self->_warning.clear();
        }
    }
} // namespace Framework::Scripting
