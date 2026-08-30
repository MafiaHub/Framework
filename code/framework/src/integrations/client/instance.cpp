/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "instance.h"

#include "integrations/shared/rpc/emit_script_event.h"

#include "networking/rpc/rpc.h"
#include "networking/rpc/chat_message.h"
#include "networking/rpc/voice_settings.h"
#include "networking/rpc/client_identity.h"
#include "networking/rpc/nametag.h"
#include "networking/rpc/resource_refresh.h"
#include "networking/rpc/server_resources.h"

#include "scripting/resource/resource_manager.h"
#include "scripting/builtins/events.h"

#include "networking/state.h"
#include "networking/replication/replication_manager.h"
#include "networking/replication/nametag_state.h"

#include <cppfs/cppfs.h>
#include <cppfs/FilePath.h>
#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>

#include <filesystem>

#include <logging/logger.h>

#include "utils/path.h"
#include "utils/profiler.h"
#include "utils/version.h"
#include "utils/time.h"
#include "utils/hardware_id.h"

#include "core_modules.h"

#include "graphics/backend/d3d11.h"
#include "graphics/backend/d3d12.h"
#include "graphics/backend/d3d9.h"

namespace Framework::Integrations::Client {
    namespace {
        // Ordering channel for the asset-download file transfer. Wire-affecting: the server must
        // use the same channel for these transfers to stay ordered relative to each other.
        constexpr char kAssetDownloadOrderingChannel = 2;

        // Handler for server-emitted scripting events; reaches the scripting engine through the
        // CoreModules singleton.
        void OnEmitScriptEvent(const Shared::RPC::EmitScriptEvent &rpc, MafiaNet::Packet *packet) {
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

            Framework::Scripting::Builtins::Events &events = resourceManager->GetEvents();
            events.EmitReserved(isolate, context, eventName, args);
        }

        // Reserved "chatMessage" event carrying { author, text, color }.
        void EmitChatMessageEvent(const Framework::Networking::RPC::ChatMessage &msg) {
            auto *scriptingModule = static_cast<Client::Scripting::ClientScriptingModule *>(Framework::CoreModules::GetScriptingModule());
            if (!scriptingModule) {
                return;
            }
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

            v8::Local<v8::String> authorStr;
            v8::Local<v8::String> textStr;
            if (!v8::String::NewFromUtf8(isolate, msg.author.c_str()).ToLocal(&authorStr) || !v8::String::NewFromUtf8(isolate, msg.text.c_str()).ToLocal(&textStr)) {
                return;
            }
            v8::Local<v8::Object> obj = v8::Object::New(isolate);
            obj->Set(context, v8::String::NewFromUtf8Literal(isolate, "author"), authorStr).Check();
            obj->Set(context, v8::String::NewFromUtf8Literal(isolate, "text"), textStr).Check();
            obj->Set(context, v8::String::NewFromUtf8Literal(isolate, "color"), v8::Uint32::NewFromUnsigned(isolate, msg.color)).Check();

            std::vector<v8::Local<v8::Value>> args {obj};
            Framework::Scripting::Builtins::Events &events = resourceManager->GetEvents();
            events.EmitReserved(isolate, context, "chatMessage", args);
        }

        // Reserved "chatSend" event. False means a handler blocked the line; no scripting, no veto.
        bool EmitChatSendEvent(const std::string &text) {
            auto *scriptingModule = static_cast<Client::Scripting::ClientScriptingModule *>(Framework::CoreModules::GetScriptingModule());
            if (!scriptingModule) {
                return true;
            }
            auto resourceManager = scriptingModule->GetResourceManager();
            if (!resourceManager) {
                return true;
            }
            auto *engine = scriptingModule->GetEngine();
            if (!engine || !engine->IsInitialized()) {
                return true;
            }

            v8::Isolate *isolate = engine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine->GetContext();
            v8::Context::Scope contextScope(context);

            v8::Local<v8::String> textStr;
            if (!v8::String::NewFromUtf8(isolate, text.c_str()).ToLocal(&textStr)) {
                return true;
            }

            std::vector<v8::Local<v8::Value>> args {textStr};
            return resourceManager->GetEvents().EmitReservedSync(isolate, context, "chatSend", args);
        }
    } // namespace

    void Instance::DispatchReceivedChat(const Framework::Networking::RPC::ChatMessage &msg) {
        EmitChatMessageEvent(msg);
        if (_chatBox.IsVisible()) {
            _chatBox.AddMessage(msg.author, msg.text, msg.color);
        }
        OnChatMessageReceived(msg);
    }

