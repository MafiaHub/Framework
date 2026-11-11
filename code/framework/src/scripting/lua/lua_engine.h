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
    // A runtime that is executing is not destroyed under its own feet: DestroyResourceRuntime takes it away at
    // once, so that GetRuntime no longer gives it, no file runs in it, none of its timers fires and nothing that
    // waits in it is resumed, and the next Tick that finds it idle destroys it. The name is free in between, for
    // a restart to create the next one. Any other runtime is destroyed on the spot, also when the call comes
    // from the script of another resource. Shutdown does not wait.
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

        // Destroys every runtime, the ones that are executing included.
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

        // Fails when the resource has a runtime already, when the state cannot be created, and when the setup
        // callback fails.
        bool CreateResourceRuntime(const std::string &resourceName) override;
        void DestroyResourceRuntime(const std::string &resourceName) override;

        // Runs the file as the chunk "@<resource>/<path below the resource root>", to its end or to where it
        // first waits. The file is read through the VFS when its path is virtual, from the disk otherwise.
        bool ExecuteResourceFile(const std::string &resourceName, std::string_view filepath) override;

        // Run inside every runtime created from now on, in a protected call, before any script.
        void SetRuntimeSetupCallback(RuntimeSetup setup);

        // The directory of a resource: where require finds its modules, and what the chunk names leave out. Set it
        // before the runtime is created, which keeps the root it was created with.
        void SetResourceRoot(const std::string &resourceName, const std::string &rootPath);

        // The runtime of a resource, or null: it has none, or the one it had is being destroyed.
        LuaRuntime *GetRuntime(const std::string &resourceName) const;

        // The errors queued since the last call. They are queued, not handled where they are raised, because what
        // handles them may stop the resource.
        std::vector<PendingError> DrainPendingErrors();

        // Called for every runtime before it is destroyed, the one whose setup failed included: the place to drop
        // what a binding keeps for that state. Listeners live as long as the engine. They must not run script.
        void AddRuntimeDisposingListener(RuntimeDisposing listener);

      private:
        struct ResourceRuntime final {
            std::string resourceName;
            std::string rootPath;
            std::unique_ptr<LuaRuntime> runtime;
            // Calls of the engine into the runtime that have not returned.
            int entered = 0;
        };

        // Counts a call of the engine into a runtime, for as long as it lives.
        class Entered;

        // Whether the runtime cannot be destroyed now.
        static bool IsBusy(const ResourceRuntime &runtime);

        // Tells the listeners and closes the state.
        void Teardown(std::unique_ptr<ResourceRuntime> runtime);

        // Destroys the runtimes whose destruction was put off, but for those still executing.
        void FlushRetiredRuntimes();

        LuaEngineOptions _options;
        RuntimeSetup _runtimeSetup;
        std::vector<RuntimeDisposing> _runtimeDisposingListeners;

        std::map<std::string, std::string, std::less<>> _resourceRoots;
        std::map<std::string, std::unique_ptr<ResourceRuntime>, std::less<>> _runtimes;

        // Runtimes destroyed while they were executing.
        std::vector<std::unique_ptr<ResourceRuntime>> _retiredRuntimes;

        std::vector<PendingError> _pendingErrors;
    };
} // namespace Framework::Scripting
