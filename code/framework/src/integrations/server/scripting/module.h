#pragma once

#include <memory>
#include <string>
#include <vector>

#include <scripting/node_engine.h>
#include <scripting/resource/resource_manager.h>
#include <world/server.h>

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
    class ServerScriptingModule {
      public:
        explicit ServerScriptingModule(std::shared_ptr<World::ServerEngine> world);
        ~ServerScriptingModule();

        /**
         * Initialize the Node.js engine and resource manager.
         * @param sdkCallback Optional callback for registering additional SDK bindings
         * @return true if initialization succeeded
         */
        bool Init(Framework::Scripting::Engine::SDKRegisterCallback sdkCallback = nullptr);

        /**
         * Pre-shutdown: stop all resources before engine shutdown.
         */
        bool PreShutdown();

        /**
         * Shutdown the Node.js engine.
         */
        bool Shutdown();

        /**
         * Update tick - process Node.js event loop and scheduled restarts.
         * Call this from the game loop.
         */
        void Update();

        /**
         * Get the Node.js engine.
         */
        Framework::Scripting::NodeEngine *GetEngine() const {
            return _nodeEngine.get();
        }

        /**
         * Get the world engine.
         */
        std::shared_ptr<World::ServerEngine> GetWorldEngine() const {
            return _world;
        }

        /**
         * Get the JavaScript resource manager.
         */
        Framework::Scripting::ResourceManager *GetResourceManager() const {
            return _resourceManager.get();
        }

        /**
         * Set the path to the resources directory.
         */
        void SetResourcesPath(const std::string &path);

        /**
         * Get the resources path.
         */
        std::string GetResourcesPath() const { return _resourcesPath; }

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
         * Process event loop until ES modules finish loading.
         * Call this after StartAllResources() to ensure async imports complete.
         */
        void WaitForPendingLoads();

        /**
         * Register the Framework SDK bindings in the engine.
         */
        void RegisterFrameworkBindings();

      private:
        std::unique_ptr<Framework::Scripting::NodeEngine> _nodeEngine;
        std::shared_ptr<World::ServerEngine> _world;
        std::unique_ptr<Framework::Scripting::ResourceManager> _resourceManager;

        std::string _resourcesPath = "resources";
    };

} // namespace Framework::Integrations::Server::Scripting
