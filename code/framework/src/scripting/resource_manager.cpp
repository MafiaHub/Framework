/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "resource_manager.h"

#include "engine.h"

#include <cppfs/FileHandle.h>
#include <cppfs/FileIterator.h>
#include <cppfs/fs.h>
#include <logging/logger.h>
#include <regex>

namespace Framework::Scripting {
    ResourceManager::ResourceManager(Engine *engine): _engine(engine) {}

    ResourceManager::~ResourceManager() {
        if (!_resources.empty()) {
            UnloadAll();
        }
    }

    ResourceManagerError ResourceManager::Load(std::string name, SDKRegisterCallback cb) {
        // Make sure the package has an acceptable name
        std::regex exp("^[A-z_0-9]{0,}$");
        if (!std::regex_match(name, exp)) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to load package {} due to invalid name", name);
            return ResourceManagerError::RESOURCE_NAME_INVALID;
        }

        // Is the package already present?
        if (_resources.find(name) != _resources.end()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Package {} already loaded", name);
            return ResourceManagerError::RESOURCE_ALREADY_LOADED;
        }

        std::string fullPath("resources/" + name);
        Resource *res = new Resource(_engine, fullPath, cb);

        // If loading failed, just free everything and return
        if (!res->IsLoaded()) {
            delete res;
            return ResourceManagerError::RESOURCE_LOADING_FAILED;
        }

        _resources[name] = res;
        return ResourceManagerError::RESOURCE_MANAGER_NONE;
    }

    ResourceManagerError ResourceManager::Unload(std::string name) {
        // Is the package event present?
        if (_resources.find(name) == _resources.end()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Package {} not loaded", name);
            return ResourceManagerError::RESOURCE_NOT_LOADED;
        }

        // Acquire the instance and call the unloader
        Resource *res = _resources[name];
        if (res->IsLoaded()) {
            res->Shutdown();
        }

        // Free em
        delete res;
        _resources.erase(name);
        return ResourceManagerError::RESOURCE_MANAGER_NONE;
    }

    void ResourceManager::LoadAll(SDKRegisterCallback cb) {
        cppfs::FileHandle dir = cppfs::fs::open("resources");
        if (!dir.exists() || !dir.isDirectory()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to find the resources directory");
            return;
        }

        for (auto it = dir.begin(); it != dir.end(); ++it) { Load(*it, cb); }
    }

    void ResourceManager::UnloadAll() {
        cppfs::FileHandle dir = cppfs::fs::open("resources");
        if (!dir.exists() || !dir.isDirectory()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to find the resources directory");
            return;
        }

        for (auto it = dir.begin(); it != dir.end(); ++it) { Unload(*it); }
    }

    void ResourceManager::InvokeEvent(const std::string &eventName, InvokeEventCallback cb) {
        auto isolate = _engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);

        for (auto resPair : _resources) {
            const auto res = resPair.second;

            auto ctx = res->GetContext();
            v8::Context::Scope contextScope(ctx);

            auto args = cb(isolate, ctx);
            res->InvokeEvent(eventName, args);
        }
    }

    void ResourceManager::InvokeErrorEvent(const std::string &error, const std::string &stackTrace, const std::string &file, int32_t line) {
        for (auto res : _resources) { res.second->InvokeErrorEvent(error, stackTrace, file, line); }
    }
} // namespace Framework::Scripting
