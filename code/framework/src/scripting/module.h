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
#include "engines/resource.h"
#include "errors.h"

// TODO find a better way to invoke resource events globally
#include "engines/node/resource.h"

namespace Framework::Scripting {
    class Module {
      private:
        std::map<std::string, Engines::IResource *> _resources;

        int _processArgsCount;
        char **_processArgs;

        Engines::IEngine *_engine = nullptr;
        EngineTypes _engineType;

      public:
        Module()  = default;
        ~Module() = default;

        ModuleError Init(EngineTypes, Engines::SDKRegisterCallback);
        ModuleError Shutdown();

        void Update();

        void LoadAllResources();
        void UnloadAllResources();
        bool LoadResource(std::string);
        bool UnloadResource(std::string);

        template <typename... Args>
        void InvokeEvent(const std::string name, Args &&...args) {
            for (auto &resource : _resources) {
                switch (_engineType) {
                case EngineTypes::ENGINE_NODE: {
                    auto *nodeResource = static_cast<Engines::Node::Resource *>(resource.second);
                    nodeResource->InvokeEvent(name, std::forward<Args>(args)...);
                    break;
                }
                default: break;
                }
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
    };
} // namespace Framework::Scripting
