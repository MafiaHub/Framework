/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "init.h"
#include "resource_manager.h"

#include <node.h>
#include <string>
#include <uv.h>
#include <v8.h>
#include <vector>

namespace Framework::Scripting {
    class Engine {
      private:
        v8::Isolate *_isolate = nullptr;
        v8::Persistent<v8::ObjectTemplate> _globalObjectTemplate;
        std::unique_ptr<node::MultiIsolatePlatform> _platform;

        ResourceManager *_resourceManager = nullptr;

      public:
        Engine() = default;

        ~Engine() = default;

        EngineError Init(SDKRegisterCallback = nullptr);

        EngineError Shutdown();

        void Update();

        v8::Isolate *GetIsolate() const {
            return _isolate;
        }

        node::MultiIsolatePlatform *GetPlatform() const {
            return _platform.get();
        }

        v8::Persistent<v8::ObjectTemplate> &GetObjectTemplate() {
            return _globalObjectTemplate;
        }

        ResourceManager *GetResourceManager() const {
            return _resourceManager;
        }
    };
} // namespace Framework::Scripting
