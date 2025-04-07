/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <external/discord/wrapper.h>
#include <external/imgui/wrapper.h>
#include <function2.hpp>
#include <graphics/renderer.h>
#include <graphics/renderio.h>

#include "networking/engine.h"
#include <FileListTransferCBInterface.h>

#include <memory>
#include <utility>
#include <world/client.h>

#include "scripting/module.h"

#include "world/types/player.hpp"
#include "world/types/streaming.hpp"

#include <gui/manager.h>

#include <input/input.h>

#include <flecs/flecs.h>

namespace Framework::Integrations::Client {
    using NetworkConnectionFinalizedCallback = fu2::function<void(flecs::entity, float) const>;
    using NetworkConnectionClosedCallback    = fu2::function<void() const>;
    using AssetsDownloadFinishedCallback     = fu2::function<void(bool success) const>;

    class Instance;

    class AssetDownloadFileProgress final: public SLNet::FileListTransferCBInterface {
      private:
        Instance *_instance = nullptr;

      public:
        AssetDownloadFileProgress(Instance *instance): _instance(instance) {}

        bool OnFile(SLNet::FileListTransferCBInterface::OnFileStruct *onFileStruct) override;

        void OnFileProgress(SLNet::FileListTransferCBInterface::FileProgressStruct *fps) override;

        bool OnDownloadComplete(DownloadCompleteStruct *dcs) override;
    };

    struct InstanceOptions {
        int64_t discordAppId                = 0;
        bool usePresence                    = true;
        bool useRenderer                    = true;
        [[maybe_unused]] bool useNetworking = true;
        bool useImGUI                       = false;

        // networked game metadata (required)
        std::string gameName;
        std::string gameVersion;
        std::string modVersion;

        bool initRendererManually = false;

        Graphics::RendererConfiguration rendererOptions = {};
    };

    struct CurrentState {
        std::string _host;
        int32_t _port;
        std::string _nickname;
        uint32_t _serverIDHash;
    };

    struct AssetDownloadStatus {
        float progress {0.0f};
        bool downloading;
        uint16_t setID;
    };

    class Instance {
      private:
        bool _initialized       = false;
        bool _renderInitialized = false;
        InstanceOptions _opts;

        std::unique_ptr<Networking::Engine> _networkingEngine;
        std::unique_ptr<External::Discord::Wrapper> _presence;
        std::unique_ptr<Graphics::Renderer> _renderer;
        std::shared_ptr<World::ClientEngine> _worldEngine;
        std::unique_ptr<Graphics::RenderIO> _renderIO;
        std::unique_ptr<Client::Scripting::ClientScriptingModule> _scriptingModule;
        std::shared_ptr<Framework::GUI::Manager> _webManager;

        // gui
        std::unique_ptr<External::ImGUI::Wrapper> _imguiApp;

        // Local state tracking
        CurrentState _currentState;
        NetworkConnectionFinalizedCallback _onConnectionFinalized;
        NetworkConnectionClosedCallback _onConnectionClosed;
        AssetsDownloadFinishedCallback _onAssetsDownloadFinished;

        // Entity factories
        std::unique_ptr<World::Archetypes::PlayerFactory> _playerFactory;
        std::unique_ptr<World::Archetypes::StreamingFactory> _streamingFactory;

        // assets
        AssetDownloadStatus _downloadStatus {};
        std::string _assetCachePath;
        bool _initialDownloadDone {};

        void InitNetworkingMessages();
        void InitAssetDownloader();
        void OnAssetsDownloaded(bool success);
        void InitCacheAssetFolders();
        void RegisterScriptingBuiltins(Framework::Scripting::Engine *);

      public:
        Instance();
        virtual ~Instance();

        ClientError Init(InstanceOptions &);
        ClientError Shutdown();

        void Render();
        void Update();

        virtual bool PostInit()    = 0;
        virtual bool PreShutdown() = 0;
        virtual void PostUpdate()  = 0;
        virtual void PostRender()  = 0;

        virtual void ModuleRegister(Framework::Scripting::Engine *engine) {
            (void)engine;
        }

        ClientError RenderInit();

        void DownloadsAssetsFromConnectedServer();

        InstanceOptions *GetOptions() {
            return &_opts;
        }

        bool IsInitialized() const {
            return _initialized;
        }

        CurrentState GetCurrentState() const {
            return _currentState;
        }

        void SetCurrentState(CurrentState state) {
            _currentState = std::move(state);
            _currentState._serverIDHash = Framework::Utils::Hashing::CalculateCRC32(_currentState._host + ":" + std::to_string(_currentState._port));
        }

        void SetOnConnectionFinalizedCallback(NetworkConnectionFinalizedCallback cb) {
            _onConnectionFinalized = std::move(cb);
        }

        void SetOnConnectionClosedCallback(NetworkConnectionClosedCallback cb) {
            _onConnectionClosed = std::move(cb);
        }

        void SetOnAssetsDownloadFinishedCallback(AssetsDownloadFinishedCallback cb) {
            _onAssetsDownloadFinished = std::move(cb);
        }

        Networking::Engine *GetNetworkingEngine() const {
            return _networkingEngine.get();
        }

        World::ClientEngine *GetWorldEngine() const {
            return _worldEngine.get();
        }

        External::Discord::Wrapper *GetPresence() const {
            return _presence.get();
        }

        External::ImGUI::Wrapper *GetImGUI() const {
            return _imguiApp.get();
        }

        Framework::GUI::Manager *GetWebManager() const {
            return _webManager.get();
        }

        Graphics::Renderer *GetRenderer() const {
            return _renderer.get();
        }

        Graphics::RenderIO *GetRenderIO() const {
            return _renderIO.get();
        }

        World::Archetypes::PlayerFactory *GetPlayerFactory() const {
            return _playerFactory.get();
        }

        World::Archetypes::StreamingFactory *GetStreamingFactory() const {
            return _streamingFactory.get();
        }

        virtual Input::IInput *GetBaseInput() const {
            return nullptr;
        }

        AssetDownloadStatus &GetAssetDownloadStatus() {
            return _downloadStatus;
        }

        void SetAssetCachePath(const std::string &path) {
            _assetCachePath = path;
        }

        const std::string &GetAssetCachePath() const {
            return _assetCachePath;
        }

        Scripting::ClientScriptingModule *GetScriptingModule() const {
            return _scriptingModule.get();
        }

        friend class AssetDownloadFileProgress;
        friend class AssetFileTransfer;
    };
} // namespace Framework::Integrations::Client
