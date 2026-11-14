/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

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
     * Server-side JavaScript scripting module with resource management support.
     *
     * Uses Node.js for full server-side JavaScript capabilities including
     * require(), async/await, npm packages, etc.
     */
    class ServerScriptingModule final
        : public Framework::Lifecycle
        , public Framework::Scripting::ScriptingModule {
      public:
        ServerScriptingModule();
        ~ServerScriptingModule();

        /**
         * Initialize the Node.js engine and resource manager.
         * @param sdkCallback Optional callback for registering additional SDK bindings
         * @return true if initialization succeeded
         */
        [[nodiscard]] Framework::Scripting::ScriptingError Init(Framework::Scripting::Engine::SDKRegisterCallback sdkCallback = nullptr);

        /**
         * Pre-shutdown: stop all resources before engine shutdown.
         */
        bool PreShutdown();

        /**
         * Shutdown the Node.js engine.
         */
        void Shutdown() override;

        /**
         * Update tick - process Node.js event loop and scheduled restarts.
         * Call this from the game loop.
         */
        void Update() override;

        /**
         * Wait for the next tick, running scripts' I/O, timers and promises as they become ready meanwhile.
         */
        void WaitUntil(std::chrono::steady_clock::time_point deadline);

        /**
         * For a server whose scripts have their own thread: run the runtimes that have work until the deadline,
         * once, and come back early with an error waiting or a task for that thread. False with no engine to run.
         */
        bool RunScriptsUntil(std::chrono::steady_clock::time_point deadline);

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
         * Get the JavaScript resource manager.
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
         * Size Node's platform pool, shared by every resource's isolate. Must be set before Init().
         */
        void SetPlatformThreads(uint32_t threads) {
            _nodeEngine->SetPlatformThreads(static_cast<int>(threads));
        }

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
         * Discover and start all JavaScript resources.
         */
        bool StartAllResources();

        /**
         * Register the Framework SDK bindings in the engine.
         */
        void RegisterFrameworkBindings();

      private:
        // Hand uncaught errors the engine queued to the resource manager, outside any script.
        void HandleRuntimeErrors();

        std::unique_ptr<Framework::Scripting::NodeEngine> _nodeEngine;
        std::unique_ptr<Framework::Scripting::ResourceManager> _resourceManager;
        std::string _modVersion;

        std::string _resourcesPath = "resources";
        bool _devMode              = false;
    };

} // namespace Framework::Integrations::Server::Scripting
