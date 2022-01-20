/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <external/discord/wrapper.h>
#include <external/imgui/wrapper.h>
#include <functional>
#include <graphics/renderer.h>
#include <graphics/renderio.h>

#include "networking/engine.h"

#include <functional>
#include <memory>
#include <world/client.h>

#include "../shared/types/player.hpp"
#include "../shared/types/streaming.hpp"

#include <flecs/flecs.h>

namespace Framework::Integrations::Client {
    using NetworkConnectionFinalizedCallback = std::function<void(flecs::entity)>;
    using NetworkConnectionClosedCallback = std::function<void()>;

    struct InstanceOptions {
        int64_t discordAppId = 0;
        bool usePresence     = true;
        bool useRenderer     = true;
        bool useNetworking   = true;
        bool useImGUI        = false;

        Graphics::RendererConfiguration rendererOptions = {};
    };

    struct CurrentState {
        std::string _host;
        int32_t _port;
        std::string _nickname;
    };

    class Instance {
      private:
        bool _initialized = false;
        InstanceOptions _opts;

        std::unique_ptr<Networking::Engine> _networkingEngine;
        std::unique_ptr<External::Discord::Wrapper> _presence;
        std::unique_ptr<Graphics::Renderer> _renderer;
        std::unique_ptr<World::ClientEngine> _worldEngine;
        std::unique_ptr<Graphics::RenderIO> _renderIO;

        // gui
        std::unique_ptr<External::ImGUI::Wrapper> _imguiApp;

        // Local state tracking
        CurrentState _currentState;
        NetworkConnectionFinalizedCallback _onConnectionFinalized;
        NetworkConnectionClosedCallback _onConnectionClosed;

        // Entity factories
        std::unique_ptr<Shared::Archetypes::PlayerFactory> _playerFactory;
        std::unique_ptr<Shared::Archetypes::StreamingFactory> _streamingFactory;

        void InitManagers();
        void InitNetworkingMessages();
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

        bool IsInitialized() const {
            return _initialized;
        }

        CurrentState GetCurrentState() const {
            return _currentState;
        }

        void SetCurrentState(CurrentState state) {
            _currentState = state;
        }

        void SetOnConnectionFinalizedCallback(NetworkConnectionFinalizedCallback cb) {
            _onConnectionFinalized = cb;
        }

        void SetOnConnectionClosedCallback(NetworkConnectionClosedCallback cb) {
            _onConnectionClosed = cb;
        }

        Networking::Engine* GetNetworkingEngine() const {
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

        Graphics::Renderer *GetRenderer() const {
            return _renderer.get();
        }

        Graphics::RenderIO* GetRenderIO() const {
            return _renderIO.get();
        }

        Shared::Archetypes::PlayerFactory* GetPlayerFactory() const {
            return _playerFactory.get();
        }

        Shared::Archetypes::StreamingFactory* GetStreamingFactory() const {
            return _streamingFactory.get();
        }
    };
} // namespace Framework::Integrations::Client
