/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "instance.h"

#include "../shared/modules/mod.hpp"

#include <networking/messages/client_handshake.h>
#include <networking/messages/client_connection_finalized.h>

#include <logging/logger.h>

namespace Framework::Integrations::Client {
    Instance::Instance() {
        _networkingEngine = std::make_unique<Networking::Engine>();
        _presence    = std::make_unique<External::Discord::Wrapper>();
        _imguiApp    = std::make_unique<External::ImGUI::Wrapper>();
        _renderer    = std::make_unique<Graphics::Renderer>();
        _worldEngine = std::make_unique<World::ClientEngine>();
        _renderIO    = std::make_unique<Graphics::RenderIO>();
    }

    ClientError Instance::Init(InstanceOptions &opts) {
        _opts = opts;

        if (opts.usePresence) {
            if (_presence && opts.discordAppId > 0) {
                _presence->Init(opts.discordAppId);
            }
        }

        if (opts.useRenderer) {
            if (_renderer) {
                _renderer->Init(opts.rendererOptions);
            }
        }

        if (_networkingEngine) {
            _networkingEngine->Init();
        }

        if (_worldEngine) {
            _worldEngine->Init();

            _worldEngine->GetWorld()->import<Shared::Modules::Mod>();
        }

        InitNetworkingMessages();
        PostInit();

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Initialize success");
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

        return ClientError::CLIENT_NONE;
    }

    void Instance::Update() {
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
        if (_renderer && _renderer->IsInitialized()) {
            _renderer->Update();
        }

        if (_renderIO) {
            _renderIO->UpdateRenderThread();
        }
    }

    void Instance::InitNetworkingMessages() {
        _networkingEngine->GetNetworkClient()->SetOnPlayerConnectedCallback([this](SLNet::Packet *packet) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Connection accepted by server, sending handshake");

            Framework::Networking::Messages::ClientHandshake msg;
            msg.FromParameters(_currentState._nickname, "MY_SUPER_ID_1", "MY_SUPER_ID_2");

            _networkingEngine->GetNetworkClient()->Send(msg, SLNet::UNASSIGNED_RAKNET_GUID);
        });
        _networkingEngine->GetNetworkClient()->RegisterMessage<Framework::Networking::Messages::ClientConnectionFinalized>(Framework::Networking::Messages::GameMessages::GAME_CONNECTION_FINALIZED, [this](SLNet::RakNetGUID guid, Framework::Networking::Messages::ClientConnectionFinalized *msg) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Connection request finalized");

            // Notify mod-level that network integration whole process succeeded
            if (_onConnectionFinalized) {
                _onConnectionFinalized();
            }
        });
        _networkingEngine->GetNetworkClient()->SetOnPlayerDisconnectedCallback([this](SLNet::Packet *packet, uint32_t reasonId) {
            // Notify mod-level that network integration got closed
            if (_onConnectionClosed) {
                _onConnectionClosed();
            }
        });
    }
} // namespace Framework::Integrations::Client
