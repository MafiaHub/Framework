/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/error.h>
#include <utils/lifecycle.h>
#include <utils/result.h>
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

        [[nodiscard]] Utils::Result<void, Error> Init(InstanceOptions &);
        void Shutdown() override;

        void Render();
        void Update() override;

        // Override what you need; this is the whole extension surface (no Set*Callback setters).
        // Order: Init -> PostInit; tick: Update -> PostUpdate, Render -> PostRender; Shutdown -> PreShutdown.
        // On connect: assets -> scripting init (ModuleRegister) -> PostScriptInit -> OnConnectionFinalized.
        virtual void PostInit() {}
        virtual void PostUpdate() {}
        virtual void PostRender() {}
        virtual void PreShutdown() {}
        // Client scripting comes up only after a connection; unavailable before this.
        virtual void PostScriptInit() {}
        virtual void ModuleRegister(Framework::Scripting::Engine *engine) {
            (void)engine;
        }

        virtual void OnConnectionFinalized(float serverTickRate) {
            (void)serverTickRate;
        }
        virtual void OnConnectionClosed() {}
        virtual void OnAssetsDownloadFinished(bool success) {
            (void)success;
        }
        virtual void OnChatMessageReceived(const std::string &text) {
            (void)text;
        }

        [[nodiscard]] Utils::Result<void, Error> RenderInit();

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

        // Invoked by the chat RPC handler with a line received from the server.
        void DispatchReceivedChat(const std::string &text) {
            OnChatMessageReceived(text);
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
