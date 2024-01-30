/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <cppfs/FileHandle.h>
#include <cppfs/FileIterator.h>
#include <cppfs/fs.h>
#include <logging/logger.h>
#include <regex>

#include "engines/node/engine.h"
#include "module.h"

#include "core_modules.h"

namespace Framework::Scripting {
    ModuleError Module::Init(EngineTypes engineType, Engines::SDKRegisterCallback cb) {
        // Initialize the engine based on the desired type
        switch (engineType) {
        case ENGINE_NODE: {
            _engine = new Engines::Node::Engine;
        } break;

        case ENGINE_LUA:
            break;

        case ENGINE_SQUIRREL:
            break;

        default:
            break;
        }
        _engine->SetModName(_modName);

        _engineType = engineType;
        _engine->SetProcessArguments(_processArgsCount, _processArgs);
        if (_engine->Init(cb) != EngineError::ENGINE_NONE) {
            delete _engine;
            return ModuleError::MODULE_ENGINE_INIT_FAILED;
        }

        // Everything just went fine hihi
        CoreModules::SetScriptingModule(this);

        return ModuleError::MODULE_NONE;
    }

    ModuleError Module::Shutdown() {
        if (!_engine) {
            return ModuleError::MODULE_ENGINE_NULL;
        }

        // Unload the gamemode if it's loaded, it can fail but it's not critical since we are shutdowning
        // So we just log out, then it's obvious for everyone
        if (!UnloadGamemode()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to unload the gamemode");
        }

        // Shutdown the engine
        _engine->Shutdown();
        CoreModules::SetScriptingModule(nullptr);

        return ModuleError::MODULE_NONE;
    }

    void Module::Update() {
        if (!_engine) {
            return;
        }

        _engine->Update();
    }

    bool Module::LoadGamemode() {
        cppfs::FileHandle dir = cppfs::fs::open("gamemode");
        if (!dir.exists() || !dir.isDirectory()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to find the gamemode directory");
            return false;
        }

        return _engine->PreloadGamemode("gamemode");
    }

    bool Module::UnloadGamemode() {
        return _engine->UnloadGamemode("gamemode");
    }
} // namespace Framework::Scripting