    bool AssetDownloadFileProgress::OnFile(MafiaNet::FileListTransferCBInterface::OnFileStruct *onFileStruct) {
        if (onFileStruct->numberOfFilesInThisSet > 0) {
            auto &downloadStatus           = _instance->GetAssetDownloadStatus();
            downloadStatus.downloading     = true;
            downloadStatus.setID           = onFileStruct->setID;
            downloadStatus.filesTotal      = onFileStruct->numberOfFilesInThisSet;
            downloadStatus.bytesTotal      = onFileStruct->byteLengthOfThisSet;
            downloadStatus.bytesDownloaded = onFileStruct->bytesDownloadedForThisSet;
            downloadStatus.currentFile     = onFileStruct->fileName;
            downloadStatus.progress        = onFileStruct->byteLengthOfThisSet > 0 ? onFileStruct->bytesDownloadedForThisSet / float(onFileStruct->byteLengthOfThisSet) : 0.0f;
            if (onFileStruct->bytesDownloadedForThisFile == onFileStruct->byteLengthOfThisFile) {
                downloadStatus.filesDownloaded = onFileStruct->fileIndex + 1;
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Asset downloaded ({}/{} - {}%): {}", onFileStruct->fileIndex + 1, onFileStruct->numberOfFilesInThisSet, int(downloadStatus.progress * 100.0f), onFileStruct->fileName);
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->flush();
            }
            _instance->OnAssetsDownloadProgress(downloadStatus);
        }
        return true;
    }

    void AssetDownloadFileProgress::OnFileProgress(MafiaNet::FileListTransferCBInterface::FileProgressStruct *fps) {
        auto *onFileStruct = fps->onFileStruct;
        if (!onFileStruct || onFileStruct->byteLengthOfThisSet == 0) {
            return;
        }
        auto &downloadStatus           = _instance->GetAssetDownloadStatus();
        downloadStatus.downloading     = true;
        downloadStatus.setID           = onFileStruct->setID;
        downloadStatus.filesTotal      = onFileStruct->numberOfFilesInThisSet;
        downloadStatus.bytesTotal      = onFileStruct->byteLengthOfThisSet;
        downloadStatus.bytesDownloaded = onFileStruct->bytesDownloadedForThisSet;
        downloadStatus.currentFile     = onFileStruct->fileName;
        downloadStatus.progress        = onFileStruct->bytesDownloadedForThisSet / float(onFileStruct->byteLengthOfThisSet);
        _instance->OnAssetsDownloadProgress(downloadStatus);
    }

    bool AssetDownloadFileProgress::OnDownloadComplete(DownloadCompleteStruct *dcs) {
        (void)dcs;

        auto &downloadStatus           = _instance->GetAssetDownloadStatus();
        downloadStatus.progress        = 1.0f;
        downloadStatus.downloading     = false;
        downloadStatus.bytesDownloaded = downloadStatus.bytesTotal;
        downloadStatus.filesDownloaded = downloadStatus.filesTotal;
        _instance->OnAssetsDownloadProgress(downloadStatus);
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
        _crashReporter = &External::Sentry::GetCrashReporter();

        // Typed lines go through "chatSend" first; Chat.send is the raw path, so a handler can
        // veto and resend without re-entering itself.
        _chatBox.SetSubmitHandler([this](const std::string &text) {
            if (!EmitChatSendEvent(text)) {
                return;
            }
            SendChatMessage(text);
        });
    }

    Instance::~Instance() = default;

