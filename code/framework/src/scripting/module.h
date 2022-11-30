/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <map>
#include <string>

#include "engines/engine.h"
#include "engines/resource.h"
#include "engines/callback.h"
#include "errors.h"

namespace Framework::Scripting {
    enum EngineTypes { ENGINE_NODE, ENGINE_LUA, ENGINE_SQUIRREL };

    class Module {
      private:
        std::map<std::string, Engines::IResource *> _resources;

        int _processArgsCount;
        char **_processArgs;

        Engines::IEngine *_engine = nullptr;
        EngineTypes _engineType;

      public:
        Module() = default;

        ~Module() = default;

        ModuleError Init(EngineTypes, Engines::SDKRegisterCallback);
        ModuleError Shutdown();

        void Update();

        void LoadAllResources();
        void UnloadAllResources();
        bool LoadResource(std::string);
        bool UnloadResource(std::string);

        Engines::IEngine *GetEngine() const {
            return _engine;
        }

        void SetProcessArguments(int argc, char **argv) {
            _processArgsCount = argc;
            _processArgs      = argv;
        }
    };
} // namespace Framework::Scripting