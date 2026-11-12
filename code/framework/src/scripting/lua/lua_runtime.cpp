/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_runtime.h"

#include "lua_scheduler.h"

#include "../output_level.h"
#include "../script_engine.h"
#include "../script_log.h"

#include "logging/logger.h"

#include <lauxlib.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Framework::Scripting {
    namespace {
        constexpr const char *kDeadlineMessage = "script ran too long";

        // The registry keeps, under these addresses, what every module that was required gave, by its name, and
        // the metatable of what marks a module as loading. Not const: equal constants may be given one address.
        char kModules     = 0;
        char kLoadingMeta = 0;

        // What the registry holds in place of the value of a module while its chunk runs.
        char kLoading = 0;

        // LoadChunk found no file for the module.
        constexpr int kNoModule = -1;

        // The longest name a module has. The host builds its paths from it.
        constexpr size_t kMaxModuleNameBytes = 256;

        constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";

        // `source` past the UTF-8 byte order mark some editors start a file with, which Lua does not read.
        std::string_view WithoutByteOrderMark(std::string_view source) {
            return source.starts_with(kByteOrderMark) ? source.substr(kByteOrderMark.size()) : source;
        }

        // Letters, digits, '_' and '-' in parts that dots join: "a.b" names a/b.lua and nothing outside the
        // resource, since no part is empty and none holds a separator.
        bool IsModuleName(std::string_view name) {
            bool partStarts = true;
            for (const char character : name) {
                if (character == '.') {
                    if (partStarts) {
                        return false;
                    }
                    partStarts = true;
                    continue;
                }
                const bool allowed = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_' || character == '-';
                if (!allowed) {
                    return false;
                }
                partStarts = false;
            }
            return !partStarts;
        }

        // The message of an error keeps half of what an error text may hold, which leaves the traceback its room.
        constexpr size_t kMaxTracedMessageBytes = LuaRuntime::kMaxErrorBytes / 2;

        // An error that leaves the runtime for the log of the manager is held to what it is held to here.
        static_assert(kMaxScriptErrorsPerSecond == LuaRuntime::kMaxLinesPerSecond);
        static_assert(kMaxScriptErrorBytes == LuaRuntime::kMaxErrorBytes);
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
        source = WithoutByteOrderMark(source);
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

    void LuaRuntime::SetModuleLoader(ModuleLoader loader) {
        _moduleLoader = std::move(loader);
    }

    struct LuaRuntime::Modules final {
        // Pushes the chunk of the module `name`, compiled as text from the first of its two files that the loader
        // has, and gives LUA_OK. Otherwise kNoModule, or the status of a chunk that does not compile with its
        // message pushed. Nothing here raises, so the strings do not stand in the way of an error.
        static int LoadChunk(lua_State *L, const LuaRuntime &runtime, std::string_view name) {
            std::string path(name);
            std::replace(path.begin(), path.end(), '.', '/');
            std::string source;
            for (const char *suffix : {".lua", "/init.lua"}) {
                const std::string file = path + suffix;
                if (runtime._moduleLoader(file, source)) {
                    const std::string chunkName = "@" + runtime._options.resourceName + "/" + file;
                    const std::string_view text = WithoutByteOrderMark(source);
                    return luaL_loadbufferx(L, text.data(), text.size(), chunkName.c_str(), "t");
                }
            }
            return kNoModule;
        }

        static int Require(lua_State *L) {
            if (lua_type(L, 1) != LUA_TSTRING) {
                return luaL_error(L, "require: module name must be a string");
            }
            lua_settop(L, 1);
            size_t length    = 0;
            const char *name = lua_tolstring(L, 1, &length);
            if (length > kMaxModuleNameBytes) {
                return luaL_error(L, "require: module name is longer than %d bytes", static_cast<int>(kMaxModuleNameBytes));
            }
            if (!IsModuleName(std::string_view(name, length))) {
                return luaL_error(L, "require: invalid module name '%s'", name);
            }
            const LuaRuntime *runtime = From(L);
            if (!runtime->_moduleLoader) {
                return luaL_error(L, "require: no module loader");
            }

            lua_rawgetp(L, LUA_REGISTRYINDEX, &kModules);
            const int modules = lua_gettop(L);
            lua_pushvalue(L, 1);
            const int kept = lua_rawget(L, modules);
            if (kept == LUA_TLIGHTUSERDATA && lua_touserdata(L, -1) == &kLoading) {
                return luaL_error(L, "require: cyclic dependency on '%s'", name);
            }
            if (kept != LUA_TNIL) {
                return 1;
            }
            lua_pop(L, 1);

            const int status = LoadChunk(L, *runtime, std::string_view(name, length));
            if (status == kNoModule) {
                return luaL_error(L, "module '%s' not found in resource '%s'", name, runtime->_options.resourceName.c_str());
            }
            if (status != LUA_OK) {
                return lua_error(L);
            }
            const int chunk = lua_gettop(L);

            // The module is marked as loading for as long as its chunk runs. The mark is taken back by a value
            // Lua closes however this call is left, so a module that fails can be required again.
            lua_createtable(L, 1, 0);
            lua_pushvalue(L, 1);
            lua_rawseti(L, -2, 1);
            lua_rawgetp(L, LUA_REGISTRYINDEX, &kLoadingMeta);
            lua_setmetatable(L, -2);
            lua_toclose(L, -1);
            lua_pushvalue(L, 1);
            lua_pushlightuserdata(L, &kLoading);
            lua_rawset(L, modules);

            // Without a continuation, so the chunk cannot yield: a module is loaded or it is not.
            lua_pushvalue(L, chunk);
            lua_pushvalue(L, 1);
            lua_call(L, 1, 1);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_pushboolean(L, 1);
            }
            lua_pushvalue(L, 1);
            lua_pushvalue(L, -2);
            lua_rawset(L, modules);
            return 1;
        }

        // The __close of what marks a module as loading: takes the mark back when the chunk did not get to its end.
        static int EndLoading(lua_State *L) {
            lua_rawgetp(L, LUA_REGISTRYINDEX, &kModules);
            lua_rawgeti(L, 1, 1);
            lua_pushvalue(L, -1);
            if (lua_rawget(L, -3) == LUA_TLIGHTUSERDATA && lua_touserdata(L, -1) == &kLoading) {
                lua_pop(L, 1);
                lua_pushnil(L);
                lua_rawset(L, -3);
            }
            return 0;
        }

        static void Install(lua_State *L) {
            lua_newtable(L);
            lua_rawsetp(L, LUA_REGISTRYINDEX, &kModules);
            lua_createtable(L, 0, 1);
            lua_pushcfunction(L, EndLoading);
            lua_setfield(L, -2, "__close");
            lua_rawsetp(L, LUA_REGISTRYINDEX, &kLoadingMeta);
            lua_pushcfunction(L, Require);
            lua_setglobal(L, "require");
        }
    };

    bool LuaRuntime::IsExecuting() const {
        return _entryDepth > 0 || _scheduler->IsAtWork();
    }

    void LuaRuntime::Tick(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point after) {
        _scheduler->Tick(now, after);
    }

    int LuaRuntime::Open(lua_State *L) {
        OpenSandbox(L);
        Modules::Install(L);
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
        int status = LUA_OK;
        {
            // Script is on the stack: the JavaScript engine frees no runtime under it (ScriptActivity).
            const ScriptActivity::LuaRunning running;
            status = lua_pcall(_state, nargs, nresults, function);
        }
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
            // The nearest Lua code: a function of the sandbox may have been called by another C function.
            runtime->_deadlineWhere = Where(L, level);
        }
        lua_sethook(L, DeadlineHook, LUA_MASKCOUNT, 1);
        lua_pushfstring(L, "%s%s", runtime->_deadlineWhere.c_str(), kDeadlineMessage);
        return lua_error(L);
    }

    std::string LuaRuntime::Where(lua_State *L, int level) {
        lua_Debug activation;
        for (; lua_getstack(L, level, &activation) != 0; ++level) {
            lua_getinfo(L, "Sl", &activation);
            if (activation.currentline > 0) {
                return std::string(activation.short_src) + ":" + std::to_string(activation.currentline) + ": ";
            }
        }
        return {};
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

    void LuaRuntime::WriteOutput(std::string_view text, OutputLevel level) {
        std::string line;
        if (!AppendBounded(line, text.data(), text.size(), kMaxLineBytes)) {
            line += kTruncated;
        }
        WriteLine(std::move(line), level);
    }

    void LuaRuntime::WriteLine(std::string line, OutputLevel level) {
        BlankControlCharacters(line);
        if (_printHandler) {
            _printHandler(line);
        }
        else {
            LogLine(line, level);
        }
    }

    // A script chooses the text of its errors as it chooses what it prints: it must get no line of its own and no
    // control sequence into the log with it. The traceback keeps its lines.
    void LuaRuntime::LogError(const std::string &message) {
        std::string entry;
        for (size_t start = 0; start <= message.size();) {
            const size_t end = std::min(message.find('\n', start), message.size());
            std::string line = message.substr(start, end - start);
            BlankControlCharacters(line);
            if (start != 0) {
                entry += "\n[" + _options.resourceName + "] ";
            }
            entry += line;
            start = end + 1;
        }
        LogLine(entry, OutputLevel::Error);
    }

    // A script that prints or fails in a loop would otherwise fill the log as fast as the disk takes it.
    void LuaRuntime::LogLine(const std::string &line, OutputLevel level) {
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
            logger->log(LogLevelOf(level), "[{}] {}", _options.resourceName, line);
        }
        else {
            logger->warn("[{}] output suppressed: more than {} lines in a second", _options.resourceName, kMaxLinesPerSecond);
        }
    }
} // namespace Framework::Scripting
