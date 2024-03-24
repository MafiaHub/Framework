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

#include "module.h"

#include "core_modules.h"

namespace Framework::Scripting {
    ModuleError Module::InitClientEngine(SDKRegisterCallback cb) {
        // TODO: implement
        return ModuleError::MODULE_NONE;
    }

    ModuleError Module::InitServerEngine(SDKRegisterCallback cb) {
        // Validate the presence of the gamemode and the gamemode server sub folder
        const cppfs::FileHandle serverFolder = cppfs::fs::open("gamemode");
        if (!serverFolder.exists() || !serverFolder.isFile()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The gamemode path is not present or is not a folder");
            return ModuleError::MODULE_MISSING_GAMEMODE;
        }

        _serverEngine = std::make_unique<ServerEngine>();
        _serverEngine->SetModName(_modName);
        _serverEngine->SetProcessArguments(_processArgsCount, _processArgs);

        // Init should return an error if the engine failed to initialize
        if (_serverEngine->Init(cb) != EngineError::ENGINE_NONE) {
            _serverEngine.reset();
            return ModuleError::MODULE_ENGINE_INIT_FAILED;
        }

        CoreModules::SetScriptingModule(this);
        return ModuleError::MODULE_NONE;
    }

    ModuleError Module::Shutdown() {
        if(_clientEngine.get() != nullptr) {
            _clientEngine->Shutdown();
            _clientEngine.reset();
        }

        if(_serverEngine.get() != nullptr) {
            _serverEngine->Shutdown();
            _serverEngine.reset();
        }

        CoreModules::SetScriptingModule(nullptr);
        return ModuleError::MODULE_NONE;
    }

    void Module::Update() const {
        if(_clientEngine.get() != nullptr){
            _clientEngine->Update();
        }

        if(_serverEngine.get() != nullptr){
            _serverEngine->Update();
        }
    }
} // namespace Framework::Scripting
