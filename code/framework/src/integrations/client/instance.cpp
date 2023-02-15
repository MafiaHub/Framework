/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "instance.h"

#include <networking/messages/client_connection_finalized.h>
#include <networking/messages/client_handshake.h>
#include <networking/messages/client_kick.h>

#include "../shared/modules/mod.hpp"

#include <logging/logger.h>

#include "utils/version.h"

#include <optick.h>

#include "core_modules.h"

namespace Framework::Integrations::Client {
    Instance::Instance() {
        OPTICK_START_CAPTURE();
        _networkingEngine = std::make_unique<Networking::Engine>();
        _presence         = std::make_unique<External::Discord::Wrapper>();
        _imguiApp         = std::make_unique<External::ImGUI::Wrapper>();
        _renderer         = std::make_unique<Graphics::Renderer>();
        _worldEngine      = std::make_unique<World::ClientEngine>();
        _renderIO         = std::make_unique<Graphics::RenderIO>();
        _playerFactory    = std::make_unique<World::Archetypes::PlayerFactory>();
        _streamingFactory = std::make_unique<World::Archetypes::StreamingFactory>();
    }

    Instance::~Instance() {
        OPTICK_STOP_CAPTURE();
    }

    ClientError Instance::Init(InstanceOptions &opts) {
        _opts = opts;

        if (opts.usePresence) {
            if (_presence && opts.discordAppId > 0) {
                if (_presence->Init(opts.discordAppId) != Framework::External::DiscordError::DISCORD_NONE) {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Discord Presence failed to initialize");
                    return ClientError::CLIENT_ENGINES_ERROR;
                }
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Discord presence initialized");
            }
        }

        if (opts.useRenderer) {
            if (_renderer) {
                if (_renderer->Init(opts.rendererOptions) != Framework::Graphics::RendererError::RENDERER_NONE) {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Renderer failed to initialize");
                    return ClientError::CLIENT_ENGINES_ERROR;
                }
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Rendering systems initialized");
            }
        }

        if (_networkingEngine) {
            if (!_networkingEngine->Init()) {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Networking engine failed to initialize");
                return ClientError::CLIENT_ENGINES_ERROR;
            }
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Networking engine initialized");
        }

        if (_worldEngine) {
            if (_worldEngine->Init() != Framework::World::EngineError::ENGINE_NONE) {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("World engine failed to initialize");
                return ClientError::CLIENT_ENGINES_ERROR;
            }

            _worldEngine->GetWorld()->import <Shared::Modules::Mod>();

            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Core ecs modules have been imported!");
        }

        InitNetworkingMessages();

        PostInit();
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Mod subsystems initialized");

        // Init the render device
        if (opts.useRenderer) {
            _renderer->SetWindow(opts.rendererOptions.windowHandle);

            switch (opts.rendererOptions.backend) {
            case Graphics::RendererBackend::BACKEND_D3D_9: _renderer->GetD3D9Backend()->Init(opts.rendererOptions.d3d9.device, nullptr); break;
            case Graphics::RendererBackend::BACKEND_D3D_11: _renderer->GetD3D11Backend()->Init(opts.rendererOptions.d3d11.device, opts.rendererOptions.d3d11.deviceContext); break;
            default: Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->info("[renderDevice] Device not implemented"); break;
            }
        }

        if (opts.useImGUI) {
            // Init the ImGui internal instance
            External::ImGUI::Config imguiConfig;
            imguiConfig.renderBackend = opts.rendererOptions.backend;
            imguiConfig.windowBackend = opts.rendererOptions.platform;
            imguiConfig.renderer      = _renderer.get();
            imguiConfig.windowHandle  = _renderer->GetWindow();
            if (_imguiApp->Init(imguiConfig) != External::ImGUI::Error::IMGUI_NONE) {
                Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->info("ImGUI has failed to init");
            }
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Client has been initialized");
        _initialized = true;
        return ClientError::CLIENT_NONE;
    }

    ClientError Instance::Shutdown() {
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

        if (_imguiApp && _imguiApp->IsInitialized()) {
            _imguiApp->Shutdown();
        }

        if (_worldEngine) {
            _worldEngine->Shutdown();
        }

        CoreModules::Reset();

        return ClientError::CLIENT_NONE;
    }

    void Instance::Update() {
        OPTICK_EVENT();
        if (_presence && _presence->IsInitialized()) {
            _presence->Update();
        }

        if (_networkingEngine) {
            _networkingEngine->Update();
        }

        if (_worldEngine) {
            _worldEngine->Update();
        }

        if (_imguiApp && _imguiApp->IsInitialized()) {
            _imguiApp->Update();
        }

        if (_renderIO) {
            _renderIO->UpdateMainThread();
        }

        PostUpdate();
    }

    void Instance::Render() {
        OPTICK_EVENT();
        if (_renderer && _renderer->IsInitialized()) {
            _renderer->Update();
        }

        if (_renderIO) {
            _renderIO->UpdateRenderThread();
        }
    }

    void Instance::InitNetworkingMessages() {
        using namespace Framework::Networking::Messages;
        const auto net = _networkingEngine->GetNetworkClient();
        net->SetOnPlayerConnectedCallback([this, net](SLNet::Packet *packet) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Connection accepted by server, sending handshake");

            ClientHandshake msg;
            msg.FromParameters(_currentState._nickname, "MY_SUPER_ID_1", "MY_SUPER_ID_2", Utils::Version::rel);

            net->Send(msg, SLNet::UNASSIGNED_RAKNET_GUID);
        });
        net->RegisterMessage<ClientConnectionFinalized>(GameMessages::GAME_CONNECTION_FINALIZED, [this, net](SLNet::RakNetGUID _guid, ClientConnectionFinalized *msg) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Connection request finalized");
            _worldEngine->OnConnect(net, msg->GetServerTickRate());
            const auto guid = GetNetworkingEngine()->GetNetworkClient()->GetPeer()->GetMyGUID();

            const auto newPlayer = GetWorldEngine()->CreateEntity(msg->GetEntityID());
            GetStreamingFactory()->SetupClient(newPlayer, guid.g);
            GetPlayerFactory()->SetupClient(newPlayer, guid.g);

            // Notify mod-level that network integration whole process succeeded
            if (_onConnectionFinalized) {
                _onConnectionFinalized(newPlayer, msg->GetServerTickRate());
            }
        });
        net->RegisterMessage<ClientKick>(GameMessages::GAME_CONNECTION_KICKED, [](SLNet::RakNetGUID guid, ClientKick *msg) {
            std::string reason = "Unknown.";

            switch (msg->GetDisconnectionReason()) {
            case Framework::Networking::Messages::DisconnectionReason::BANNED: reason = "You are banned."; break;
            case Framework::Networking::Messages::DisconnectionReason::KICKED: reason = "You have been kicked."; break;
            case Framework::Networking::Messages::DisconnectionReason::KICKED_INVALID_PACKET: reason = "You have been kicked (invalid packet)."; break;
            case Framework::Networking::Messages::DisconnectionReason::WRONG_VERSION: reason = "You have been kicked (wrong client version)."; break;
            case Framework::Networking::Messages::DisconnectionReason::INVALID_PASSWORD: reason = "You have been kicked (wrong password)."; break;
            default: break;
            }
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Connection dropped: {}", reason);
        });
        net->SetOnPlayerDisconnectedCallback([this](SLNet::Packet *packet, uint32_t reasonId) {
            _worldEngine->OnDisconnect();

            // Notify mod-level that network integration got closed
            if (_onConnectionClosed) {
                _onConnectionClosed();
            }
        });

        Framework::World::Modules::Base::SetupClientReceivers(net, _worldEngine.get(), _streamingFactory.get());

        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Game sync networking messages registered");
    }
} // namespace Framework::Integrations::Client
