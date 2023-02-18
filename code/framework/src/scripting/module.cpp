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

        case ENGINE_LUA: break;

        case ENGINE_SQUIRREL: break;

        default: break;
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

        if (!_resources.empty()) {
            UnloadAllResources();
        }

        _engine->Shutdown();

        CoreModules::SetScriptingModule(nullptr);

        return ModuleError::MODULE_NONE;
    }

    void Module::Update() {
        if (!_engine) {
            return;
        }

        // Update the engine (no way?)
        _engine->Update();

        // Manually tick the resources since the engine is not handling that very specific part (poor arch design)
        for (auto res : _resources) {
            res.second->Update(true);
        }
    }

    void Module::LoadAllResources() {
        cppfs::FileHandle dir = cppfs::fs::open("resources");
        if (!dir.exists() || !dir.isDirectory()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to find the resources directory");
            return;
        }

        for (auto it = dir.begin(); it != dir.end(); ++it) {
            LoadResource(*it);
        }
    }

    void Module::UnloadAllResources() {
        cppfs::FileHandle dir = cppfs::fs::open("resources");
        if (!dir.exists() || !dir.isDirectory()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to find the resources directory");
            return;
        }

        for (auto it = dir.begin(); it != dir.end(); ++it) {
            UnloadResource(*it);
        }
    }

    bool Module::LoadResource(std::string name) {
        // Make sure the package has an acceptable name
        std::regex exp("^[A-z_0-9]{0,}$");
        if (!std::regex_match(name, exp)) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to load package {} due to invalid name", name);
            return false;
        }

        // Is the package already present?
        if (_resources.find(name) != _resources.end()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Package {} already loaded", name);
            return false;
        }

        std::string fullPath("resources/" + name);
        Engines::IResource *res = _engine->LoadResource(fullPath);
        if (!res) {
            return false;
        }

        res->Preload();

        // If loading failed, just free everything and return
        if (!res->IsLoaded()) {
            delete res;
            return false;
        }

        res->SetModule(this);

        _resources[name] = res;
        return true;
    }

    bool Module::UnloadResource(std::string name) {
        // Is the package event present?
        if (_resources.find(name) == _resources.end()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Package {} not loaded", name);
            return false;
        }

        // Acquire the instance and call the unloader
        Engines::IResource *res = _resources[name];
        if (res->IsLoaded()) {
            res->Shutdown();
        }

        // Free em
        delete res;
        _resources.erase(name);
        return true;
    }
} // namespace Framework::Scripting
