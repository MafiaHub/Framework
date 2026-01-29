#include "resource_manager.h"

#include "../builtins/events.h"

#include <algorithm>
#include <filesystem>
#include <queue>
#include <stack>

namespace Framework::Scripting {

    ResourceManager::ResourceManager(Engine *jsEngine, const ResourceManagerConfig &config)
        : _config(config)
        , _jsEngine(jsEngine) {
    }

    ResourceManager::~ResourceManager() {
        StopAll();
    }

    const ResourceManagerConfig &ResourceManager::GetConfig() const {
        return _config;
    }

    void ResourceManager::SetConfig(const ResourceManagerConfig &config) {
        _config = config;
    }

    size_t ResourceManager::DiscoverResources() {
        size_t count = 0;

        std::filesystem::path resourcesDir(_config.resourcesPath);
        if (!std::filesystem::exists(resourcesDir)) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Resources directory not found: {}", _config.resourcesPath);
            return 0;
        }

        for (const auto &entry : std::filesystem::directory_iterator(resourcesDir)) {
            if (!entry.is_directory()) {
                continue;
            }

            // Check for package.json
            auto packagePath = entry.path() / "package.json";
            if (!std::filesystem::exists(packagePath)) {
                continue;
            }

            if (DiscoverResource(entry.path().string())) {
                ++count;
            }
        }

