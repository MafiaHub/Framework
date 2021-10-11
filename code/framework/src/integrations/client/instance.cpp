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
        _presence = new External::Discord::Wrapper;
        _renderer = new Graphics::Renderer;
    }

    Instance::~Instance() {
        delete _presence;
        delete _renderer;
    }

    ClientError Instance::Init(InstanceOptions &opts) {
        _opts = opts;

        if (opts.usePresence) {
            if (_presence) {
                _presence->Init(763114144454672444);
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
        if (_renderer) {
            _renderer->Shutdown();
        }

        if (_presence) {
            _presence->Shutdown();
        }

        return ClientError::CLIENT_NONE;
    }

    void Instance::Update() {
        if (_presence) {
            _presence->Update();
        }
    }

    void Instance::Render() {
        if (_renderer) {
            _renderer->Update();
        }
    }
} // namespace Framework::Integrations::Client
