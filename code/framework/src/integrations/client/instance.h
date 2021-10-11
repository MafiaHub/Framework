/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <external/discord/wrapper.h>
#include <graphics/renderer.h>

namespace Framework::Integrations::Client {
    struct InstanceOptions {
        int64_t discordAppId = 0;
        bool usePresence   = true;
        bool useRenderer   = true;
        bool useNetworking = true;

        Graphics::RendererConfiguration rendererOptions = {};
    };

    class Instance {
      private:
        bool _initialized = false;
        InstanceOptions _opts;

        External::Discord::Wrapper *_presence;
        Graphics::Renderer *_renderer;

      public:
        Instance();
        ~Instance();

        ClientError Init(InstanceOptions &);
        ClientError Shutdown();

        void Render();
        void Update();

        bool IsInitialized() const {
            return _initialized;
        }

        External::Discord::Wrapper *GetPresence() const {
            return _presence;
        }

        Graphics::Renderer *GetRenderer() const {
            return _renderer;
        }
    };
} // namespace Framework::Integrations::Client