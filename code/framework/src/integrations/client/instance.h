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
#include <external/sentry/wrapper.h>
#include <function2.hpp>
#include <graphics/renderer.h>
#include <graphics/renderio.h>

#include "networking/engine.h"
#include "networking/rpc/chat_message.h"
#include "integrations/client/ui/chat_box.h"
#include <mafianet/FileListTransferCBInterface.h>

#include <cstdint>
#include <memory>
#include <string>
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
        std::string modSlug;
        std::string modVersion;

        bool verifyBuildToken = true; // false bypasses the build/version mismatch challenge

        bool initRendererManually = false;

        // Crash reporting (Sentry). Empty DSN leaves it disabled; the value is project-specific.
        std::string sentryDSN;
        // Directory holding crashpad_handler(.exe); the Sentry cache is created beneath it.
        // Empty -> current working directory.
        std::string sentryModulePath;
        // Release identifier; empty -> derived from gameName + gameVersion.
        std::string sentryRelease;
        // Deployment environment ("retail" / "dev" / "ci"); empty leaves it unset.
        std::string sentryEnvironment;

        // Optional UI font (TTF). Empty -> ImGui's embedded ASCII-only font.
        // A Unicode-covering font enables non-Latin scripts (e.g. Cyrillic).
        std::string imguiFontPath;
        float imguiFontSize = 16.0f;

        Graphics::RendererConfiguration rendererOptions = {};
    };

    struct CurrentState {
        std::string host;
        int32_t port;
        std::string nickname;
        uint32_t serverIDHash;
    };

    // On-connect asset transfer state, filled from the MafiaNet transfer set.
    struct AssetDownloadStatus {
        bool downloading         = false;
        float progress           = 0.0f; // 0..1 over the whole set
        uint64_t bytesDownloaded = 0;
        uint64_t bytesTotal      = 0;
        uint32_t filesDownloaded = 0;
        uint32_t filesTotal      = 0;
        std::string currentFile;
        uint16_t setID           = 0;
    };

    // Observable stages of the connect handshake, for a mod's loading UI.
    enum class ConnectionPhase {
        Disconnected,
        Connecting,
        Authenticating,
        Downloading,
        Starting,
        InGame,
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
        std::unique_ptr<External::Sentry::Wrapper> _crashReporter;

        // gui
        std::unique_ptr<External::ImGUI::Wrapper> _imguiApp;

        // Local state tracking
        CurrentState _currentState;

        // assets
        AssetDownloadStatus _downloadStatus {};
        // Shared file-transfer callback handed to MafiaNet; MafiaNet does not
        // take ownership of it, so it must outlive any active download.
        AssetDownloadFileProgress _assetDownloadProgress {this};
        ConnectionPhase _connectionPhase = ConnectionPhase::Disconnected;
        std::string _assetCachePath;
        bool _initialDownloadDone {};

        // Pending resources from server (stored here to survive scripting module reset)
        std::vector<Client::Scripting::ServerResourceInfo> _pendingServerResources;

        // Client resources the server hot-reloaded; refreshed after the next
        // asset re-sync completes (dev mode). Empty on a normal connect.
        std::vector<Client::Scripting::ServerResourceInfo> _pendingRefreshResources;

        // Handshake state carried from ServerResources until the ReadyEvent spawn barrier completes.
        int _readyEventId {};
        float _serverTickRate {};
        // One-shot latch so a stray ready-event completion can't re-run the mod's finalization.
        bool _connectionFinalized {};

        // Human-readable reason for the last remote disconnection; empty when it was locally
        // initiated (user quit). Set before OnConnectionClosed() fires.
        std::string _lastDisconnectionReason;

        // One-shot: serve http://resources/<resource>/<file> from the asset cache (scripted web views).
        bool _resourceSchemeRegistered {};

        // Cold-start deep-link URL; delivered to OnProtocolLaunch from Update().
        std::string _pendingProtocolUrl;

        UI::ChatBox _chatBox;

        void InitNetworkingMessages();
        void InitAssetDownloader();
        void InitProtocolHandler();
        void OnAssetsDownloaded(bool success);
        void RegisterResourceSchemeHandler();
        // Targeted delta re-sync for a hot-reload (does not stop all resources).
        void SyncResourceUpdatesFromServer();
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
        virtual void OnAssetsDownloadProgress(const AssetDownloadStatus &status) {
            (void)status;
        }
        virtual void OnConnectionPhaseChanged(ConnectionPhase phase) {
            (void)phase;
        }
        virtual void OnChatMessageReceived(const Framework::Networking::RPC::ChatMessage &msg) {
            (void)msg;
        }

        // Deep link (custom URL protocol) that launched the client. Fires once from Update() after
        // PostInit. url is the raw, un-decoded URI.
        virtual void OnProtocolLaunch(const std::string &url) {
            (void)url;
        }

        [[nodiscard]] Utils::Result<void, Error> RenderInit();

        void DownloadsAssetsFromConnectedServer();

      private:
        // Cancel any in-flight transfer and kick off a fresh asset download from the connected
        // server. The single call site for MafiaNet's DownloadFromSubdirectory.
        void StartAssetDownload();

      public:
        InstanceOptions &GetOptions() {
            return _opts;
        }

        CurrentState GetCurrentState() const {
            return _currentState;
        }

        const std::string &GetLastDisconnectionReason() const {
            return _lastDisconnectionReason;
        }

        void SetCurrentState(CurrentState state) {
            _currentState = std::move(state);
            _currentState.serverIDHash = Framework::Utils::Hashing::CalculateCRC32(_opts.modSlug + ":" + _currentState.host + ":" + std::to_string(_currentState.port));
        }

        // Emits the reserved "chatMessage" script event, then calls OnChatMessageReceived.
        void DispatchReceivedChat(const Framework::Networking::RPC::ChatMessage &msg);

        // Send a chat line to the server (the local player's outgoing text).
        void SendChatMessage(const std::string &text);

        // Built-in chat overlay, inert until a mod renders it. Driven by the Chat scripting builtin.
        UI::ChatBox &GetChatBox() {
            return _chatBox;
        }
        const UI::ChatBox &GetChatBox() const {
            return _chatBox;
        }

        Networking::Engine *GetNetworkingEngine() const {
            return _networkingEngine.get();
        }

        External::Discord::Wrapper *GetPresence() const {
            return _presence.get();
        }

        External::ImGUI::Wrapper *GetImGUI() const {
            return _imguiApp.get();
        }

        External::Sentry::Wrapper *GetCrashReporter() const {
            return _crashReporter.get();
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

        ConnectionPhase GetConnectionPhase() const {
            return _connectionPhase;
        }

        // Set the phase; fires OnConnectionPhaseChanged on change.
        void SetConnectionPhase(ConnectionPhase phase);

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
