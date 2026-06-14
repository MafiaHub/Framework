/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "instance.h"

#include "integrations/shared/rpc/emit_lua_event.h"

#include "networking/rpc/rpc.h"
#include "networking/rpc/chat_message.h"
#include "networking/rpc/client_identity.h"
#include "networking/rpc/server_resources.h"

#include "scripting/resource/resource_manager.h"
#include "scripting/builtins/events.h"

#include "networking/state.h"
#include "networking/replication/replication_manager.h"

#include <cppfs/cppfs.h>
#include <cppfs/FilePath.h>
#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>

#include <logging/logger.h>

#include "utils/path.h"
#include "utils/version.h"
#include "utils/hardware_id.h"

#include "core_modules.h"

#include "graphics/backend/d3d11.h"
#include "graphics/backend/d3d12.h"
#include "graphics/backend/d3d9.h"

namespace Framework::Integrations::Client {
    namespace {
        // Handler for server-emitted scripting events; reaches the scripting engine through the
        // CoreModules singleton.
        void OnEmitLuaEvent(const Shared::RPC::EmitLuaEvent &rpc, MafiaNet::Packet *packet) {
            (void)packet;
            const auto eventName = rpc.GetEventName();
            if (eventName.empty()) {
                return;
            }
            const auto payloadStr = rpc.GetPayload();

            auto *scriptingModule = static_cast<Client::Scripting::ClientScriptingModule *>(Framework::CoreModules::GetScriptingModule());
            if (!scriptingModule) {
                return;
            }

            // Emit to JavaScript resources via the Events system
            auto resourceManager = scriptingModule->GetResourceManager();
            if (!resourceManager) {
                return;
            }

            auto *engine = scriptingModule->GetEngine();
            if (!engine || !engine->IsInitialized()) {
                return;
            }

            v8::Isolate *isolate = engine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine->GetContext();
            v8::Context::Scope contextScope(context);

            // Parse JSON payload and emit event
            std::vector<v8::Local<v8::Value>> args;
            if (!payloadStr.empty()) {
                v8::Local<v8::String> jsonStr;
                if (!v8::String::NewFromUtf8(isolate, payloadStr.c_str()).ToLocal(&jsonStr)) {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Failed to create V8 string from event payload: {}", payloadStr);
                    return;
                }

                v8::TryCatch tryCatch(isolate);
                v8::Local<v8::Value> parsed;
                if (!v8::JSON::Parse(context, jsonStr).ToLocal(&parsed)) {
                    if (tryCatch.HasCaught()) {
                        v8::String::Utf8Value errorMsg(isolate, tryCatch.Exception());
                        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Failed to parse event payload JSON: {}", *errorMsg ? *errorMsg : "unknown error");
                    }
                    else {
                        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Failed to parse event payload JSON: {}", payloadStr);
                    }
                    return;
                }
                args.push_back(parsed);
            }

            resourceManager->GetEvents().EmitReserved(isolate, context, eventName, args);
        }
    } // namespace

    bool AssetDownloadFileProgress::OnFile(MafiaNet::FileListTransferCBInterface::OnFileStruct *onFileStruct) {
        if (onFileStruct->numberOfFilesInThisSet > 0) {
            auto &downloadStatus    = _instance->GetAssetDownloadStatus();
            downloadStatus.progress = onFileStruct->bytesDownloadedForThisSet / float(onFileStruct->byteLengthOfThisSet);
            if (onFileStruct->bytesDownloadedForThisFile == onFileStruct->byteLengthOfThisFile) {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Asset downloaded ({}/{} - {}%): {}", onFileStruct->fileIndex + 1, onFileStruct->numberOfFilesInThisSet, int(downloadStatus.progress * 100.0f), onFileStruct->fileName);
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->flush();
            }
        }
        return true;
    }

