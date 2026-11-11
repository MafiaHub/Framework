/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_runtime.h"

#include "lua_scheduler.h"

#include "logging/logger.h"

#include <lauxlib.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace Framework::Scripting {
    namespace {
        constexpr const char *kDeadlineMessage = "script ran too long";

        // The message of an error keeps half of what an error text may hold, which leaves the traceback its room.
        constexpr size_t kMaxTracedMessageBytes = LuaRuntime::kMaxErrorBytes / 2;

        // A space for each control character but the tab, so that a script cannot start a line of its own in the
        // log or drive the terminal that shows it. That covers the C1 controls as UTF-8 spells them.
        void BlankControlCharacters(std::string &line) {
            for (size_t i = 0; i < line.size(); ++i) {
                const auto byte = static_cast<unsigned char>(line[i]);
                if ((byte < 0x20 && byte != '\t') || byte == 0x7F) {
                    line[i] = ' ';
                }
                else if (byte == 0xC2 && i + 1 < line.size() && static_cast<unsigned char>(line[i + 1]) >= 0x80 && static_cast<unsigned char>(line[i + 1]) <= 0x9F) {
                    line[i]     = ' ';
                    line[i + 1] = ' ';
                }
            }
        }
    } // namespace

    LuaRuntime::LuaRuntime(LuaRuntimeOptions options): _options(std::move(options)) {
        _state = lua_newstate(Allocate, this, luaL_makeseed(nullptr));
        if (_state == nullptr) {
            const char *reason = _options.memoryLimitBytes != 0 ? "its memory limit is too small for a Lua state" : "out of memory";
            throw std::runtime_error("LuaRuntime: cannot create the Lua state of '" + _options.resourceName + "': " + reason);
        }
        lua_atpanic(_state, Panic);

        // A coroutine starts with a copy of this, so From() reads it on any thread.
        *static_cast<LuaRuntime **>(lua_getextraspace(_state)) = this;

        _scheduler = std::make_unique<LuaScheduler>(*this);
        lua_pushcfunction(_state, Open);
        if (lua_pcall(_state, 0, 0, 0) != LUA_OK) {
            const std::string error = PopError(_state);
            lua_close(_state);
            throw std::runtime_error("LuaRuntime: cannot set up the Lua state of '" + _options.resourceName + "': " + error);
        }

        // Installed for good: a coroutine inherits the hook of the thread that creates it.
        if (HasDeadline()) {
            lua_sethook(_state, DeadlineHook, LUA_MASKCOUNT, kHookInterval);
        }
    }

    LuaRuntime::~LuaRuntime() {
        _scheduler.reset();
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
        return _scheduler->Begin(0, {}, &error);
    }

    bool LuaRuntime::Call(int nargs, int nresults, std::string &error) {
        return Execute(nargs, nresults, error);
    }

    void LuaRuntime::SetPrintHandler(PrintHandler handler) {
        _printHandler = std::move(handler);
    }

    void LuaRuntime::Tick(std::chrono::steady_clock::time_point now) {
        _scheduler->Tick(now);
    }

    int LuaRuntime::Open(lua_State *L) {
        OpenSandbox(L);
        From(L)->_scheduler->Install(L);
        return 0;
    }

    bool LuaRuntime::Execute(int nargs, int nresults, std::string &error) {
        const int function = lua_gettop(_state) - nargs;
        // Host code that takes the failure of a nested entry and returns must not be a way to go on.
        if (PastDeadline()) {
            lua_settop(_state, function - 1);
            error = kDeadlineMessage;
            return false;
        }
        // The handler, and the results a call leaves beyond what it took.
        if (!lua_checkstack(_state, 1 + std::max(nresults, 0))) {
            lua_settop(_state, function - 1);
            error = "cannot grow the Lua stack";
            return false;
        }
        lua_pushcfunction(_state, Traceback);
        lua_insert(_state, function);

        if (_entryDepth++ == 0 && HasDeadline()) {
            _deadline = std::chrono::steady_clock::now() + _options.entryDeadline;
        }
        const int status = lua_pcall(_state, nargs, nresults, function);
        if (--_entryDepth == 0) {
            _expired = false;
        }

        if (status != LUA_OK) {
            error = PopError(_state);
        }
        lua_remove(_state, function);
        return status == LUA_OK;
    }

    // The message handler of every entry: the error as text, then where it was raised. An error that is not a string
    // is spelled as tostring() would.
    int LuaRuntime::Traceback(lua_State *L) {
        size_t length       = 0;
        const char *message = lua_type(L, 1) == LUA_TSTRING ? lua_tolstring(L, 1, &length) : luaL_tolstring(L, 1, &length);
        if (length > kMaxTracedMessageBytes) {
            lua_pushlstring(L, message, kMaxTracedMessageBytes);
            lua_pushstring(L, kTruncated);
            lua_concat(L, 2);
            message = lua_tostring(L, -1);
        }
        luaL_traceback(L, L, message, 1);
        return 1;
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

    // Reached by an error raised where no protected call can take it, which host code alone can do. Lua aborts the
    // process when this returns; an exception takes the error to that host code instead, and leaves a state whose
    // stack nobody unwound.
    int LuaRuntime::Panic(lua_State *L) {
        throw std::runtime_error("LuaRuntime: unprotected Lua error: " + PopError(L));
    }

    bool LuaRuntime::PastDeadline() const {
        return HasDeadline() && _entryDepth > 0 && (_expired || std::chrono::steady_clock::now() >= _deadline);
    }

    void LuaRuntime::DeadlineHook(lua_State *L, lua_Debug *) {
        if (From(L)->PastDeadline()) {
            // A hook runs in the activation it interrupted, so level 0 is the Lua code that ran too long.
            RaiseDeadline(L, 0);
            return;
        }
        // The next entry finds a thread that was stopped at every instruction, and puts the interval back.
        if (lua_gethookcount(L) != kHookInterval) {
            lua_sethook(L, DeadlineHook, LUA_MASKCOUNT, kHookInterval);
        }
    }

    // From here to the end of the entry, `L` is stopped at every instruction: whatever of the script still gets
    // to run on it, a __close handler for one, is raised on again before it does anything.
    int LuaRuntime::RaiseDeadline(lua_State *L, int level) {
        LuaRuntime *runtime = From(L);
        if (!runtime->_expired) {
            runtime->_expired = true;
            runtime->_deadlineWhere.clear();
            // The nearest Lua code: a function of the sandbox may have been called by another C function.
            lua_Debug activation;
            for (; lua_getstack(L, level, &activation) != 0; ++level) {
                lua_getinfo(L, "Sl", &activation);
                if (activation.currentline > 0) {
                    runtime->_deadlineWhere = std::string(activation.short_src) + ":" + std::to_string(activation.currentline) + ": ";
                    break;
                }
            }
        }
        lua_sethook(L, DeadlineHook, LUA_MASKCOUNT, 1);
        lua_pushfstring(L, "%s%s", runtime->_deadlineWhere.c_str(), kDeadlineMessage);
        return lua_error(L);
    }

    bool LuaRuntime::AppendBounded(std::string &text, const char *data, size_t length, size_t limit) {
        const size_t room = limit - std::min(limit, text.size());
        text.append(data, std::min(length, room));
        return length <= room;
    }

    // Nothing here allocates in the state, which may have no memory left.
    std::string LuaRuntime::PopError(lua_State *L) {
        std::string error;
        if (lua_type(L, -1) == LUA_TSTRING) {
            size_t length    = 0;
            const char *text = lua_tolstring(L, -1, &length);
            if (!AppendBounded(error, text, length, kMaxErrorBytes)) {
                error += kTruncated;
            }
        }
        else {
            error = std::string("(error object is a ") + luaL_typename(L, -1) + " value)";
        }
        lua_pop(L, 1);
        return error;
    }

    void LuaRuntime::WriteLine(std::string line) {
        BlankControlCharacters(line);
        if (_printHandler) {
            _printHandler(line);
        }
        else {
            LogLine(line);
        }
    }

    // A script that prints in a loop would otherwise fill the log as fast as the disk takes it.
    void LuaRuntime::LogLine(const std::string &line) {
        const auto now = std::chrono::steady_clock::now();
        if (now - _logSecond >= std::chrono::seconds(1)) {
            _logSecond = now;
            _logLines  = 0;
        }
        if (_logLines > kMaxLinesPerSecond) {
            return;
        }
        const auto logger = Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING);
        if (_logLines++ < kMaxLinesPerSecond) {
            logger->info("[{}] {}", _options.resourceName, line);
        }
        else {
            logger->warn("[{}] output suppressed: more than {} lines in a second", _options.resourceName, kMaxLinesPerSecond);
        }
    }
} // namespace Framework::Scripting
