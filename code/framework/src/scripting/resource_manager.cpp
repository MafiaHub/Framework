#include "resource_manager.h"
#include "engine.h"

#include <cppfs/FileHandle.h>
#include <cppfs/FileIterator.h>
#include <cppfs/fs.h>
#include <logging/logger.h>

namespace Framework::Scripting {
    ResourceManager::ResourceManager(Engine *engine): _engine(engine) {}

    ResourceManager::~ResourceManager() {
        if (!_resources.empty()) {
            UnloadAll();
        }
    }

    ResourceManagerError ResourceManager::Load(std::string name, SDKRegisterCallback cb) {
        // TODO: check for package name correctness before trying to load the resource

        // Is the package already present?
        if (_resources.find(name) != _resources.end()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Package {} already loaded", name);
            return Framework::Scripting::ResourceManagerError::RESOURCE_ALREADY_LOADED;
        }

        std::string fullPath("resources/" + name);
        Resource *res = new Resource(_engine, fullPath, cb);

        // If loading failed, just free everything and return
        if (!res->IsLoaded()) {
            delete res;
            return Framework::Scripting::ResourceManagerError::RESOURCE_LOADING_FAILED;
        }

        _resources[name] = res;
        return Framework::Scripting::ResourceManagerError::RESOURCE_MANAGER_NONE;
    }

    ResourceManagerError ResourceManager::Unload(std::string name) {
        // Is the package event present?
        if (_resources.find(name) == _resources.end()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Package {} not loaded", name);
            return Framework::Scripting::ResourceManagerError::RESOURCE_NOT_LOADED;
        }

        // Acquire the instance and call the unloader
        Resource *res = _resources[name];
        if (res->IsLoaded()) {
            res->Shutdown();
        }

        // Free em
        delete res;
        _resources.erase(name);
        return Framework::Scripting::ResourceManagerError::RESOURCE_MANAGER_NONE;
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
        for (auto resPair : _resources) { 
            const auto res = resPair.second;

            auto isolate = _engine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);

            auto ctx = res->GetContext();
            v8::Context::Scope contextScope(ctx);
            res->InvokeEvent(eventName, cb(isolate, ctx));
        }
    }

    void ResourceManager::InvokeErrorEvent(const std::string &error, const std::string &stackTrace, const std::string &file, int32_t line) {
        for (auto res : _resources) { res.second->InvokeErrorEvent(error, stackTrace, file, line); }
    }
} // namespace Framework::Scripting
