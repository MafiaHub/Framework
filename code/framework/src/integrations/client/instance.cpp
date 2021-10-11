/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "instance.h"

#include <logging/logger.h>

namespace Framework::Integrations::Client {
    Instance::Instance() {
        _presence    = std::make_unique<External::Discord::Wrapper>();
        _renderer    = std::make_unique<Graphics::Renderer>();
        _worldEngine = std::make_unique<World::Engine>();
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

        Framework::Logging::GetLogger("Application")->debug("Initialize success");
        _initialized = true;
        return ClientError::CLIENT_NONE;
    }

    ClientError Instance::Shutdown() {
        if (_renderer && _renderer->IsInitialized()) {
            _renderer->Shutdown();
        }

        if (_presence && _presence->IsInitialized()) {
            _presence->Shutdown();
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

        if (_worldEngine) {
            _worldEngine->Update();
        }
    }

    void Instance::Render() {
        if (_renderer && _renderer->IsInitialized()) {
            _renderer->Update();
        }
    }
} // namespace Framework::Integrations::Client
