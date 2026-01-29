#include "js_resource_manager.h"

#include <algorithm>
#include <filesystem>
#include <queue>
#include <stack>

namespace Framework::Scripting::JS {

    JSResourceManager::JSResourceManager(Engine *jsEngine, const JSResourceManagerConfig &config)
        : _config(config)
        , _jsEngine(jsEngine) {
    }

    JSResourceManager::~JSResourceManager() {
        StopAll();
    }

    const JSResourceManagerConfig &JSResourceManager::GetConfig() const {
        return _config;
    }

    void JSResourceManager::SetConfig(const JSResourceManagerConfig &config) {
        _config = config;
    }

    size_t JSResourceManager::DiscoverResources() {
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

    bool JSResourceManager::DiscoverResource(const std::string &path) {
        auto resource = std::make_unique<JSResource>(path);

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

    void JSResourceManager::BuildDependencyGraph() {
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

    bool JSResourceManager::ValidateDependencies(std::string &outError) const {
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

    std::vector<std::string> JSResourceManager::ComputeLoadOrder() const {
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

    JSResourceOperationResult JSResourceManager::StartAll() {
        std::string depError;
        if (!ValidateDependencies(depError)) {
            return JSResourceOperationResult::Failure(depError);
        }

        auto loadOrder = ComputeLoadOrder();
        if (loadOrder.empty() && GetResourceCount() > 0) {
            return JSResourceOperationResult::Failure("Dependency cycle detected");
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

        return JSResourceOperationResult::Success(started);
    }

    JSResourceOperationResult JSResourceManager::StopAll() {
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

        return JSResourceOperationResult::Success(stopped);
    }

    JSResourceOperationResult JSResourceManager::StartResource(const std::string &name) {
        JSResource *resource = GetResourceMutable(name);
        if (!resource) {
            return JSResourceOperationResult::Failure("Resource not found: " + name);
        }

        if (resource->IsRunning()) {
            return JSResourceOperationResult::Success({name});
        }

        // Start dependencies first
        auto deps = GetDependencies(name);
        for (const auto &depName : deps) {
            if (!IsResourceRunning(depName)) {
                auto result = StartResource(depName);
                if (!result.success) {
                    return JSResourceOperationResult::Failure("Failed to start dependency '" + depName + "': " + result.error);
                }
            }
        }

        // Transition to Loading
        if (!resource->TransitionTo(ResourceState::Loading)) {
            return JSResourceOperationResult::Failure("Invalid state transition for resource: " + name);
        }

        // Execute the entry point script
        std::string error;
        if (!ExecuteResourceScript(*resource, error)) {
            resource->SetError(error);
            FireOnResourceError(name, error);
            return JSResourceOperationResult::Failure(error);
        }

        // Transition to Running
        if (!resource->TransitionTo(ResourceState::Running)) {
            return JSResourceOperationResult::Failure("Failed to transition to Running state");
        }

        resource->SetLoadTimestamp();
        resource->ClearRestartAttempts();

        FireOnResourceStarted(name);
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Started JS resource: {}", name);

        return JSResourceOperationResult::Success({name});
    }

    JSResourceOperationResult JSResourceManager::StopResource(const std::string &name) {
        JSResource *resource = GetResourceMutable(name);
        if (!resource) {
            return JSResourceOperationResult::Failure("Resource not found: " + name);
        }

        if (!resource->IsRunning()) {
            return JSResourceOperationResult::Success({});
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

        // Clear exports
        resource->ClearExports();

        // Transition to Stopped
        resource->TransitionTo(ResourceState::Stopped);

        stopped.push_back(name);
        FireOnResourceStopped(name);
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Stopped JS resource: {}", name);

        return JSResourceOperationResult::Success(stopped);
    }

    JSResourceOperationResult JSResourceManager::RestartResource(const std::string &name) {
        auto stopResult = StopResource(name);
        if (!stopResult.success) {
            return stopResult;
        }

        return StartResource(name);
    }

    JSResourceOperationResult JSResourceManager::ReloadResource(const std::string &name) {
        return RestartResource(name);
    }

    bool JSResourceManager::ExecuteResourceScript(JSResource &resource, std::string &outError) {
        if (!_jsEngine || !_jsEngine->IsInitialized()) {
            outError = "JavaScript engine not initialized";
            return false;
        }

        std::string entryPoint = _config.isClient ? resource.GetClientEntryPoint() : resource.GetServerEntryPoint();

        if (entryPoint.empty()) {
            // No entry point defined for this side
            return true;
        }

        if (!std::filesystem::exists(entryPoint)) {
            outError = "Entry point not found: " + entryPoint;
            return false;
        }

        // Set resource context
        SetCurrentResourceContext(resource.GetName());

        bool result = _jsEngine->ExecuteFile(entryPoint);
        if (!result) {
            outError = _jsEngine->GetLastError();
        }

        // Clear context
        SetCurrentResourceContext("");

        return result;
    }

    std::vector<std::string> JSResourceManager::GetAllResourceNames() const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        std::vector<std::string> names;
        names.reserve(_resources.size());
        for (const auto &[name, _] : _resources) {
            names.push_back(name);
        }
        return names;
    }

    std::vector<std::string> JSResourceManager::GetRunningResourceNames() const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        std::vector<std::string> names;
        for (const auto &[name, resource] : _resources) {
            if (resource->IsRunning()) {
                names.push_back(name);
            }
        }
        return names;
    }

    std::vector<std::string> JSResourceManager::GetLoadOrder() const {
        return ComputeLoadOrder();
    }

    bool JSResourceManager::HasResource(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        return _resources.find(name) != _resources.end();
    }

    bool JSResourceManager::IsResourceRunning(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        auto it = _resources.find(name);
        return it != _resources.end() && it->second->IsRunning();
    }

    ResourceState JSResourceManager::GetResourceState(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        auto it = _resources.find(name);
        if (it == _resources.end()) {
            return ResourceState::Unloaded;
        }
        return it->second->GetState();
    }

    const JSResource *JSResourceManager::GetResource(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        auto it = _resources.find(name);
        return it != _resources.end() ? it->second.get() : nullptr;
    }

    JSResource *JSResourceManager::GetResourceMutable(const std::string &name) {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        auto it = _resources.find(name);
        return it != _resources.end() ? it->second.get() : nullptr;
    }

    std::set<std::string> JSResourceManager::GetDependents(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_graphMutex);
        auto it = _dependents.find(name);
        return it != _dependents.end() ? it->second : std::set<std::string>{};
    }

    std::set<std::string> JSResourceManager::GetDependencies(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_graphMutex);
        auto it = _dependencies.find(name);
        return it != _dependencies.end() ? it->second : std::set<std::string>{};
    }

    void JSResourceManager::SetOnResourceStarted(JSResourceEventCallback callback) {
        _onResourceStarted = std::move(callback);
    }

    void JSResourceManager::SetOnResourceStopped(JSResourceEventCallback callback) {
        _onResourceStopped = std::move(callback);
    }

    void JSResourceManager::SetOnResourceError(JSResourceErrorCallback callback) {
        _onResourceError = std::move(callback);
    }

    void JSResourceManager::SetOnResourceStateChanged(JSResourceStateCallback callback) {
        _onResourceStateChanged = std::move(callback);
    }

    Engine *JSResourceManager::GetJSEngine() const {
        return _jsEngine;
    }

    void JSResourceManager::SetCurrentResourceContext(const std::string &name) {
        std::lock_guard<std::mutex> lock(_contextMutex);
        _currentResourceContext = name;
    }

    std::string JSResourceManager::GetCurrentResourceContext() const {
        std::lock_guard<std::mutex> lock(_contextMutex);
        return _currentResourceContext;
    }

    JSResource *JSResourceManager::GetCurrentResource() {
        std::string name = GetCurrentResourceContext();
        if (name.empty()) {
            return nullptr;
        }
        return GetResourceMutable(name);
    }

    size_t JSResourceManager::GetResourceCount() const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        return _resources.size();
    }

    size_t JSResourceManager::GetRunningResourceCount() const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        size_t count = 0;
        for (const auto &[_, resource] : _resources) {
            if (resource->IsRunning()) {
                ++count;
            }
        }
        return count;
    }

    void JSResourceManager::HandleResourceRuntimeError(const std::string &resourceName, const std::string &error) {
        JSResource *resource = GetResourceMutable(resourceName);
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

    bool JSResourceManager::ScheduleAutoRestart(const std::string &resourceName) {
        JSResource *resource = GetResourceMutable(resourceName);
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

    void JSResourceManager::ProcessScheduledRestarts() {
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

    void JSResourceManager::FireOnResourceStarted(const std::string &name) {
        if (_onResourceStarted) {
            _onResourceStarted(name);
        }
    }

    void JSResourceManager::FireOnResourceStopped(const std::string &name) {
        if (_onResourceStopped) {
            _onResourceStopped(name);
        }
    }

    void JSResourceManager::FireOnResourceError(const std::string &name, const std::string &error) {
        if (_onResourceError) {
            _onResourceError(name, error);
        }
    }

    void JSResourceManager::FireOnResourceStateChanged(const std::string &name, ResourceState oldState, ResourceState newState) {
        if (_onResourceStateChanged) {
            _onResourceStateChanged(name, oldState, newState);
        }
    }

} // namespace Framework::Scripting::JS
