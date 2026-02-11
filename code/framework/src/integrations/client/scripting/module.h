#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include <scripting/v8_engine.h>
#include <scripting/resource/resource_manager.h>
#include <world/client.h>

namespace Framework::Integrations::Client::Scripting {

    /**
     * Information about a resource received from the server.
     */
    struct ServerResourceInfo {
        std::string name;
        std::string version;
        uint32_t hash = 0;
    };

    /**
     * Callback type for resource download requests.
     */
    using ResourceDownloadCallback = std::function<void(const std::string &resourceName, const std::string &version)>;

    /**
     * Callback type for resource sync completion.
     */
    using ResourceSyncCompleteCallback = std::function<void(bool success)>;

    /**
     * Client-side JavaScript scripting module with resource management support.
     *
     * Uses standalone V8 engine for client-side JavaScript execution.
     * No Node.js APIs are available at the binary level.
     */
    class ClientScriptingModule final {
      public:
        explicit ClientScriptingModule(std::shared_ptr<World::ClientEngine> world);
        ~ClientScriptingModule();

        /**
         * Initialize the V8 engine and resource manager.
         * @param sdkCallback Optional callback for registering additional SDK bindings
         * @return true if initialization succeeded
         */
        bool Init(Framework::Scripting::Engine::SDKRegisterCallback sdkCallback = nullptr);

        /**
         * Shutdown the V8 engine.
         * Use this only when the client is truly closing.
         */
        bool Shutdown();

        /**
         * Reset the scripting module for reconnection.
         * Stops all resources and clears state but keeps the engine running.
         * Use this on disconnect to avoid expensive reinitialization.
         */
        void Reset();

        /**
         * Update tick - process pending operations.
         * Call this from the game loop.
         */
        void Update();

        /**
         * Get the V8 engine.
         */
        Framework::Scripting::V8Engine *GetEngine() const {
            return _engine.get();
        }

        /**
         * Get the world engine.
         */
        std::shared_ptr<World::ClientEngine> GetWorldEngine() const {
            return _world;
        }

        /**
         * Get the JavaScript resource manager.
         */
        Framework::Scripting::ResourceManager *GetResourceManager() const {
            return _resourceManager.get();
        }

        /**
         * Set the resource cache path (where downloaded resources are stored).
         */
        void SetResourceCachePath(const std::string &path);

        /**
         * Get the resource cache path.
         */
        const std::string &GetResourceCachePath() const {
            return _resourceCachePath;
        }

        /**
         * Handle resource list message from server.
         * Called when server sends the list of resources on connection.
         */
        void OnServerResourceList(const std::vector<ServerResourceInfo> &resources);

        /**
         * Check if all server resources have been synchronized.
         */
        bool AreResourcesSynced() const {
            return _resourcesSynced;
        }

        /**
         * Get the list of resources received from the server.
         */
        const std::vector<ServerResourceInfo> &GetServerResourceList() const {
            return _serverResourceList;
        }

        /**
         * Set the server resource list (without triggering download checks).
         * Used when resources have already been downloaded.
         */
        void SetServerResourceList(const std::vector<ServerResourceInfo> &resources) {
            _serverResourceList = resources;
            _resourcesSynced = true;
        }

        /**
         * Mark that a resource has been downloaded and is ready to load.
         */
        void OnResourceDownloaded(const std::string &resourceName);

        /**
         * Start all downloaded resources.
         */
        bool StartAllResources();

        /**
         * Stop all running resources.
         */
        void StopAllResources();

        /**
         * Set callback for when a resource download is needed.
         */
        void SetOnResourceDownloadNeeded(ResourceDownloadCallback callback) {
            _onResourceDownloadNeeded = std::move(callback);
        }

        /**
         * Set callback for when resource sync is complete.
         */
        void SetOnResourceSyncComplete(ResourceSyncCompleteCallback callback) {
            _onResourceSyncComplete = std::move(callback);
        }

        /**
         * Get the local path for a resource.
         */
        std::string GetResourcePath(const std::string &resourceName) const;

        /**
         * Register the Framework SDK bindings in the engine.
         */
        void RegisterFrameworkBindings();

      private:
        std::unique_ptr<Framework::Scripting::V8Engine> _engine;
        std::shared_ptr<World::ClientEngine> _world;
        std::unique_ptr<Framework::Scripting::ResourceManager> _resourceManager;

        // Resource synchronization state
        std::vector<ServerResourceInfo> _serverResourceList;
        bool _resourcesSynced = false;

        // Callbacks
        ResourceDownloadCallback _onResourceDownloadNeeded;
        ResourceSyncCompleteCallback _onResourceSyncComplete;

        // Resource cache path
        std::string _resourceCachePath = "resources";
    };

} // namespace Framework::Integrations::Client::Scripting
