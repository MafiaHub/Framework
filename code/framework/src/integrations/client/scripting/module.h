#pragma once

#include <scripting/client_engine.h>
#include <scripting/resource/resource_manager.h>
#include <world/client.h>

#include <functional>
#include <string>
#include <vector>

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
     * Called when client needs to download a resource from the server.
     */
    using ResourceDownloadCallback = std::function<void(const std::string &resourceName, const std::string &version, uint32_t hash)>;

    /**
     * Callback type for resource sync completion.
     * Called when all resources have been synchronized.
     */
    using ResourceSyncCompleteCallback = std::function<void(bool success)>;

    /**
     * Client-side scripting module with resource management support.
     *
     * - Mirrors server resource lifecycle
     * - Handles resource synchronization from server at connection time
     * - Client runs standalone after initial resource sync
     */
    class ClientScriptingModule {
      private:
        std::shared_ptr<Framework::Scripting::ClientEngine> _clientEngine;
        std::shared_ptr<World::ClientEngine> _world;
        std::unique_ptr<Framework::Scripting::ResourceManager> _resourceManager;

        // Resource synchronization state
        std::vector<ServerResourceInfo> _serverResourceList;
        bool _resourcesSynced = false;

        // Callbacks
        ResourceDownloadCallback _onResourceDownloadNeeded;
        ResourceSyncCompleteCallback _onResourceSyncComplete;

        // Resource cache path
        std::string _resourceCachePath;

      public:
        ClientScriptingModule(std::shared_ptr<World::ClientEngine>);

        ~ClientScriptingModule();

        bool Init(Framework::Scripting::SDKRegisterCallback);

        bool Shutdown();

        /**
         * Update the scripting module (call from main loop).
         * Processes ResourceManager updates like health checks and scheduled restarts.
         */
        void Update();

        // Engine access

        std::shared_ptr<Framework::Scripting::ClientEngine> GetEngine() const {
            return _clientEngine;
        }

        std::shared_ptr<World::ClientEngine> GetWorldEngine() const {
            return _world;
        }

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
         * @param resources List of resources the server wants the client to load
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
         * Mark that a resource has been downloaded and is ready to load.
         * Call this after the asset download system has downloaded a resource.
         * @param resourceName Name of the resource that was downloaded
         */
        void OnResourceDownloaded(const std::string &resourceName);

        /**
         * Start all downloaded resources.
         * Call this after all resources have been downloaded.
         */
        bool StartAllResources();

        // Callbacks

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

        // State queries

        /**
         * Check if a resource is available locally (downloaded and valid).
         * @param resourceName Name of the resource
         * @param version Expected version
         * @param hash Expected content hash
         * @return True if resource is available and matches
         */
        bool IsResourceAvailable(const std::string &resourceName, const std::string &version, uint32_t hash) const;

        /**
         * Get the local path for a resource.
         * @param resourceName Name of the resource
         * @return Path to the resource directory
         */
        std::string GetResourcePath(const std::string &resourceName) const;

      private:
        /**
         * Check which resources need to be downloaded.
         * @return List of resources that need downloading
         */
        std::vector<ServerResourceInfo> GetResourcesToDownload() const;

        /**
         * Discover a single resource from the cache.
         * @param resourceName Name of the resource
         * @return True if resource was discovered successfully
         */
        bool DiscoverCachedResource(const std::string &resourceName);
    };
} // namespace Framework::Integrations::Client::Scripting