    void AssetDownloadFileProgress::OnFileProgress(MafiaNet::FileListTransferCBInterface::FileProgressStruct *fps) {
        auto &downloadStatus    = _instance->GetAssetDownloadStatus();
        auto onFileStruct       = fps->onFileStruct;
        downloadStatus.progress = onFileStruct->byteLengthOfThisSet / float(onFileStruct->bytesDownloadedForThisSet);
    }

    bool AssetDownloadFileProgress::OnDownloadComplete(DownloadCompleteStruct *dcs) {
        (void)dcs;

        auto &downloadStatus       = _instance->GetAssetDownloadStatus();
        downloadStatus.progress    = 1.0f;
        downloadStatus.downloading = false;
        _instance->OnAssetsDownloaded(true);
        return false;
    }

    Instance::Instance() {
        _networkingEngine = std::make_unique<Networking::Engine>();
        _presence         = std::make_unique<External::Discord::Wrapper>();
        _imguiApp         = std::make_unique<External::ImGUI::Wrapper>();
        _renderer         = std::make_unique<Graphics::Renderer>();
        _renderIO         = std::make_unique<Graphics::RenderIO>();
        _scriptingModule  = std::make_unique<Client::Scripting::ClientScriptingModule>();
        _webManager = std::make_unique<Framework::GUI::Manager>();
    }

    Instance::~Instance() = default;

    Utils::Result<void, Error> Instance::Init(InstanceOptions &opts) {
        _opts = opts;

        if (opts.gameName.empty() || opts.gameVersion.empty()) {
            return Error("Game name and version are required");
        }

        if (opts.usePresence) {
            if (_presence && opts.discordAppId > 0) {
                if (_presence->Init(opts.discordAppId) != External::DiscordError::DISCORD_NONE) {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Discord Presence failed to initialize");
                }
                else {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Discord presence initialized");
                }
            }
        }

        if (_networkingEngine) {
            if (auto netResult = _networkingEngine->Init(); !netResult) {
                return netResult;
            }
            CoreModules::SetNetworkPeer(_networkingEngine->GetNetworkClient());
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Networking engine initialized");
        }

        CoreModules::SetWebManager(_webManager.get());

        InitNetworkingMessages();
        InitAssetDownloader();
        
        if (!opts.initRendererManually) {
            if (auto renderResult = RenderInit(); !renderResult) {
                return renderResult;
            }
        }

        PostInit();
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Mod subsystems initialized");

        
        // Store reference to the input system
        CoreModules::SetInput(GetBaseInput());

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Client has been initialized");
        _initialized = true;
        return {};
    }

    void Instance::InitAssetDownloader() {
        InitCacheAssetFolders();

        GetNetworkingEngine()->GetNetworkClient()->SetOnAssetsDownloadFailedCallback([this]() {
            this->OnAssetsDownloaded(false);
        });
    }

    void Instance::InitCacheAssetFolders() {
        const auto appDataPath = Framework::Utils::GetAppDataPathA();
        cppfs::fs::open(fmt::format("{}\\MafiaHubIntegration", appDataPath)).createDirectory();
        cppfs::fs::open(fmt::format("{}\\MafiaHubIntegration\\servers", appDataPath)).createDirectory();
    }

    Utils::Result<void, Error> Instance::RenderInit() {
        if (_renderInitialized) {
            return {};
        }

        // Init the render device
        if (_opts.useRenderer) {
            if (_renderer) {
                if (auto renderResult = _renderer->Init(_opts.rendererOptions); !renderResult) {
                    return renderResult;
                }

                _renderer->SetWindow(_opts.rendererOptions.windowHandle);

                bool backendInitOk = false;
                switch (_opts.rendererOptions.backend) {
                case Graphics::RendererBackend::BACKEND_D3D_9: backendInitOk = _renderer->GetD3D9Backend()->Init(_opts.rendererOptions); break;
                case Graphics::RendererBackend::BACKEND_D3D_11: backendInitOk = _renderer->GetD3D11Backend()->Init(_opts.rendererOptions); break;
                case Graphics::RendererBackend::BACKEND_D3D_12: backendInitOk = _renderer->GetD3D12Backend()->Init(_opts.rendererOptions); break;
                default: Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->info("[renderDevice] Device not implemented"); break;
                }
                if (!backendInitOk) {
                    return Error("Failed to initialize graphics backend");
                }
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Rendering systems initialized");
            }

            if (_opts.useImGUI) {
                // Init the ImGui internal instance
                External::ImGUI::Config imguiConfig;
                imguiConfig.renderBackend = _opts.rendererOptions.backend;
                imguiConfig.windowBackend = _opts.rendererOptions.platform;
                imguiConfig.renderer      = _renderer.get();
                imguiConfig.windowHandle  = _renderer->GetWindow();
                if (_imguiApp->Init(imguiConfig) != External::ImGUI::Error::IMGUI_NONE) {
                    Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->info("ImGUI has failed to init");
                }
            }
        }

        _renderInitialized = true;
        return {};
    }

