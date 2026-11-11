/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_runtime.h"

#include "logging/logger.h"

#include <lauxlib.h>

#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace Framework::Scripting {
    namespace {
        constexpr const char *kDeadlineMessage = "script ran too long";

        // The message handler of every entry: the error as text, then where it was raised. An error that is not a
        // string is spelled as tostring() would.
        int Traceback(lua_State *L) {
            const char *message = lua_type(L, 1) == LUA_TSTRING ? lua_tostring(L, 1) : luaL_tolstring(L, 1, nullptr);
            luaL_traceback(L, L, message, 1);
            return 1;
        }

        // Takes the error off the stack. Nothing here allocates in the state, which may have no memory left.
        std::string PopError(lua_State *L) {
            std::string error;
            if (lua_type(L, -1) == LUA_TSTRING) {
                size_t length    = 0;
                const char *text = lua_tolstring(L, -1, &length);
                error.assign(text, length);
            }
            else {
                error = std::string("(error object is a ") + luaL_typename(L, -1) + " value)";
            }
            lua_pop(L, 1);
            return error;
        }
    } // namespace

    LuaRuntime::LuaRuntime(LuaRuntimeOptions options): _options(std::move(options)), _printHandler(DefaultPrintHandler()) {
        _state = lua_newstate(Allocate, this, luaL_makeseed(nullptr));
        if (_state == nullptr) {
            throw std::runtime_error("LuaRuntime: the memory limit of '" + _options.resourceName + "' is too small for a Lua state");
        }

        // A coroutine starts with a copy of this, so From() reads it on any thread.
        *static_cast<LuaRuntime **>(lua_getextraspace(_state)) = this;

        lua_pushcfunction(_state, OpenSandbox);
        if (lua_pcall(_state, 0, 0, 0) != LUA_OK) {
            const std::string error = PopError(_state);
            lua_close(_state);
            throw std::runtime_error("LuaRuntime: cannot set up the Lua state of '" + _options.resourceName + "': " + error);
        }

        // Installed for good: a coroutine inherits the hook of the thread that creates it.
        if (_options.entryDeadline.count() > 0) {
            lua_sethook(_state, DeadlineHook, LUA_MASKCOUNT, kHookInterval);
        }
    }

    LuaRuntime::~LuaRuntime() {
        lua_close(_state);
    }

    LuaRuntime *LuaRuntime::From(lua_State *L) {
        return *static_cast<LuaRuntime **>(lua_getextraspace(L));
    }

    bool LuaRuntime::RunString(std::string_view source, std::string_view chunkName, std::string &error) {
        const std::string name(chunkName);
        if (luaL_loadbufferx(_state, source.data(), source.size(), name.c_str(), "t") != LUA_OK) {
            error = PopError(_state);
            return false;
        }
        return Execute(0, 0, error);
    }

    bool LuaRuntime::Call(int nargs, int nresults, std::string &error) {
        return Execute(nargs, nresults, error);
    }

    void LuaRuntime::SetPrintHandler(PrintHandler handler) {
        _printHandler = handler ? std::move(handler) : DefaultPrintHandler();
    }

    LuaRuntime::PrintHandler LuaRuntime::DefaultPrintHandler() const {
        return [resourceName = _options.resourceName](const std::string &line) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("[{}] {}", resourceName, line);
        };
    }

    bool LuaRuntime::Execute(int nargs, int nresults, std::string &error) {
        const int function = lua_gettop(_state) - nargs;
        if (!lua_checkstack(_state, 1)) {
            lua_settop(_state, function - 1);
            error = "cannot grow the Lua stack";
            return false;
        }
        lua_pushcfunction(_state, Traceback);
        lua_insert(_state, function);

        if (_entryDepth++ == 0 && _options.entryDeadline.count() > 0) {
            _deadline = std::chrono::steady_clock::now() + _options.entryDeadline;
        }
        const int status = lua_pcall(_state, nargs, nresults, function);
        --_entryDepth;

        if (status != LUA_OK) {
            error = PopError(_state);
        }
        lua_remove(_state, function);
        return status == LUA_OK;
    }

    // Counts the bytes handed out, which growth alone can take past the cap. A block can always be freed or made
    // smaller, and Lua relies on that.
    void *LuaRuntime::Allocate(void *runtime, void *block, size_t oldSize, size_t newSize) {
        auto *self = static_cast<LuaRuntime *>(runtime);
        // Without a block, `oldSize` says what kind of object is being created.
        const size_t held = block != nullptr ? oldSize : 0;
        if (newSize == 0) {
            std::free(block);
            self->_memoryInUse -= held;
            return nullptr;
        }

        const size_t limit = self->_options.memoryLimitBytes;
        if (limit != 0 && newSize > held && newSize - held > limit - self->_memoryInUse) {
            return nullptr;
        }

        void *resized = std::realloc(block, newSize);
        if (resized != nullptr) {
            self->_memoryInUse = self->_memoryInUse - held + newSize;
        }
        return resized;
    }

    // Past the deadline the thread is made to stop at every instruction, so that code which catches the error is
    // raised on again before it does anything: pcall cannot keep an entry alive. The next entry finds the thread
    // counting single instructions and puts the interval back.
    void LuaRuntime::DeadlineHook(lua_State *L, lua_Debug *) {
        const LuaRuntime *runtime = From(L);
        if (runtime->_entryDepth == 0 || std::chrono::steady_clock::now() < runtime->_deadline) {
            if (lua_gethookcount(L) != kHookInterval) {
                lua_sethook(L, DeadlineHook, LUA_MASKCOUNT, kHookInterval);
            }
            return;
        }

        lua_sethook(L, DeadlineHook, LUA_MASKCOUNT, 1);
        // A hook runs in the activation it interrupted, so level 0 is the Lua code that ran too long.
        luaL_where(L, 0);
        lua_pushstring(L, kDeadlineMessage);
        lua_concat(L, 2);
        lua_error(L);
    }
} // namespace Framework::Scripting
