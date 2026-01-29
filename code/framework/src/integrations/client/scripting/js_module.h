#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include <scripting/js/v8_engine.h>
#include <scripting/js/resource/js_resource_manager.h>
#include <world/client.h>

namespace Framework::Integrations::Client::Scripting {

    /**
     * Information about a JS resource received from the server.
     */
    struct JSServerResourceInfo {
        std::string name;
        std::string version;
    };

    /**
     * Callback type for resource download requests.
     */
    using JSResourceDownloadCallback = std::function<void(const std::string &resourceName, const std::string &version)>;

    /**
     * Callback type for resource sync completion.
     */
    using JSResourceSyncCompleteCallback = std::function<void(bool success)>;

    /**
     * Client-side JavaScript scripting module with resource management support.
     *
     * Uses V8 for sandboxed client-side JavaScript execution.
     * More restricted than server-side (no filesystem, no network).
     */
    class JSClientScriptingModule {
      public:
        explicit JSClientScriptingModule(std::shared_ptr<World::ClientEngine> world);
        ~JSClientScriptingModule();

        /**
         * Initialize the V8 engine and resource manager.
         * @param sdkCallback Optional callback for registering additional SDK bindings
         * @return true if initialization succeeded
         */
        bool Init(Framework::Scripting::JS::Engine::SDKRegisterCallback sdkCallback = nullptr);

        /**
         * Shutdown the V8 engine.
         */
        bool Shutdown();

        /**
         * Update tick - process pending operations.
         * Call this from the game loop.
         */
        void Update();

        /**
         * Get the V8 engine.
         */
        Framework::Scripting::JS::V8Engine *GetEngine() const {
            return _v8Engine.get();
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
        Framework::Scripting::JS::JSResourceManager *GetResourceManager() const {
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
        void OnServerResourceList(const std::vector<JSServerResourceInfo> &resources);

        /**
         * Check if all server resources have been synchronized.
         */
        bool AreResourcesSynced() const {
            return _resourcesSynced;
        }

        /**
         * Get the list of resources received from the server.
         */
        const std::vector<JSServerResourceInfo> &GetServerResourceList() const {
            return _serverResourceList;
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
        void SetOnResourceDownloadNeeded(JSResourceDownloadCallback callback) {
            _onResourceDownloadNeeded = std::move(callback);
        }

        /**
         * Set callback for when resource sync is complete.
         */
        void SetOnResourceSyncComplete(JSResourceSyncCompleteCallback callback) {
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
        std::unique_ptr<Framework::Scripting::JS::V8Engine> _v8Engine;
        std::shared_ptr<World::ClientEngine> _world;
        std::unique_ptr<Framework::Scripting::JS::JSResourceManager> _resourceManager;

        // Resource synchronization state
        std::vector<JSServerResourceInfo> _serverResourceList;
        bool _resourcesSynced = false;

        // Callbacks
        JSResourceDownloadCallback _onResourceDownloadNeeded;
        JSResourceSyncCompleteCallback _onResourceSyncComplete;

        // Resource cache path
        std::string _resourceCachePath = "resources";
    };

} // namespace Framework::Integrations::Client::Scripting
