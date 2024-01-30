/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <map>
#include <string>

#include "engine_kind.h"
#include "engines/callback.h"
#include "engines/engine.h"
#include "errors.h"

namespace Framework::Scripting {
    class Module {
      private:
        int _processArgsCount = 0;
        char **_processArgs = nullptr;
        std::string _modName;

        Engines::IEngine *_engine = nullptr;
        EngineTypes _engineType = EngineTypes::ENGINE_NODE;

      public:
        Module() = default;
        ~Module() = default;

        ModuleError Init(EngineTypes, Engines::SDKRegisterCallback);
        ModuleError Shutdown();

        void Update();

        bool LoadGamemode();
        bool UnloadGamemode();

        Engines::IEngine *GetEngine() const {
            return _engine;
        }

        EngineTypes GetEngineType() const {
            return _engineType;
        }

        void SetProcessArguments(int argc, char **argv) {
            _processArgsCount = argc;
            _processArgs = argv;
        }

        void SetModName(std::string name) {
            _modName = name;
        }
    };
} // namespace Framework::Scripting
