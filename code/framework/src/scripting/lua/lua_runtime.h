/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../bind/doc.h"

#include <function2/function2.hpp>
#include <lua.h>

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace Framework::Scripting {
    struct LuaRuntimeOptions {
        std::string resourceName;
        Bind::Side side         = Bind::Side::Server;
        size_t memoryLimitBytes = 0;                 // 0: no cap
        std::chrono::milliseconds entryDeadline {0}; // 0: off
        std::string rootPath;                        // resource directory, for chunk names and require
    };

    // One Lua state, set up for the scripts of one resource: the libraries a script may use and nothing that reaches
    // a file, a process or bytecode (lua_sandbox.cpp), a cap on the memory it holds, and a deadline on every entry
    // into it.
    //
    // The deadline is a count hook, so it bounds the Lua instructions a thread runs and nothing else. It does not
    // reach a C function (a pathological string pattern), nor the code Lua runs with hooks off: a __gc finalizer,
    // and an xpcall message handler or a coroutine's __close handler that runs because the hook raised. A script
    // that catches the error in the thread that ran out of time is raised on again at once; one that catches it
    // from a coroutine it resumed goes on until the count of its own thread runs out.
    class LuaRuntime final {
      public:
        using PrintHandler = fu2::function<void(const std::string &line) const>;

        // Throws std::runtime_error when the state does not fit under the memory cap.
        explicit LuaRuntime(LuaRuntimeOptions options);
        ~LuaRuntime();

        LuaRuntime(const LuaRuntime &)            = delete;
        LuaRuntime &operator=(const LuaRuntime &) = delete;

        // The main thread.
        lua_State *State() const {
            return _state;
        }

        // The runtime that owns `L`, its main thread or any coroutine of it. `L` must belong to a LuaRuntime: a
        // state created another way has nothing to find.
        static LuaRuntime *From(lua_State *L);

        const LuaRuntimeOptions &Options() const {
            return _options;
        }

        // Compiles `source` as text and runs it on the main thread. False with the message and the traceback in
        // `error`. `chunkName` is what Lua shows in messages: "@resource/file.lua" for a file, "=name" otherwise.
        bool RunString(std::string_view source, std::string_view chunkName, std::string &error);

        // Protected call of the function and `nargs` arguments on top of the main stack, under the deadline.
        // Leaves `nresults` results, or returns false with the error text popped into `error`.
        bool Call(int nargs, int nresults, std::string &error);

        // Bytes the state holds, as counted by its allocator.
        size_t MemoryInUse() const {
            return _memoryInUse;
        }

        // Receives every line print() writes, and every warning as "warning: <text>". It must not throw. The
        // default, which an empty handler restores, writes "[resource] line" to the scripting logger.
        void SetPrintHandler(PrintHandler handler);

      private:
        // Instructions a thread runs between two looks at the deadline.
        static constexpr int kHookInterval = 10000;

        static void *Allocate(void *runtime, void *block, size_t oldSize, size_t newSize);
        static void DeadlineHook(lua_State *L, lua_Debug *activation);

        // lua_sandbox.cpp. OpenSandbox opens the libraries and replaces the globals a script must not have as Lua
        // ships them; it raises on failure, so it runs in a protected call.
        static int OpenSandbox(lua_State *L);
        static int Print(lua_State *L);
        static void Warn(void *runtime, const char *message, int toContinue);

        // Every entry into the state goes through here. Takes the function and its `nargs` arguments from the top
        // of the main stack, as Call does.
        bool Execute(int nargs, int nresults, std::string &error);

        PrintHandler DefaultPrintHandler() const;

        LuaRuntimeOptions _options;
        PrintHandler _printHandler;

        // The pieces of a warning Lua has not finished handing over.
        std::string _warning;
        bool _warningContinues = false;

        size_t _memoryInUse = 0;
        lua_State *_state   = nullptr;

        // Entries in progress: the outermost one arms the deadline, the ones nested in it run under the same.
        int _entryDepth = 0;
        std::chrono::steady_clock::time_point _deadline;
    };
} // namespace Framework::Scripting