    void Instance::Shutdown() {
        PreShutdown();

        if (_renderer && _renderer->IsInitialized()) {
            _renderer->Shutdown();
        }

        if (_presence && _presence->IsInitialized()) {
            _presence->Shutdown();
        }

        if (_networkingEngine) {
            _networkingEngine->Shutdown();
        }

        if (_scriptingModule) {
            _scriptingModule->Shutdown();
        }

        if (_imguiApp && _imguiApp->IsInitialized()) {
            _imguiApp->Shutdown();
        }

        CoreModules::SetScriptingModule(nullptr);
        CoreModules::SetWebManager(nullptr);
        CoreModules::SetNetworkPeer(nullptr);
        CoreModules::SetReplication(nullptr);
        CoreModules::SetInput(nullptr);
        CoreModules::Reset();

        Lifecycle::Shutdown();
    }

    void Instance::Update() {
        if (_presence && _presence->IsInitialized()) {
            _presence->Update();
        }

        if (_networkingEngine) {
            _networkingEngine->Update();
        }

        if (_scriptingModule) {
            _scriptingModule->Update();
        }

        if (_imguiApp && _imguiApp->IsInitialized()) {
            _imguiApp->Update();
        }

        if (_renderIO) {
            _renderIO->UpdateMainThread();
        }

        if (_webManager) {
            _webManager->Update();
        }

        PostUpdate();
    }

    void Instance::Render() {
        if (_renderer && _renderer->IsInitialized()) {
            _renderer->Update();
        }

        if (_renderIO) {
            _renderIO->UpdateRenderThread();
        }

        PostRender();
    }

    void Instance::InitNetworkingMessages() {
        const auto net = _networkingEngine->GetNetworkClient();
        // Build gate: NetworkClient challenges automatically on connect; a mismatch drops us.
        net->SetBuildToken(Framework::Networking::NetworkPeer::BuildToken(_opts.gameName, _opts.gameVersion, Utils::Version::rel, _opts.modVersion));

        net->SetOnPlayerConnectedCallback([](MafiaNet::Packet *) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Connection accepted by server, verifying build");
        });

        // Server's resource list. Store it (survives a scripting module reset) and start the asset
        // phase; the ready-event id and tick rate are held until the spawn barrier completes.
        net->RegisterRPC<Framework::Networking::RPC::ServerResources>([this](const Framework::Networking::RPC::ServerResources &payload, MafiaNet::Packet *) {
            _readyEventId   = payload.readyEventId;
            _serverTickRate = payload.tickRate;

            _pendingServerResources = payload.resources;
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Received resource list from server with {} resources", _pendingServerResources.size());

            DownloadsAssetsFromConnectedServer();
        });

