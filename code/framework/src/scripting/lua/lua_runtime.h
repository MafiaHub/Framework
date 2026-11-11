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
    // Memory. An allocation past the cap fails and Lua raises "not enough memory", after collecting garbage first
    // for its own objects. The buffer a string function builds its result in (string.rep, table.concat,
    // string.format) is refused without that collection, so a script close to the cap can get the error while
    // holding little that is live.
    //
    // Time. The deadline is a count hook, and a state that has one runs Lua code at about half the speed of a state
    // without. Past the deadline the entry cannot go on: the thread that ran out of time is hooked on every
    // instruction, so not even its __close handlers run; pcall, xpcall and the coroutine functions raise instead
    // of returning; an xpcall message handler is not called; no coroutine or nested entry starts. What the hook
    // cannot interrupt is a C function: one library call that loops for long (a pathological string pattern,
    // table.move over a huge range) holds the state until it returns.
    //
    // A hook does not run while Lua has hooks switched off, which is why a script cannot set a __gc finalizer and
    // why coroutine.create runs a coroutine's function under a protected call. A thread that host code starts with
    // lua_resume has no such call: if the hook kills it, Lua leaves hooks disabled on it for good, so it must
    // never be resumed or reused, and closing it runs its pending __close handlers without a deadline.
    //
    // Host code. A Lua error raised outside a protected call throws std::runtime_error; the state must then be
    // considered unusable and the runtime destroyed. Outside an entry, read and write values a script owns with raw
    // access only (registry references, lua_rawget, lua_rawset): lua_getglobal, lua_getfield and their like run a
    // script's metamethod unprotected and without a deadline.
    class LuaRuntime final {
      public:
        using PrintHandler = fu2::function<void(const std::string &line) const>;

        // The most a script gets into one line of output and into one error text. What is longer is cut and ends
        // with " ...(truncated)".
        static constexpr size_t kMaxLineBytes  = 4096;
        static constexpr size_t kMaxErrorBytes = 16384;
        // Lines the default print handler writes to the log in one second; the rest of that second is dropped.
        static constexpr int kMaxLinesPerSecond = 200;

        // Throws std::runtime_error when the state cannot be created, as under a memory cap it does not fit.
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
        // Leaves `nresults` results (LUA_MULTRET for all of them, 250 at most otherwise: Lua 5.5 takes no more),
        // or returns false with the error text popped into `error`. An entry nested in one whose deadline has
        // passed does not run.
        bool Call(int nargs, int nresults, std::string &error);

        // Bytes the state holds, as counted by its allocator.
        size_t MemoryInUse() const {
            return _memoryInUse;
        }

        // Receives every line print() writes, and every warning as "warning: <text>": at most kMaxLineBytes of
        // it, with a space for each control character but the tab. It must not throw. The default, which an empty
        // handler restores, writes "[resource] line" to the scripting logger, kMaxLinesPerSecond at most.
        void SetPrintHandler(PrintHandler handler);

      private:
        // What a script calls in place of Lua's own functions. Defined in lua_sandbox.cpp.
        struct Sandbox;

        // Instructions a thread runs between two looks at the deadline.
        static constexpr int kHookInterval = 10000;

        static constexpr const char *kTruncated = " ...(truncated)";

        static void *Allocate(void *runtime, void *block, size_t oldSize, size_t newSize);
        static int Panic(lua_State *L);
        static int Traceback(lua_State *L);
        static void DeadlineHook(lua_State *L, lua_Debug *activation);

        // Opens the libraries and replaces what a script must not have as Lua ships it. Raises on failure, so it
        // runs in a protected call. Defined in lua_sandbox.cpp.
        static int OpenSandbox(lua_State *L);

        // Every entry into the state goes through here. Takes the function and its `nargs` arguments from the top
        // of the main stack, as Call does.
        bool Execute(int nargs, int nresults, std::string &error);

        bool HasDeadline() const {
            return _options.entryDeadline.count() > 0;
        }

        // Whether the entry in progress has run out of time, by the clock.
        bool PastDeadline() const;

        // Aborts the entry in progress from thread `L`: raises "<position>: script ran too long", the position
        // being where the script was when its time was first found to be up, `level` activations above the caller.
        static int RaiseDeadline(lua_State *L, int level);

        // Appends to `text` what keeps it within `limit` bytes. False when that was not all of it.
        static bool AppendBounded(std::string &text, const char *data, size_t length, size_t limit);

        // Takes the error off the top of the stack, as text of kMaxErrorBytes at most.
        static std::string PopError(lua_State *L);

        // Hands a line of output to the print handler.
        void WriteLine(std::string line);
        void LogLine(const std::string &line);

        LuaRuntimeOptions _options;
        PrintHandler _printHandler; // empty: the log

        // The second the default handler is counting lines in, and how many it has seen.
        std::chrono::steady_clock::time_point _logSecond;
        int _logLines = 0;

        // The warning Lua is handing over piece by piece.
        std::string _warning;
        bool _warningContinues = false;
        bool _warningWhole     = true;

        size_t _memoryInUse = 0;
        lua_State *_state   = nullptr;

        // Entries in progress: the outermost one arms the deadline, the ones nested in it run under the same.
        int _entryDepth = 0;
        std::chrono::steady_clock::time_point _deadline;
        // Set once the entry in progress is being aborted, with the position its error names.
        bool _expired = false;
        std::string _deadlineWhere;
    };
} // namespace Framework::Scripting