        BuildDependencyGraph();

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Discovered {} JavaScript resources", count);
        return count;
    }

    bool ResourceManager::DiscoverResource(const std::string &path) {
        auto resource = std::make_unique<Resource>(path);

        if (!resource->IsManifestValid()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Invalid package.json in {}: {}", path, resource->GetErrorMessage());
            return false;
        }

        std::string name = resource->GetName();

        {
            std::lock_guard<std::mutex> lock(_resourcesMutex);
            if (_resources.find(name) != _resources.end()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Duplicate resource name: {}", name);
                return false;
            }
            _resources[name] = std::move(resource);
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Discovered JS resource: {} v{}", name, _resources[name]->GetVersion());
        return true;
    }

    void ResourceManager::BuildDependencyGraph() {
        std::lock_guard<std::mutex> graphLock(_graphMutex);
        std::lock_guard<std::mutex> resourceLock(_resourcesMutex);

        _dependencies.clear();
        _dependents.clear();

        for (const auto &[name, resource] : _resources) {
            const auto &deps = resource->GetManifest().GetMafiaHubConfig().resourceDependencies;
            for (const auto &dep : deps) {
                _dependencies[name].insert(dep.name);
                _dependents[dep.name].insert(name);
            }
        }
    }

    bool ResourceManager::ValidateDependencies(std::string &outError) const {
        std::lock_guard<std::mutex> graphLock(_graphMutex);
        std::lock_guard<std::mutex> resourceLock(_resourcesMutex);

        for (const auto &[name, deps] : _dependencies) {
            for (const auto &depName : deps) {
                if (_resources.find(depName) == _resources.end()) {
                    outError = "Resource '" + name + "' depends on missing resource '" + depName + "'";
                    return false;
                }
            }
        }
        return true;
    }

    std::vector<std::string> ResourceManager::ComputeLoadOrder() const {
        std::lock_guard<std::mutex> graphLock(_graphMutex);
        std::lock_guard<std::mutex> resourceLock(_resourcesMutex);

        std::vector<std::string> result;
        std::map<std::string, int> inDegree;
        std::queue<std::string> queue;

        // Initialize in-degrees
        for (const auto &[name, _] : _resources) {
            auto it = _dependencies.find(name);
            inDegree[name] = (it != _dependencies.end()) ? static_cast<int>(it->second.size()) : 0;
            if (inDegree[name] == 0) {
                queue.push(name);
            }
        }

        // Topological sort
        while (!queue.empty()) {
            std::string current = queue.front();
            queue.pop();
            result.push_back(current);

            auto depIt = _dependents.find(current);
            if (depIt != _dependents.end()) {
                for (const auto &dependent : depIt->second) {
                    --inDegree[dependent];
                    if (inDegree[dependent] == 0) {
                        queue.push(dependent);
                    }
                }
            }
        }

        // Check for cycles
        if (result.size() != _resources.size()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Dependency cycle detected in JS resources");
            return {};
        }

        return result;
    }

    ResourceOperationResult ResourceManager::StartAll() {
        std::string depError;
        if (!ValidateDependencies(depError)) {
            return ResourceOperationResult::Failure(depError);
        }

        auto loadOrder = ComputeLoadOrder();
        if (loadOrder.empty() && GetResourceCount() > 0) {
            return ResourceOperationResult::Failure("Dependency cycle detected");
        }

        std::vector<std::string> started;
        for (const auto &name : loadOrder) {
            auto result = StartResource(name);
            if (result.success) {
                started.push_back(name);
            } else {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to start resource {}: {}", name, result.error);
            }
        }

        return ResourceOperationResult::Success(started);
    }

    ResourceOperationResult ResourceManager::StopAll() {
        auto loadOrder = GetLoadOrder();
        // Stop in reverse order
        std::reverse(loadOrder.begin(), loadOrder.end());

        std::vector<std::string> stopped;
        for (const auto &name : loadOrder) {
            if (IsResourceRunning(name)) {
                auto result = StopResource(name);
                if (result.success) {
                    stopped.push_back(name);
                }
            }
        }

        return ResourceOperationResult::Success(stopped);
    }

    ResourceOperationResult ResourceManager::StartResource(const std::string &name) {
        Resource *resource = GetResourceMutable(name);
        if (!resource) {
            return ResourceOperationResult::Failure("Resource not found: " + name);
        }

        if (resource->IsRunning()) {
            return ResourceOperationResult::Success({name});
        }

        // Start dependencies first
        auto deps = GetDependencies(name);
        for (const auto &depName : deps) {
            if (!IsResourceRunning(depName)) {
                auto result = StartResource(depName);
                if (!result.success) {
                    return ResourceOperationResult::Failure("Failed to start dependency '" + depName + "': " + result.error);
                }
            }
        }

        // Transition to Loading
        if (!resource->TransitionTo(ResourceState::Loading)) {
            return ResourceOperationResult::Failure("Invalid state transition for resource: " + name);
        }

        // Execute the entry point script
        std::string error;
        if (!ExecuteResourceScript(*resource, error)) {
            resource->SetError(error);
            FireOnResourceError(name, error);
            return ResourceOperationResult::Failure(error);
        }

        // Emit resourceStart event and wait for handlers
        if (_jsEngine && _jsEngine->IsInitialized()) {
            v8::Isolate *isolate = _jsEngine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _jsEngine->GetContext();
            v8::Context::Scope contextScope(context);

            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, name));

            SetCurrentResourceContext(name);
            Events::EmitReserved(isolate, context, "resourceStart", args);
            SetCurrentResourceContext("");
        }

        // Transition to Running
        if (!resource->TransitionTo(ResourceState::Running)) {
            return ResourceOperationResult::Failure("Failed to transition to Running state");
        }

        resource->SetLoadTimestamp();
        resource->ClearRestartAttempts();

        FireOnResourceStarted(name);
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Started JS resource: {}", name);

        return ResourceOperationResult::Success({name});
    }

    ResourceOperationResult ResourceManager::StopResource(const std::string &name) {
        Resource *resource = GetResourceMutable(name);
        if (!resource) {
            return ResourceOperationResult::Failure("Resource not found: " + name);
        }

        if (!resource->IsRunning()) {
            return ResourceOperationResult::Success({});
        }

        std::vector<std::string> stopped;

        // Stop dependents first if configured
        if (_config.cascadeStopDependents) {
            auto dependents = GetDependents(name);
            for (const auto &depName : dependents) {
                if (IsResourceRunning(depName)) {
                    auto result = StopResource(depName);
                    stopped.insert(stopped.end(), result.affectedResources.begin(), result.affectedResources.end());
                }
            }
        }

        // Transition to Stopping
        resource->TransitionTo(ResourceState::Stopping);

        // Emit resourceStop event before cleanup
        if (_jsEngine && _jsEngine->IsInitialized()) {
            v8::Isolate *isolate = _jsEngine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _jsEngine->GetContext();
            v8::Context::Scope contextScope(context);

            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, name));

            SetCurrentResourceContext(name);
            Events::EmitReserved(isolate, context, "resourceStop", args);
            SetCurrentResourceContext("");
        }

        // Call cleanup (removes handlers)
        CallResourceStop(name);

        // Clear exports
        resource->ClearExports();

        // Transition to Stopped
        resource->TransitionTo(ResourceState::Stopped);

        stopped.push_back(name);
        FireOnResourceStopped(name);
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Stopped JS resource: {}", name);

        return ResourceOperationResult::Success(stopped);
    }

    ResourceOperationResult ResourceManager::RestartResource(const std::string &name) {
        auto stopResult = StopResource(name);
        if (!stopResult.success) {
            return stopResult;
        }

        return StartResource(name);
    }

    ResourceOperationResult ResourceManager::ReloadResource(const std::string &name) {
        return RestartResource(name);
    }

    bool ResourceManager::ExecuteResourceScript(Resource &resource, std::string &outError) {
        if (!_jsEngine || !_jsEngine->IsInitialized()) {
            outError = "JavaScript engine not initialized";
            return false;
        }

        std::string entryPoint = _config.isClient ? resource.GetClientEntryPoint() : resource.GetServerEntryPoint();

        if (entryPoint.empty()) {
            return true;
        }

        if (!std::filesystem::exists(entryPoint)) {
            outError = "Entry point not found: " + entryPoint;
            return false;
        }

        std::string resourceName = resource.GetName();
        SetCurrentResourceContext(resourceName);

        std::filesystem::path absPath = std::filesystem::absolute(entryPoint);
        std::string absPathStr = absPath.string();

        std::string escapedPath = absPathStr;
        for (size_t pos = 0; (pos = escapedPath.find('\\', pos)) != std::string::npos; pos += 2) {
            escapedPath.replace(pos, 1, "\\\\");
        }

        std::string code;
        bool isESModule = resource.GetManifest().IsESModule();

        if (isESModule) {
            // ES Module - just import, no lifecycle function detection
            code =
                "(async function() {\n"
                "    try {\n"
                "        await import('file://" + escapedPath + "');\n"
                "    } catch (err) {\n"
                "        console.error('[" + resourceName + "] Failed to load ES module:', err);\n"
                "        throw err;\n"
                "    }\n"
                "})();\n";
        } else {
            // CommonJS - just require, no lifecycle function detection
            code =
                "(function() {\n"
                "    require('" + escapedPath + "');\n"
                "})();\n";
        }

        bool result = _jsEngine->Execute(code, absPathStr);
        if (!result) {
            outError = _jsEngine->GetLastError();
        }

        SetCurrentResourceContext("");

        return result;
    }

    bool ResourceManager::CallResourceStop(const std::string &resourceName) {
        // Cleanup handlers before resource fully stops
        Events::CleanupResource(resourceName);
        return true;
    }

    std::vector<std::string> ResourceManager::GetAllResourceNames() const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        std::vector<std::string> names;
        names.reserve(_resources.size());
        for (const auto &[name, _] : _resources) {
            names.push_back(name);
        }
        return names;
    }

    std::vector<std::string> ResourceManager::GetRunningResourceNames() const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        std::vector<std::string> names;
        for (const auto &[name, resource] : _resources) {
            if (resource->IsRunning()) {
                names.push_back(name);
            }
        }
        return names;
    }

    std::vector<std::string> ResourceManager::GetLoadOrder() const {
        return ComputeLoadOrder();
    }

    bool ResourceManager::HasResource(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        return _resources.find(name) != _resources.end();
    }

    bool ResourceManager::IsResourceRunning(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        auto it = _resources.find(name);
        return it != _resources.end() && it->second->IsRunning();
    }

    ResourceState ResourceManager::GetResourceState(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        auto it = _resources.find(name);
        if (it == _resources.end()) {
            return ResourceState::Unloaded;
        }
        return it->second->GetState();
    }

    const Resource *ResourceManager::GetResource(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        auto it = _resources.find(name);
        return it != _resources.end() ? it->second.get() : nullptr;
    }

    Resource *ResourceManager::GetResourceMutable(const std::string &name) {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        auto it = _resources.find(name);
        return it != _resources.end() ? it->second.get() : nullptr;
    }

    std::set<std::string> ResourceManager::GetDependents(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_graphMutex);
        auto it = _dependents.find(name);
        return it != _dependents.end() ? it->second : std::set<std::string>{};
    }

    std::set<std::string> ResourceManager::GetDependencies(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_graphMutex);
        auto it = _dependencies.find(name);
        return it != _dependencies.end() ? it->second : std::set<std::string>{};
    }

    void ResourceManager::SetOnResourceStarted(ResourceEventCallback callback) {
        _onResourceStarted = std::move(callback);
    }

    void ResourceManager::SetOnResourceStopped(ResourceEventCallback callback) {
        _onResourceStopped = std::move(callback);
    }

    void ResourceManager::SetOnResourceError(ResourceErrorCallback callback) {
        _onResourceError = std::move(callback);
    }

    void ResourceManager::SetOnResourceStateChanged(ResourceStateCallback callback) {
        _onResourceStateChanged = std::move(callback);
    }

    Engine *ResourceManager::GetJSEngine() const {
        return _jsEngine;
    }

    void ResourceManager::SetCurrentResourceContext(const std::string &name) {
        std::lock_guard<std::mutex> lock(_contextMutex);
        _currentResourceContext = name;
    }

    std::string ResourceManager::GetCurrentResourceContext() const {
        std::lock_guard<std::mutex> lock(_contextMutex);
        return _currentResourceContext;
    }

    Resource *ResourceManager::GetCurrentResource() {
        std::string name = GetCurrentResourceContext();
        if (name.empty()) {
            return nullptr;
        }
        return GetResourceMutable(name);
    }

    size_t ResourceManager::GetResourceCount() const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        return _resources.size();
    }

    size_t ResourceManager::GetRunningResourceCount() const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        size_t count = 0;
        for (const auto &[_, resource] : _resources) {
            if (resource->IsRunning()) {
                ++count;
            }
        }
        return count;
    }

    void ResourceManager::HandleResourceRuntimeError(const std::string &resourceName, const std::string &error) {
        Resource *resource = GetResourceMutable(resourceName);
        if (!resource) {
            return;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Runtime error: {}", resourceName, error);

        resource->SetError(error);
        FireOnResourceError(resourceName, error);

        const auto &config = resource->GetManifest().GetMafiaHubConfig();
        if (config.errorBehavior == "restart") {
            ScheduleAutoRestart(resourceName);
        } else if (config.errorBehavior == "stop") {
            StopResource(resourceName);
        }
        // "continue" behavior: do nothing, let the resource keep running
    }

    bool ResourceManager::ScheduleAutoRestart(const std::string &resourceName) {
        Resource *resource = GetResourceMutable(resourceName);
        if (!resource || !resource->CanAutoRestart()) {
            return false;
        }

        resource->RecordRestartAttempt();
        int backoffMs = resource->GetRestartBackoffMs();

        std::lock_guard<std::mutex> lock(_scheduledRestartsMutex);
        _scheduledRestarts.push_back({
            resourceName,
            std::chrono::system_clock::now() + std::chrono::milliseconds(backoffMs)
        });

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Scheduled restart for {} in {}ms", resourceName, backoffMs);
        return true;
    }

    void ResourceManager::ProcessScheduledRestarts() {
        std::vector<ScheduledRestart> dueRestarts;

        {
            std::lock_guard<std::mutex> lock(_scheduledRestartsMutex);
            auto now = std::chrono::system_clock::now();

            auto it = std::remove_if(_scheduledRestarts.begin(), _scheduledRestarts.end(),
                [&now, &dueRestarts](const ScheduledRestart &sr) {
                    if (sr.scheduledTime <= now) {
                        dueRestarts.push_back(sr);
                        return true;
                    }
                    return false;
                });
            _scheduledRestarts.erase(it, _scheduledRestarts.end());
        }

        for (const auto &sr : dueRestarts) {
            RestartResource(sr.resourceName);
        }
    }

    void ResourceManager::FireOnResourceStarted(const std::string &name) {
        if (_onResourceStarted) {
            _onResourceStarted(name);
        }
    }

    void ResourceManager::FireOnResourceStopped(const std::string &name) {
        if (_onResourceStopped) {
            _onResourceStopped(name);
        }
    }

    void ResourceManager::FireOnResourceError(const std::string &name, const std::string &error) {
        if (_onResourceError) {
            _onResourceError(name, error);
        }
    }

    void ResourceManager::FireOnResourceStateChanged(const std::string &name, ResourceState oldState, ResourceState newState) {
        if (_onResourceStateChanged) {
            _onResourceStateChanged(name, oldState, newState);
        }
    }

} // namespace Framework::Scripting