        // Spawn barrier complete: activate replication and report the connection final.
        net->SetOnConnectionReadyCallback([this, net](int eventId) {
            // Only the event the server assigned in ServerResources finalizes this connection, and
            // only once — a stray or repeated completion must not re-run the mod's spawn logic.
            if (eventId != _readyEventId || _connectionFinalized) {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Ignoring ready event {} (expected {}, finalized: {})", eventId, _readyEventId, _connectionFinalized);
                return;
            }
            _connectionFinalized = true;
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Connection ready (event {}), finalizing", eventId);
            // tickInterval is in seconds; SetAutoSerializeInterval wants milliseconds.
            if (auto *replication = net->GetReplicationManager()) {
                CoreModules::SetReplication(replication);
                replication->SetAutoSerializeInterval(static_cast<MafiaNet::Time>(_serverTickRate * 1000.0f));
            }
            if (_onConnectionFinalized) {
                _onConnectionFinalized(_serverTickRate);
            }
        });

        // Version mismatches don't reach here — they fail the build challenge (WRONG_VERSION).
        net->SetOnPlayerDisconnectedCallback([this](MafiaNet::Packet *packet, Framework::Networking::DisconnectionReason reasonId, const std::string &customReason) {
            std::string reason = "Unknown.";
            switch (reasonId) {
            case Framework::Networking::DisconnectionReason::BANNED: reason = "You are banned."; break;
            case Framework::Networking::DisconnectionReason::KICKED: reason = "You have been kicked."; break;
            case Framework::Networking::DisconnectionReason::KICKED_CUSTOM: reason = "You have been kicked. Reason: " + customReason; break;
            case Framework::Networking::DisconnectionReason::KICKED_INVALID_PACKET: reason = "You have been kicked (invalid packet)."; break;
            case Framework::Networking::DisconnectionReason::WRONG_VERSION: reason = "You have been kicked (wrong client version)."; break;
            case Framework::Networking::DisconnectionReason::INVALID_PASSWORD: reason = "You have been kicked (wrong password)."; break;
            default: break;
            }
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Connection dropped: {}", reason);

            // Reset initial asset download state
            _initialDownloadDone = false;
            _downloadStatus      = {};
            _connectionFinalized = false;
            
            // Entity teardown is native: ReplicaManager3 deletes server-created replicas when the
            // connection drops (QueryActionOnPopConnection_Client).
            CoreModules::SetReplication(nullptr);

            // Notify mod-level that network integration got closed
            if (_onConnectionClosed) {
                _onConnectionClosed();
            }

            // Reset the scripting engine (keeps engine alive, just stops resources)
            _scriptingModule->Reset();

            // Unregister from CoreModules so a subsequent reconnect can re-register
            // without tripping the "already registered" assertion
            CoreModules::SetScriptingModule(nullptr);

            // Destroy scriptable web views
            if (_webManager) {
                _webManager->CleanupViews();
            }
        });

        net->RegisterRPC<Shared::RPC::EmitLuaEvent>(&OnEmitLuaEvent);

