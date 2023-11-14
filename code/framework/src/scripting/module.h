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
#include "engines/node/engine.h"
#include "errors.h"

namespace Framework::Scripting {
    class Module {
      private:
        int _processArgsCount;
        char **_processArgs;
        std::string _modName;

        Engines::IEngine *_engine = nullptr;
        EngineTypes _engineType;

      public:
        Module()  = default;
        ~Module() = default;

        ModuleError Init(EngineTypes, Engines::SDKRegisterCallback);
        ModuleError Shutdown();

        void Update();

        bool LoadGamemode();
        bool UnloadGamemode();

        template <typename... Args>
        void InvokeEvent(const std::string name, Args &&...args) {
            if(!_engine){
                return;
            }

            switch(_engineType){
                case EngineTypes::ENGINE_NODE: {
                    reinterpret_cast<Engines::Node::Engine*>(_engine)->InvokeEvent(name, std::forward<Args>(args)...);
                } break;
            }
        }

        Engines::IEngine *GetEngine() const {
            return _engine;
        }

        EngineTypes GetEngineType() const {
            return _engineType;
        }

        void SetProcessArguments(int argc, char **argv) {
            _processArgsCount = argc;
            _processArgs      = argv;
        }

        void SetModName(std::string name) {
            _modName = name;
        }
    };
} // namespace Framework::Scripting
