/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <atomic>

#include <node.h>
#include <v8.h>

#include "../callback.h"
#include "../engine.h"
#include "resource.h"
#include "sdk.h"

namespace Framework::Scripting::Engines::Node {
    class Engine: public IEngine {
      private:
        SDK *_sdk = nullptr;
        v8::Isolate *_isolate;
        v8::Persistent<v8::ObjectTemplate> _globalObjectTemplate;
        std::unique_ptr<node::MultiIsolatePlatform> _platform;
        std::string _modName;
        std::atomic<bool> _isShuttingDown = false;

      public:
        EngineError Init(SDKRegisterCallback) override;
        EngineError Shutdown() override;
        void Update() override;

        Resource *LoadResource(std::string) override;
        Resource *UnloadResource(std::string) override;

        v8::Isolate *GetIsolate() const {
            return _isolate;
        }

        node::MultiIsolatePlatform *GetPlatform() const {
            return _platform.get();
        }

        v8::Local<v8::ObjectTemplate> GetGlobalObjectTemplate() const {
            return _globalObjectTemplate.Get(_isolate);
        }

        void SetProcessArguments(int argc, char **argv) override {}

        void SetModName(std::string name) override {
            _modName = name;
        }

        std::string GetModName() const {
            return _modName;
        }

      protected:
        SDK *GetSDK() const {
            return _sdk;
        }
    };
} // namespace Framework::Scripting::Engines::Node