    Utils::Result<void, Error> Instance::Init(InstanceOptions &opts) {
        _opts = opts;

        if (opts.gameName.empty() || opts.gameVersion.empty()) {
            return Error("Game name and version are required");
        }

        CoreModules::SetClientInstance(this);

        // Crash reporting comes up first so its handler is installed before anything else can fault.
        // An entry-point InitCrashReporter already installed it; this is then a no-op and only the
        // decoration below applies.
        if (_crashReporter && !opts.sentryDSN.empty()) {
            External::Sentry::InitOptions sentryOpts;
            sentryOpts.dsn         = opts.sentryDSN;
            sentryOpts.handlerPath = opts.sentryModulePath.empty() ? "." : opts.sentryModulePath;
            sentryOpts.release     = opts.sentryRelease.empty() ? opts.gameName + "@" + opts.gameVersion : opts.sentryRelease;
            sentryOpts.environment = opts.sentryEnvironment;
            sentryOpts.attachments = opts.sentryAttachments;
            if (auto sentryResult = External::Sentry::InitCrashReporter(sentryOpts); !sentryResult) {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Crash reporting disabled: {}", sentryResult.GetError().message);
            }
            else {
                _crashReporter->SetGameInformation({opts.gameName, opts.gameVersion + " / mod " + opts.modVersion});
                _crashReporter->SetTag("net.role", "client");
                _crashReporter->SetTag("build.game_version", opts.gameVersion);
                _crashReporter->SetTag("build.mod_version", opts.modVersion);

                const auto *logger = Logging::GetInstance();
                _crashReporter->AddAttachment(logger->GetLogFolder() + "/" + logger->GetLogName() + ".log");

                auto *reporter = _crashReporter;
                Logging::GetInstance()->SetLogForwarder([reporter](int level, const std::string &name, const std::string &message) {
                    External::Sentry::Level mapped = External::Sentry::Level::Info;
                    if (level >= spdlog::level::critical) {
                        mapped = External::Sentry::Level::Fatal;
                    }
                    else if (level >= spdlog::level::err) {
                        mapped = External::Sentry::Level::Error;
                    }
                    else if (level >= spdlog::level::warn) {
                        mapped = External::Sentry::Level::Warning;
                    }
                    reporter->AddBreadcrumb(name, message, mapped);
                });

                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Crash reporting initialized");
            }
        }

        if (opts.usePresence) {
            if (_presence && opts.discordAppId > 0) {
                if (auto discordResult = _presence->Init(opts.discordAppId); !discordResult) {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Discord Presence failed to initialize: {}", discordResult.GetError().message);
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

            // Attaches RakVoice to the live peer, so it must follow the networking engine.
            // The relay session itself opens later, on connect.
            if (_voiceClient.Init(_networkingEngine->GetNetworkClient())) {
                CoreModules::SetVoiceClient(&_voiceClient);
            }
            else {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice client unavailable; voice chat disabled");
            }
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

        InitProtocolHandler();

        
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

    void Instance::InitProtocolHandler() {
#ifdef _WIN32
        char urlBuffer[2048] = {};
        if (GetEnvironmentVariableA("MafiaHubLaunchURL", urlBuffer, sizeof(urlBuffer)) > 0 && urlBuffer[0]) {
            _pendingProtocolUrl = urlBuffer;
            SetEnvironmentVariableA("MafiaHubLaunchURL", nullptr); // clear so it can't leak into children
        }
#endif
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

                // Renderer::Init already built and initialized the backend for the
                // configured API; initializing it a second time here rebuilt every
                // descriptor heap, allocator and command list, leaking the first set
                // along with a device reference.
                _renderer->SetWindow(_opts.rendererOptions.windowHandle);

                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Rendering systems initialized");
            }

            if (_opts.useImGUI) {
                // Init the ImGui internal instance
                External::ImGUI::Config imguiConfig;
                imguiConfig.renderBackend = _opts.rendererOptions.backend;
                imguiConfig.windowBackend = _opts.rendererOptions.platform;
                imguiConfig.renderer      = _renderer.get();
                imguiConfig.windowHandle  = _renderer->GetWindow();
                imguiConfig.fontPath      = _opts.imguiFontPath;
                imguiConfig.fontSize      = _opts.imguiFontSize;
                if (auto imguiResult = _imguiApp->Init(imguiConfig); !imguiResult) {
                    Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->info("ImGUI has failed to init: {}", imguiResult.GetError().message);
                }
            }
        }

        _renderInitialized = true;
        return {};
    }

    void Instance::Shutdown() {
        PreShutdown();

        // Before the renderer: CefShutdown must drain the browsers while the device is
        // alive, else the guarded pump faults and orphans cef_subprocess.exe.
        if (_webManager && _webManager->IsInitialized()) {
            _webManager->Shutdown();
        }

        if (_renderer && _renderer->IsInitialized()) {
            _renderer->Shutdown();
        }

        if (_presence && _presence->IsInitialized()) {
            _presence->Shutdown();
        }

        // Before the networking engine: this detaches the plugin from the peer.
        _voiceClient.Shutdown();

        if (_networkingEngine) {
            _networkingEngine->Shutdown();
        }

        if (_scriptingModule) {
            _scriptingModule->Shutdown();
        }

        if (_imguiApp && _imguiApp->IsInitialized()) {
            _imguiApp->Shutdown();
        }

        // Drain, never close: the reporter outlives this instance.
        if (_crashReporter && _crashReporter->IsInitialized()) {
            _crashReporter->Flush();
        }

        CoreModules::SetScriptingModule(nullptr);
        CoreModules::SetWebManager(nullptr);
        CoreModules::SetVoiceClient(nullptr);
        CoreModules::SetNetworkPeer(nullptr);
        CoreModules::SetReplication(nullptr);
        CoreModules::SetInput(nullptr);
        CoreModules::SetClientInstance(nullptr);
        CoreModules::Reset();

        Lifecycle::Shutdown();

        // Last: flush and tear down the async logging thread pool before static
        // destruction can race it.
        Logging::GetInstance()->Shutdown();
    }

    void Instance::Update() {
        FW_PROFILE_SCOPE_N("Client::Update");

        // Deliver the cold-start deep link (after PostInit).
        if (!_pendingProtocolUrl.empty()) {
            std::string url;
            url.swap(_pendingProtocolUrl);
            OnProtocolLaunch(url);
        }

        if (_presence && _presence->IsInitialized()) {
            FW_PROFILE_SCOPE_N("Client::Presence");
            _presence->Update();
        }

        UpdateNetworking();

        if (_scriptingModule) {
            FW_PROFILE_SCOPE_N("Client::Scripting");
            _scriptingModule->Update();
        }

        if (_imguiApp && _imguiApp->IsInitialized()) {
            FW_PROFILE_SCOPE_N("Client::ImGui");
            _imguiApp->Update();
        }

        if (_renderIO) {
            FW_PROFILE_SCOPE_N("Client::RenderIO");
            _renderIO->UpdateMainThread();
        }

        if (_webManager) {
            FW_PROFILE_SCOPE_N("Client::WebManager");
            _webManager->Update();
        }

        {
            FW_PROFILE_SCOPE_N("Client::PostUpdate");
            PostUpdate();
        }

        FW_PROFILE_FRAME();
    }

    void Instance::UpdateNetworking() {
        if (!_networkingEngine) {
            return;
        }

        FW_PROFILE_SCOPE_N("Client::Networking");
        _networkingEngine->Update();
        TrySignalConnectionSpawnReady();

        // After the peer pump: RakVoice decodes inbound frames from inside RakPeer::Receive,
        // so draining speakers here picks up this tick's audio rather than last tick's.
        {
            FW_PROFILE_SCOPE_N("Client::Voice");

            // The chat box and web views belong to the framework, so it enforces this itself
            // rather than trusting every mod to remember.
            _voiceClient.SetInputSuppressed(_chatBox.IsInputActive() || (_webManager && _webManager->IsAnyViewFocused()));

            // Speaker positions come from the replicated entity set, as the server's voice
            // router gets them: an owner GUID means a player-controlled entity. Done here so
            // a mod only has to supply the listener transform.
            if (auto *replication = _networkingEngine->GetNetworkClient()->GetReplicationManager()) {
                _voiceClient.BeginSpeakerUpdate();
                replication->ForEachEntity([this](Framework::Networking::Replication::NetworkEntity *entity) {
                    if (entity->ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID) {
                        _voiceClient.SetSpeakerPosition(static_cast<uint64_t>(entity->ownerGUID), entity->position);
                    }
                });
                _voiceClient.EndSpeakerUpdate();
            }

            _voiceClient.Update();
        }
    }

    void Instance::Render() {
        FW_PROFILE_SCOPE_N("Client::Render");

        if (_renderer && _renderer->IsInitialized()) {
            FW_PROFILE_SCOPE_N("Client::Renderer");
            _renderer->Update();
        }

        if (_renderIO) {
            FW_PROFILE_SCOPE_N("Client::RenderThreadIO");
            _renderIO->UpdateRenderThread();
        }

        {
            FW_PROFILE_SCOPE_N("Client::PostRender");
            PostRender();
        }

        FW_PROFILE_FRAME_N("Render");
    }

    void Instance::InitNetworkingMessages() {
        const auto net = _networkingEngine->GetNetworkClient();
        // Build gate: NetworkClient challenges automatically on connect; a mismatch drops us.
        if (_opts.verifyBuildToken) {
            net->SetBuildToken(Framework::Networking::NetworkPeer::BuildToken(_opts.gameName, _opts.gameVersion, Utils::Version::rel, _opts.modVersion));
        }
        else {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Build token verification DISABLED; connecting to any server version");
            net->SetBuildToken(Framework::Networking::NetworkPeer::kBuildVerificationDisabledToken);
        }

        net->SetOnPlayerConnectedCallback([this, net](MafiaNet::Packet *packet) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Connection accepted by server, verifying build");

            // ID_CONNECTION_REQUEST_ACCEPTED is withheld by MafiaNet until the session handshake
            // completes, so the server's payload is already in hand here -- earlier than the
            // resource list, the asset download, or any client script.
            _serverConfig = nlohmann::json::object();
            if (packet) {
                const std::string_view raw = net->GetRemoteSessionConfig(packet->guid);
                if (!raw.empty()) {
                    // Remote input: a server can publish anything at all here, so a parse failure is
                    // an ordinary outcome rather than an error worth dropping the connection over.
                    auto parsed = nlohmann::json::parse(raw.begin(), raw.end(), nullptr, false);
                    if (parsed.is_discarded() || !parsed.is_object()) {
                        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Server config is not a JSON object; ignoring {} byte(s)", raw.size());
                    }
                    else {
                        _serverConfig = std::move(parsed);
                        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Received {} server config key(s)", _serverConfig.size());
                    }
                }
            }

            SetConnectionPhase(ConnectionPhase::Authenticating);
        });

        // Server's resource list. Store it (survives a scripting module reset) and start the asset
        // phase; the ready-event id and tick rate are held until the spawn barrier completes.
        net->RegisterRPC<Framework::Networking::RPC::ServerResources>([this](const Framework::Networking::RPC::ServerResources &payload, MafiaNet::Packet *) {
            ++_assetProcessingGeneration;
            _deferredInitialAssetProcessingGeneration = 0;
            _resumingDeferredInitialAssetProcessing    = false;
            _readyEventId   = payload.readyEventId;
            _serverTickRate = payload.tickRate;

            _pendingServerResources = payload.resources;
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Received resource list from server with {} resources", _pendingServerResources.size());

            SetConnectionPhase(ConnectionPhase::Downloading);
            DownloadsAssetsFromConnectedServer();
        });

        // Server hot-reloaded a client resource (dev mode): re-sync its files
        // (delta) and restart just that resource, leaving the rest running.
        net->RegisterRPC<Framework::Networking::RPC::ResourceRefresh>([this](const Framework::Networking::RPC::ResourceRefresh &payload, MafiaNet::Packet *) {
            if (payload.resources.empty()) {
                return;
            }
            // Ignore until connected with a running module, else we'd cancel
            // the initial download (which already fetches current files).
            auto *sm = GetScriptingModule();
            if (!sm || !sm->GetScriptingEngine() || !sm->GetScriptingEngine()->IsInitialized()) {
                return;
            }
            // Accumulate (deduped): one reload arrives as several RPCs and a
            // single delta download covers them all; overwriting would drop all but the last.
            for (const auto &r : payload.resources) {
                bool known = false;
                for (const auto &e : _pendingRefreshResources) {
                    if (e.name == r.name) { known = true; break; }
                }
                if (!known) {
                    _pendingRefreshResources.push_back(r);
                }
            }
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Server hot-reloaded {} resource(s); re-syncing", payload.resources.size());
            // An in-flight re-sync already covers these; it drains the full set on complete.
            if (!_downloadStatus.downloading) {
                SyncResourceUpdatesFromServer();
            }
        });

        // Server stopped a client resource at runtime: stop it here too.
        net->RegisterRPC<Framework::Networking::RPC::ResourceStop>([this](const Framework::Networking::RPC::ResourceStop &payload, MafiaNet::Packet *) {
            if (payload.resources.empty()) {
                return;
            }
            auto *sm = GetScriptingModule();
            if (!sm || !sm->GetScriptingEngine() || !sm->GetScriptingEngine()->IsInitialized()) {
                return;
            }
            auto *rm = sm->GetResourceManager();
            if (!rm) {
                return;
            }
            for (const auto &res : payload.resources) {
                // Drop any queued refresh so a pending sync can't resurrect it.
                for (auto it = _pendingRefreshResources.begin(); it != _pendingRefreshResources.end();) {
                    it = (it->name == res.name) ? _pendingRefreshResources.erase(it) : it + 1;
                }
                if (rm->IsResourceRunning(res.name)) {
                    auto result = rm->StopResource(res.name);
                    if (!result) {
                        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Failed to stop client resource '{}': {}", res.name, result.GetError());
                    }
                }
            }
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
            _spawnBarrierArmed   = false;
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Connection ready (event {}), finalizing", eventId);
            // tickInterval is in seconds; SetAutoSerializeInterval wants milliseconds.
            if (auto *replication = net->GetReplicationManager()) {
                CoreModules::SetReplication(replication);
                replication->SetAutoSerializeInterval(static_cast<MafiaNet::Time>(Framework::Utils::Time::SecondsToMs(_serverTickRate)));
            }
            _chatBox.SetVisible(true);
            _chatBox.SetSessionActive(true);
            SetConnectionPhase(ConnectionPhase::InGame);
            OnConnectionFinalized(_serverTickRate);
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
            case Framework::Networking::DisconnectionReason::NO_FREE_SLOT: reason = "The server is full."; break;
            case Framework::Networking::DisconnectionReason::GRACEFUL_SHUTDOWN: reason = "The server closed the connection."; break;
            case Framework::Networking::DisconnectionReason::LOST: reason = "Connection to the server lost."; break;
            case Framework::Networking::DisconnectionReason::FAILED: reason = "Could not connect to the server."; break;
            default: break;
            }
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Connection dropped: {}", reason);

            // A null packet means the disconnect was locally initiated (user quit) — no reason to surface.
            _lastDisconnectionReason = packet ? reason : "";

            // Reset initial asset download state
            ++_assetProcessingGeneration;
            _deferredInitialAssetProcessingGeneration = 0;
            _resumingDeferredInitialAssetProcessing    = false;
            _serverConfig                              = nlohmann::json::object();
            _initialDownloadDone                       = false;
            _downloadStatus                            = {};
            _connectionFinalized                       = false;
            _spawnBarrierArmed                         = false;
            _projectSpawnReady                         = false;
            SetConnectionPhase(ConnectionPhase::Disconnected);
            
            // Entity teardown is native: ReplicaManager3 deletes server-created replicas when the
            // connection drops (QueryActionOnPopConnection_Client).
            CoreModules::SetReplication(nullptr);

            _chatBox.SetSessionActive(false);

            // Notify mod-level that network integration got closed
            OnConnectionClosed();

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

        net->RegisterRPC<Shared::RPC::EmitScriptEvent>(&OnEmitScriptEvent);

        // Chat lines from the server are forwarded to the mod's UI via the received callback.
        net->RegisterRPC<Framework::Networking::RPC::ChatMessage>([this](const Framework::Networking::RPC::ChatMessage &payload, MafiaNet::Packet *) {
            DispatchReceivedChat(payload);
        });

        // The server's voice ranges, so the mixer fades a talker out where the frames stop.
        net->RegisterRPC<Framework::Networking::RPC::VoiceSettings>([this](const Framework::Networking::RPC::VoiceSettings &payload, MafiaNet::Packet *) {
            _voiceClient.SetDefaultSpeakerRange(payload.proximityRange);
        });

        net->RegisterRPC<Framework::Networking::RPC::VoiceSpeakerRange>([this](const Framework::Networking::RPC::VoiceSpeakerRange &payload, MafiaNet::Packet *) {
            _voiceClient.SetSpeakerRange(payload.player, payload.range);
        });

        // Scripted nametag state for our own avatar; our next upstream update carries it to the others.
        net->RegisterRPC<Framework::Networking::RPC::SetNametagState>([](const Framework::Networking::RPC::SetNametagState &payload, MafiaNet::Packet *) {
            auto *replication = CoreModules::GetReplication();
            auto *entity      = replication ? replication->GetEntityByNetworkID(payload.networkId) : nullptr;
            if (!entity || !entity->IsOwner()) {
                return;
            }
            auto *nametag = entity->GetNametag();
            if (!nametag) {
                return;
            }
            nametag->components = payload.components;
            nametag->color      = payload.color;
            nametag->text       = payload.text;
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

    void Instance::SetConnectionPhase(ConnectionPhase phase) {
        if (_connectionPhase == phase) {
            return;
        }
        _connectionPhase = phase;
        OnConnectionPhaseChanged(phase);
    }

    Utils::Result<void, Error> Instance::ConnectToServer(const std::string &host, int32_t port, const std::string &password) {
        auto result = _networkingEngine->Connect(host, port, password);
        SetConnectionPhase(result ? ConnectionPhase::Connecting : ConnectionPhase::Disconnected);
        return result;
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
        const auto cacheDir   = fmt::format("{}\\MafiaHubIntegration\\servers\\{}", appDataPath, _currentState.serverIDHash);

        // Let the system know where our scripts are stored
        SetAssetCachePath(cacheDir);
        streamer->SetApplicationDirectory(cacheDir.c_str());
        auto cacheDirHandle = cppfs::fs::open(cacheDir);

        // Ensure we stop existing downloads since the server has pushed new changes already
        // (also before the bail below, so a failed cache dir doesn't leave a transfer running)
        if (_downloadStatus.downloading) {
            net->GetFileListTransfer()->CancelReceive(_downloadStatus.setID);
            _downloadStatus = {};
        }

        if (!cacheDirHandle.exists()) {
            InitCacheAssetFolders();
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

        StartAssetDownload();
    }

    void Instance::StartAssetDownload() {
        const auto net      = GetNetworkingEngine()->GetNetworkClient();
        const auto streamer = net->GetAssetStreamer();

        if (_downloadStatus.downloading) {
            net->GetFileListTransfer()->CancelReceive(_downloadStatus.setID);
            _downloadStatus = {};
        }

        _downloadStatus.downloading = true;
        _downloadStatus.setID = streamer->DownloadFromSubdirectory(nullptr, nullptr, true, net->GetPeer()->GetSystemAddressFromIndex(0), &_assetDownloadProgress, MafiaNet::Priority::High, kAssetDownloadOrderingChannel, nullptr);
    }

    void Instance::SyncResourceUpdatesFromServer() {
        const auto net = GetNetworkingEngine()->GetNetworkClient();
        if (net->GetConnectionState() != Framework::Networking::PeerState::CONNECTED) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Cannot re-sync resources while not connected");
            _pendingRefreshResources.clear();
            return;
        }

        // Unlike DownloadsAssetsFromConnectedServer, does NOT stop all resources
        // or tear down web views; cache path is already set from connect.
        const auto streamer = net->GetAssetStreamer();
        const auto cacheDir = GetAssetCachePath();
        if (cacheDir.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("No asset cache path set; cannot re-sync resources");
            _pendingRefreshResources.clear();
            return;
        }
        streamer->SetApplicationDirectory(cacheDir.c_str());

        StartAssetDownload();
    }

    void Instance::OnAssetsDownloaded(bool success) {
        if (success && _deferredInitialAssetProcessingGeneration != 0 && !_resumingDeferredInitialAssetProcessing) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Ignoring duplicate initial asset completion while generation {} is deferred", _deferredInitialAssetProcessingGeneration);
            return;
        }

        const auto net = GetNetworkingEngine()->GetNetworkClient();
        if (success) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("All the assets have been downloaded!");

            auto scriptingModule = GetScriptingModule();

            // Hot-reload path: module already running, refresh just the flagged
            // resources instead of re-initializing everything.
            if (scriptingModule && scriptingModule->GetScriptingEngine()
                && scriptingModule->GetScriptingEngine()->IsInitialized()
                && !_pendingRefreshResources.empty()) {
                if (auto *rm = scriptingModule->GetResourceManager()) {
                    for (const auto &res : _pendingRefreshResources) {
                        // Newly started server-side: discover from cache first.
                        if (!rm->HasResource(res.name)) {
                            const std::string resPath = GetAssetCachePath() + "/" + res.name;
                            if (!rm->DiscoverResource(resPath)) {
                                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Could not discover new client resource '{}' at {}", res.name, resPath);
                                continue;
                            }
                        }
                        // Reload if running, start if newly discovered/stopped.
                        auto result = rm->IsResourceRunning(res.name) ? rm->RefreshResource(res.name) : rm->StartResource(res.name);
                        if (!result) {
                            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Failed to sync client resource '{}': {}", res.name, result.GetError());
                        }
                    }
                }
                _pendingRefreshResources.clear();
                // Run the shared completion cleanup, skipping only the
                // first-connect spawn barrier below.
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->flush();
                _downloadStatus = {};
                OnAssetsDownloadFinished(success);
                return;
            }
            // A refresh that raced an initial connect falls through to full init.
            _pendingRefreshResources.clear();

            if (!_resumingDeferredInitialAssetProcessing) {
                const uint64_t generation = _assetProcessingGeneration;
                if (OnInitialAssetDownloadReady(generation, _downloadStatus) == InitialAssetProcessingDecision::Defer) {
                    if (generation != _assetProcessingGeneration || net->GetConnectionState() != Framework::Networking::PeerState::CONNECTED) {
                        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Initial asset processing generation {} requested deferral after the connection became stale", generation);
                        return;
                    }
                    _deferredInitialAssetProcessingGeneration = generation;
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Initial asset processing deferred for generation {} before scripting startup", generation);
                    return;
                }
            }

            SetConnectionPhase(ConnectionPhase::Starting);

            if (scriptingModule) {
                // Set resource cache path before init
                scriptingModule->SetResourceCachePath(GetAssetCachePath());

                RegisterResourceSchemeHandler();

                // Initialize the scripting module with builtin registration callback
                const auto sdkCallback = [this](Framework::Scripting::Engine *engine) {
                    this->RegisterScriptingBuiltins(engine);
                };

                if (scriptingModule->Init(sdkCallback) != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Client scripting engine failed to initialize");
                    (void)net->Disconnect();
                    return;
                }
                CoreModules::SetScriptingModule(scriptingModule);

                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Client scripting engine initialized");

                PostScriptInit();

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
            _deferredInitialAssetProcessingGeneration = 0;
            _resumingDeferredInitialAssetProcessing    = false;
            (void)net->Disconnect();
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

            // Launcher-set when the game was located through Steam; Win32 read, the CRT's getenv
            // copy predates it.
            char steamId[32] = {};
#ifdef _WIN32
            GetEnvironmentVariableA("MafiaHubSteamId", steamId, sizeof(steamId));
#endif

            Framework::Networking::RPC::ClientIdentity identity;
            identity.name       = _currentState.nickname;
            identity.steamId    = steamId;
            identity.discordId  = _presence ? _presence->GetUserId() : "";
            identity.hardwareId = Framework::Utils::GetHardwareId();
            net->SendRPC(identity, serverGuid);

            net->GetReadyEvent()->SetEvent(_readyEventId, false);
            net->GetReadyEvent()->AddToWaitList(_readyEventId, serverGuid);
            _spawnBarrierArmed = true;
            _projectSpawnReady = !RequiresExplicitConnectionSpawnReady();
            TrySignalConnectionSpawnReady();
        }

        _downloadStatus = {};

        // Let the mod-level know assets have just been finished processing
        OnAssetsDownloadFinished(success);
    }

    bool Instance::CompleteDeferredInitialAssetProcessing(uint64_t generation, bool success) {
        if (generation == 0 || _deferredInitialAssetProcessingGeneration != generation || _assetProcessingGeneration != generation) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Ignoring stale deferred initial asset completion for generation {} (current {}, deferred {})", generation, _assetProcessingGeneration, _deferredInitialAssetProcessingGeneration);
            return false;
        }

        Framework::Networking::NetworkClient *net = GetNetworkingEngine() ? GetNetworkingEngine()->GetNetworkClient() : nullptr;
        if (success && (!net || net->GetConnectionState() != Framework::Networking::PeerState::CONNECTED)) {
            _deferredInitialAssetProcessingGeneration = 0;
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Cannot resume deferred initial asset processing for generation {} after the connection closed", generation);
            return false;
        }

        _deferredInitialAssetProcessingGeneration = 0;
        _resumingDeferredInitialAssetProcessing    = true;
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("{} deferred initial asset processing for generation {}", success ? "Resuming" : "Failing", generation);
        OnAssetsDownloaded(success);
        _resumingDeferredInitialAssetProcessing = false;
        return true;
    }

    void Instance::SignalConnectionSpawnReady() {
        _projectSpawnReady = true;
        TrySignalConnectionSpawnReady();
    }

    void Instance::TrySignalConnectionSpawnReady() {
        if (!_spawnBarrierArmed || !_projectSpawnReady || _connectionFinalized || !_networkingEngine) {
            return;
        }

        Framework::Networking::NetworkClient *net = _networkingEngine->GetNetworkClient();
        if (!net || !net->IsInitialReplicationDownloadComplete()) {
            return;
        }

        _spawnBarrierArmed = false;
        net->GetReadyEvent()->SetEvent(_readyEventId, true);
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Initial replication download and project spawn are ready (event {})", _readyEventId);
    }

    void Instance::RegisterResourceSchemeHandler() {
        // Not gated on the manager being initialized: the registry exists from
        // its constructor, so a root claimed before Manager::Init is in place by
        // the time the first request arrives.
        if (!_webManager || GetAssetCachePath().empty()) {
            return;
        }

        // Internal origin for scripted web views: fw://resources/<resource>/<file>
        // comes from the per-server asset cache, so a resource can ship its own
        // pages. Reconnecting elsewhere moves that cache under the same origin.
        if (!_resourceProvider) {
            _resourceProvider = std::make_shared<Framework::GUI::Resources::DirectoryProvider>(GetAssetCachePath());
        }
        else {
            _resourceProvider->SetRoot(GetAssetCachePath());
        }

        if (_resourceSchemeRegistered) {
            return;
        }
        _resourceSchemeRegistered = true;

        _webManager->RegisterResourceRoot("resources", _resourceProvider);
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Serving {}://resources from the asset cache", Framework::GUI::Resources::kResourceScheme);
    }

    void Instance::RegisterScriptingBuiltins(Framework::Scripting::Engine *engine) {
        // JavaScript bindings are registered by ClientScriptingModule::RegisterFrameworkBindings
        // This method is called to allow mod-specific customization
        ModuleRegister(engine);
    }
} // namespace Framework::Integrations::Client
