/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <function2/function2.hpp>

#include <scripting/bind/descriptors.h>
#include <scripting/lua/lua_engine.h>
#include <scripting/module.h>
#include <scripting/node_engine.h>
#include <scripting/resource/resource_manager.h>
#include <utils/lifecycle.h>

namespace Framework::Integrations::Server::Scripting {

    /**
     * Information about a resource for sending to clients.
     */
    struct ClientResourceInfo {
        std::string name;
        std::string version;
    };

    /**
     * Server-side scripting module with resource management support.
     *
     * Uses Node.js for full server-side JavaScript capabilities including
     * require(), async/await, npm packages, etc. A resource's `.lua` scripts
     * run in a sandboxed Lua state of its own, next to its Node.js environment.
     */
    class ServerScriptingModule final
        : public Framework::Lifecycle
        , public Framework::Scripting::ScriptingModule {
      public:
        ServerScriptingModule();
        ~ServerScriptingModule();

        /**
         * Initialize the Node.js engine, the Lua engine and resource manager.
         * @param sdkCallback Optional callback for registering additional SDK bindings
         * @return true if initialization succeeded
         */
        [[nodiscard]] Framework::Scripting::ScriptingError Init(Framework::Scripting::Engine::SDKRegisterCallback sdkCallback = nullptr);

        /**
         * Pre-shutdown: stop all resources before engine shutdown.
         */
        bool PreShutdown();

        /**
         * Shutdown the resource manager, then the Lua engine, then the Node.js engine.
         */
        void Shutdown() override;

        /**
         * Update tick - process Node.js event loop, Lua timers and scheduled restarts.
         * Call this from the game loop.
         */
        void Update() override;

        /**
         * Wait for the next tick, running scripts' I/O, timers and promises as they become ready meanwhile. Lua timers
         * fire when they come due, a few milliseconds apart at least. A Lua sleep or timer of no delay is left to the
         * next Update, so a loop around sleep(0) goes round once for each tick.
         */
        void WaitUntil(std::chrono::steady_clock::time_point deadline);

        /**
         * How many rounds WaitUntil has taken since the module was created: one for a wait that nothing cut short,
         * one more each time a Lua timer came due or an uncaught error came up before the deadline. For diagnostics
         * and tests.
         */
        uint64_t GetWaitRounds() const {
            return _waitRounds;
        }

        /**
         * Get the Node.js engine.
         */
        Framework::Scripting::NodeEngine *GetEngine() const {
            return _nodeEngine.get();
        }

        Framework::Scripting::Engine *GetScriptingEngine() const override {
            return _nodeEngine.get();
        }

        /**
         * Get the Lua engine.
         */
        Framework::Scripting::LuaEngine *GetLuaEngine() const {
            return _luaEngine.get();
        }

        /**
         * Get the resource manager.
         */
        Framework::Scripting::ResourceManager *GetResourceManager() const override {
            return _resourceManager.get();
        }

        /**
         * Set the path to the resources directory.
         */
        void SetResourcesPath(const std::string &path);

        /**
         * Get the resources path.
         */
        std::string GetResourcesPath() const {
            return _resourcesPath;
        }

        /**
         * Enable/disable development mode (file-watch hot-reload). Must be set
         * before Init(); also updates an already-created resource manager.
         */
        void SetDevMode(bool enabled);

        /**
         * Set the mod version exposed as ExecutionEnvironment.modVersion. Must be set before Init().
         */
        void SetModVersion(const std::string &version) {
            _modVersion = version;
        }

        /**
         * Get list of resources to send to clients.
         * Only includes resources with client entry points defined.
         */
        std::vector<ClientResourceInfo> GetClientResourceList() const;

        /**
         * Discover and start all resources.
         */
        bool StartAllResources();

        /**
         * Register the Framework SDK bindings in the engine.
         */
        void RegisterFrameworkBindings();

        /**
         * Register the Framework SDK bindings in a resource's Lua runtime, then run the setups AddLuaRuntimeSetup
         * was given. The Lua engine runs this inside every runtime it creates, in a protected call.
         */
        void RegisterLuaBindings(Framework::Scripting::LuaRuntime &runtime);

        /**
         * Have `setup` run in every Lua state the server creates from now on, after the Framework's own builtins
         * are registered in it and in the same protected call: where a project registers its Lua builtins. Call
         * it before or after Init(), and before the resources that need it start. Call once per process;
         * setups persist across `Reset()`. A setup may add another, which runs in the state being set up too.
         */
        void AddLuaRuntimeSetup(fu2::function<void(Framework::Scripting::LuaRuntime &) const> setup);

        /**
         * The definitions of what RegisterLuaBindings publishes: what the Lua definition file of a server is
         * written from (Bind::WriteLuaDefinitions).
         */
        static std::vector<const Framework::Scripting::Bind::ClassDesc *> LuaBuiltinDefinitions();

      private:
        // Hand uncaught errors the engines queued to the resource manager, outside any script.
        void HandleRuntimeErrors();

        // Fire the Lua timers that are due and wake the coroutines whose sleep is over, then handle what they raised.
        // With `after`, only what came due later than it.
        void TickLua(std::chrono::steady_clock::time_point after = std::chrono::steady_clock::time_point::min());

        std::unique_ptr<Framework::Scripting::NodeEngine> _nodeEngine;
        std::unique_ptr<Framework::Scripting::LuaEngine> _luaEngine;
        std::unique_ptr<Framework::Scripting::ResourceManager> _resourceManager;
        std::vector<fu2::function<void(Framework::Scripting::LuaRuntime &) const>> _luaRuntimeSetups;
        std::chrono::steady_clock::time_point _lastLuaTick {};
        uint64_t _waitRounds = 0;
        std::string _modVersion;

        std::string _resourcesPath = "resources";
        bool _devMode              = false;
    };

} // namespace Framework::Integrations::Server::Scripting
