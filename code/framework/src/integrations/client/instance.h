/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <utils/lifecycle.h>
#include <external/discord/wrapper.h>
#include <external/imgui/wrapper.h>
#include <function2.hpp>
#include <graphics/renderer.h>
#include <graphics/renderio.h>

#include "networking/engine.h"
#include <mafianet/FileListTransferCBInterface.h>

#include <memory>
#include <utility>

#include "scripting/module.h"

#include <gui/manager.h>

#include <input/input.h>

namespace Framework::Integrations::Client {
    // Fired once the connection handshake completes, with the server tick rate.
    using NetworkConnectionFinalizedCallback = fu2::function<void(float) const>;
    using NetworkConnectionClosedCallback    = fu2::function<void() const>;
    using AssetsDownloadFinishedCallback     = fu2::function<void(bool success) const>;
    // Fired when the server sends a chat line to display.
    using NetworkChatMessageCallback         = fu2::function<void(const std::string &text) const>;

    class Instance;

    class AssetDownloadFileProgress final: public MafiaNet::FileListTransferCBInterface {
      private:
        Instance *_instance = nullptr;

      public:
        AssetDownloadFileProgress(Instance *instance): _instance(instance) {}

        bool OnFile(MafiaNet::FileListTransferCBInterface::OnFileStruct *onFileStruct) override;

        void OnFileProgress(MafiaNet::FileListTransferCBInterface::FileProgressStruct *fps) override;

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
        std::string host;
        int32_t port;
        std::string nickname;
        uint32_t serverIDHash;
    };

    struct AssetDownloadStatus {
        float progress {0.0f};
        bool downloading;
        uint16_t setID;
    };

    class Instance : public Framework::Lifecycle {
      private:
        bool _renderInitialized = false;
        InstanceOptions _opts;

        std::unique_ptr<Networking::Engine> _networkingEngine;
        std::unique_ptr<External::Discord::Wrapper> _presence;
        std::unique_ptr<Graphics::Renderer> _renderer;
        std::unique_ptr<Graphics::RenderIO> _renderIO;
        std::unique_ptr<Client::Scripting::ClientScriptingModule> _scriptingModule;
        std::unique_ptr<Framework::GUI::Manager> _webManager;

        // gui
        std::unique_ptr<External::ImGUI::Wrapper> _imguiApp;

        // Local state tracking
        CurrentState _currentState;
        NetworkConnectionFinalizedCallback _onConnectionFinalized;
        NetworkConnectionClosedCallback _onConnectionClosed;
        AssetsDownloadFinishedCallback _onAssetsDownloadFinished;
        NetworkChatMessageCallback _onChatMessageReceived;

        // assets
        AssetDownloadStatus _downloadStatus {};
        std::string _assetCachePath;
        bool _initialDownloadDone {};

        // Pending resources from server (stored here to survive scripting module reset)
        std::vector<Client::Scripting::ServerResourceInfo> _pendingServerResources;

        // Handshake state carried from ServerResources until the ReadyEvent spawn barrier completes.
        int _readyEventId {};
        float _serverTickRate {};
        // One-shot latch so a stray ready-event completion can't re-run the mod's finalization.
        bool _connectionFinalized {};

        void InitNetworkingMessages();
        void InitAssetDownloader();
        void OnAssetsDownloaded(bool success);
        void InitCacheAssetFolders();
        void RegisterScriptingBuiltins(Framework::Scripting::Engine *);

      public:
        Instance();
        virtual ~Instance();

        [[nodiscard]] ClientError Init(InstanceOptions &);
        void Shutdown() override;

        void Render();
        void Update() override;

        virtual void PostInit() {}
        virtual void PostUpdate() {}
        virtual void PostRender() {}
        virtual void PreShutdown() {}

        virtual void ModuleRegister(Framework::Scripting::Engine *engine) {
            (void)engine;
        }

        [[nodiscard]] ClientError RenderInit();

        void DownloadsAssetsFromConnectedServer();

        InstanceOptions &GetOptions() {
            return _opts;
        }

        CurrentState GetCurrentState() const {
            return _currentState;
        }

        void SetCurrentState(CurrentState state) {
            _currentState = std::move(state);
            _currentState.serverIDHash = Framework::Utils::Hashing::CalculateCRC32(_currentState.host + ":" + std::to_string(_currentState.port));
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

        void SetOnChatMessageReceivedCallback(NetworkChatMessageCallback cb) {
            _onChatMessageReceived = std::move(cb);
        }

        // Invoked by the chat RPC handler with a line received from the server.
        void DispatchReceivedChat(const std::string &text) const {
            if (_onChatMessageReceived) {
                _onChatMessageReceived(text);
            }
        }

        // Send a chat line to the server (the local player's outgoing text).
        void SendChatMessage(const std::string &text);

        Networking::Engine *GetNetworkingEngine() const {
            return _networkingEngine.get();
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
