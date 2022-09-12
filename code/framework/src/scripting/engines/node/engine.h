/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <node.h>
#include <v8.h>

#include "../engine.h"
#include "resource.h"

namespace Framework::Scripting::Engines::Node {
    class Engine: public IEngine {
      private:
        v8::Isolate *_isolate;
        std::unique_ptr<node::MultiIsolatePlatform> _platform;

        int _processArgsCount;
        char **_processArgs;

      public:
        EngineError Init() override;
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

        void SetProcessArguments(int argc, char **argv) override {
            _processArgsCount = argc;
            _processArgs      = argv;
        }
    };
} // namespace Framework::Scripting::Engines::Node