        // Chat lines from the server are forwarded to the mod's UI via the received callback.
        net->RegisterRPC<Framework::Networking::RPC::ChatMessage>([this](const Framework::Networking::RPC::ChatMessage &payload, MafiaNet::Packet *) {
            DispatchReceivedChat(payload.text);
        });

        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Networking messages registered");
    }

    void Instance::SendChatMessage(const std::string &text) {
        if (text.empty()) {
            return;
        }
        const auto net = GetNetworkingEngine()->GetNetworkClient();
        if (!net) {
            return;
        }
        Framework::Networking::RPC::ChatMessage payload {text};
        net->BroadcastRPC(payload);
    }

    void Instance::DownloadsAssetsFromConnectedServer() {
        const auto net = GetNetworkingEngine()->GetNetworkClient();

        // Make sure we're connected to the server already, otherwise bail with warning
        if (net->GetConnectionState() != Framework::Networking::PeerState::CONNECTED) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("We can't download assets if we are not connected to the server yet!");
            return;
        }

        // Stop running resources before redownloading (preserves server resource list)
        _scriptingModule->StopAllResources();

        // Destroy scriptable web views
        if (_webManager) {
            _webManager->CleanupViews();
        }

        // Setup the asset downloader
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Setting up asset downloads...");
        const auto streamer = net->GetAssetStreamer();

        // Compute the destination path
        const auto appDataPath = Framework::Utils::GetAppDataPathA();
        const auto cacheDir   = fmt::format("{}\\MafiaHubIntegration\\servers\\{}", appDataPath, _currentState.serverIDHash); // TODO: fix path to use mod name

        // Let the system know where our scripts are stored
        SetAssetCachePath(cacheDir);
        streamer->SetApplicationDirectory(cacheDir.c_str());
        auto cacheDirHandle = cppfs::fs::open(cacheDir);

        // Ensure we stop existing downloads since the server has pushed new changes already
        if (_downloadStatus.downloading) {
            net->GetFileListTransfer()->CancelReceive(_downloadStatus.setID);
            _downloadStatus = {};
        }

        if (!cacheDirHandle.exists()) {
            if (cacheDirHandle.createDirectory()) {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Client asset cache: {}", _currentState.serverIDHash);
            }
            else {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Could not create folder for client asset cache: {}", cacheDir);
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Skip downloading assets.");
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->flush();

                // Ensure we finish the download flow gracefully
                OnAssetsDownloaded(false);
                return;
            }
        }
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->flush();

        _downloadStatus.setID = streamer->DownloadFromSubdirectory(nullptr, nullptr, true, net->GetPeer()->GetSystemAddressFromIndex(0), new AssetDownloadFileProgress(this), HIGH_PRIORITY, 2, nullptr);
    }

    void Instance::OnAssetsDownloaded(bool success) {
        const auto net = GetNetworkingEngine()->GetNetworkClient();
        if (success) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("All the assets have been downloaded!");

            auto scriptingModule = GetScriptingModule();
            if (scriptingModule) {
                // Set resource cache path before init
                scriptingModule->SetResourceCachePath(GetAssetCachePath());

                // Initialize the scripting module with builtin registration callback
                const auto sdkCallback = [this](Framework::Scripting::Engine *engine) {
                    this->RegisterScriptingBuiltins(engine);
                };

                if (scriptingModule->Init(sdkCallback) != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Client scripting engine failed to initialize");
                    net->Disconnect();
                    return;
                }
                CoreModules::SetScriptingModule(scriptingModule);

                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Client scripting engine initialized");

                // Pass the pending resource list to the scripting module (without triggering download logic)
                if (!_pendingServerResources.empty()) {
                    scriptingModule->SetServerResourceList(_pendingServerResources);
                }

                // Start all resources via ResourceManager
                if (!scriptingModule->StartAllResources()) {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Failed to start client resources");
                }
                else {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Client resources started successfully");
                }
            }
        }
        else {
            net->Disconnect();
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("There has been an issue downloading assets!");
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->flush();
            return;
        }
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->flush();

        // Announce ourselves (server builds the avatar and opens the replication gate), then arm our
        // half of the spawn barrier. First connect only.
        if (!_initialDownloadDone) {
            _initialDownloadDone = true;

            const auto serverGuid = net->GetPeer()->GetGUIDFromIndex(0);

            Framework::Networking::RPC::ClientIdentity identity;
            identity.name       = _currentState.nickname;
            identity.steamId    = ""; // no Steam integration wired into the client yet
            identity.discordId  = _presence ? _presence->GetUserId() : "";
            identity.hardwareId = Framework::Utils::GetHardwareId();
            net->SendRPC(identity, serverGuid);

            net->GetReadyEvent()->AddToWaitList(_readyEventId, serverGuid);
            net->GetReadyEvent()->SetEvent(_readyEventId, true);
        }

        _downloadStatus = {};

        // Let the mod-level know assets have just been finished processing
        if (_onAssetsDownloadFinished) {
            _onAssetsDownloadFinished(success);
        }
    }

    void Instance::RegisterScriptingBuiltins(Framework::Scripting::Engine *engine) {
        // JavaScript bindings are registered by ClientScriptingModule::RegisterFrameworkBindings
        // This method is called to allow mod-specific customization
        ModuleRegister(engine);
    }
} // namespace Framework::Integrations::Client
