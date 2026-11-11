/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../bind/doc.h"
#include "../errors.h"
#include "../script_engine.h"

#include <function2/function2.hpp>

#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Scripting {
    class LuaRuntime;

    struct LuaEngineOptions {
        Bind::Side side         = Bind::Side::Server;
        size_t memoryLimitBytes = 512u << 20;        // of each state; 0: no cap
        std::chrono::milliseconds entryDeadline {0}; // of each entry into a state; 0: off
    };

    // The Lua engine: one LuaRuntime (lua_runtime.h) for each resource that has Lua scripts, created, ticked and
    // destroyed here. It holds no V8.
    //
    // A runtime that is executing (LuaRuntime::IsExecuting: script is running in it, or its scheduler is in the
    // middle of telling a `done`) is not destroyed under its own feet. DestroyResourceRuntime takes it away at
    // once, so that GetRuntime no longer gives it, no file runs in it, none of its timers fires and nothing that
    // waits in it is resumed, and the next Tick that finds it idle destroys it. The name is free in between, for
    // a restart to create the next one. Any other runtime is destroyed on the spot, also when the call comes
    // from the script of another resource.
    class LuaEngine final: public ScriptEngine {
      public:
        // Installs the bindings in a runtime. A Lua error it raises, or a std::exception it throws, fails the
        // creation of that runtime.
        using RuntimeSetup = fu2::function<void(LuaRuntime &) const>;
        // Hears of a runtime that is about to be destroyed, while its state is still open.
        using RuntimeDisposing = fu2::function<void(LuaRuntime &) const>;

        // An error nobody waited for: raised by a timer callback, or by a coroutine after its first wait.
        struct PendingError {
            std::string resourceName;
            std::string errorMessage;
        };

        explicit LuaEngine(LuaEngineOptions options = {});
        ~LuaEngine() override;

        // Creates nothing: a state is made when a resource needs one.
        [[nodiscard]] ScriptingError Init();

        // Destroys every runtime. It may be called from inside a script. While any runtime is executing none of
        // them is closed: they are all taken away as DestroyResourceRuntime takes one that is executing, the
        // engine is no longer initialised, and they are destroyed once none of them is executing any more. That is
        // on the way out of the Tick, the file or the creation the script ran under, or else, when the script was
        // started past the engine, at the next of those calls, the next Tick at the latest, or with the engine.
        void Shutdown() override;

        Language GetLanguage() const override {
            return Language::Lua;
        }

        // A path that ends in ".lua", in lower case.
        bool HandlesFile(std::string_view path) const override {
            return path.ends_with(".lua");
        }

        // Fires the timers that are due in every runtime and wakes the coroutines whose sleep is over. A runtime
        // that is executing further up the stack is left alone. The second form is given the time, for a test.
        void Tick() override;
        void Tick(std::chrono::steady_clock::time_point now);

        // When Tick next has something to run: the earliest time a timer fires or a sleep ends in any runtime, on
        // the clock Tick() reads. Nothing when none is pending. A runtime that is executing is left out, as Tick
        // leaves it alone.
        std::optional<std::chrono::steady_clock::time_point> NextDue() const;

        // Fails when the resource has a runtime already, when the state cannot be created, when the setup
        // callback fails, and when that callback destroys the runtime it is setting up ("resource runtime was
        // destroyed during setup").
        bool CreateResourceRuntime(const std::string &resourceName) override;
        void DestroyResourceRuntime(const std::string &resourceName) override;

        // Runs the file as the chunk "@<resource>/<path below the resource root>", to its end or to where it
        // first waits. The file is read through the VFS when its path is virtual, from the disk otherwise.
        bool ExecuteResourceFile(const std::string &resourceName, std::string_view filepath) override;

        // Run inside every runtime created from now on, in a protected call, before any script.
        void SetRuntimeSetupCallback(RuntimeSetup setup);

        // The directory of a resource: where require finds its modules, and what the chunk names leave out. It is
        // read when a file is run and when a module is asked for, so it may be set, or changed, once the runtime
        // exists. LuaRuntimeOptions::rootPath is what it was when the runtime was created.
        void SetResourceRoot(const std::string &resourceName, const std::string &rootPath);

        // The runtime of a resource, or null: it has none, or the one it had is being destroyed.
        LuaRuntime *GetRuntime(const std::string &resourceName) const;

        // The errors queued since the last call. They are queued, not handled where they are raised, because what
        // handles them may stop the resource.
        std::vector<PendingError> DrainPendingErrors();

        // Called for every runtime before it is destroyed, the one whose setup failed included: the place to drop
        // what a binding keeps for that state. Keep such things by the LuaRuntime, or its lua_State, never by the
        // name of the resource: a runtime that was put off is told of here after the resource has got its next
        // one. Listeners live as long as the engine. They must not run script.
        void AddRuntimeDisposingListener(RuntimeDisposing listener);

      private:
        // Runs the setup callback in a runtime that was just created. False when it failed, or took the runtime
        // away.
        bool SetUp(const std::string &resourceName, LuaRuntime &runtime);

        // The root of a resource as it is now, or empty.
        std::string ResourceRoot(const std::string &resourceName) const;

        // Tells the listeners and closes the state.
        void Teardown(std::unique_ptr<LuaRuntime> runtime);

        // Destroys the runtimes whose destruction was put off, but for those still executing. A shutdown that was
        // put off holds them all back until none is.
        void FlushRetiredRuntimes();

        // Ends a shutdown that a script asked for, if there is one and nothing is executing any more.
        void FinishShutdown();

        LuaEngineOptions _options;
        RuntimeSetup _runtimeSetup;
        std::vector<RuntimeDisposing> _runtimeDisposingListeners;

        std::map<std::string, std::string, std::less<>> _resourceRoots;
        std::map<std::string, std::unique_ptr<LuaRuntime>, std::less<>> _runtimes;

        // Runtimes destroyed while they were executing.
        std::vector<std::unique_ptr<LuaRuntime>> _retiredRuntimes;
        // Shutdown found one of them executing, and waits for all of them.
        bool _shuttingDown = false;

        std::vector<PendingError> _pendingErrors;
    };
} // namespace Framework::Scripting
