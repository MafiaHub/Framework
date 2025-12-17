#include "resource_manager.h"

#include <logging/logger.h>

#include <cppfs/FileHandle.h>
#include <cppfs/FileIterator.h>
#include <cppfs/fs.h>

#include <algorithm>

namespace Framework::Scripting {

    ResourceManager::ResourceManager(sol::state *luaState, const ResourceManagerConfig &config)
        : _config(config)
        , _luaState(luaState) {
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("ResourceManager initialized with path: {}", _config.resourcesPath);
    }

    ResourceManager::~ResourceManager() {
        // Stop all resources before destruction
        StopAll();
    }

    const ResourceManagerConfig &ResourceManager::GetConfig() const {
        return _config;
    }

    void ResourceManager::SetConfig(const ResourceManagerConfig &config) {
        _config = config;
    }

    // Discovery

    size_t ResourceManager::DiscoverResources() {
        std::lock_guard<std::mutex> lock(_resourcesMutex);

        _resources.clear();
        _dependencyGraph.Clear();

        cppfs::FileHandle resourcesDir = cppfs::fs::open(_config.resourcesPath);

        // Check for legacy gamemode support (Phase 8.3)
        if (_config.enableLegacySupport) {
            // Look for a manifest.json directly in the resources path (legacy structure)
            std::string legacyManifestPath = _config.resourcesPath + "/manifest.json";
            cppfs::FileHandle legacyManifest = cppfs::fs::open(legacyManifestPath);

            if (legacyManifest.exists() && legacyManifest.isFile()) {
                _hasLegacyGamemode  = true;
                _legacyGamemodePath = _config.resourcesPath;
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Detected legacy gamemode structure at: {}", _legacyGamemodePath);

                // Create a resource for the legacy gamemode
                auto resource = std::make_unique<Resource>(_legacyGamemodePath);
                if (resource->IsManifestValid()) {
                    std::string name    = resource->GetName();
                    _resources[name]    = std::move(resource);
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Discovered legacy gamemode: {}", name);
                }
            }
        }

        // Scan for resources in subdirectories
        if (resourcesDir.exists() && resourcesDir.isDirectory()) {
            for (auto it = resourcesDir.begin(); it != resourcesDir.end(); ++it) {
                std::string entryName = *it;
                std::string entryPath = _config.resourcesPath + "/" + entryName;

                cppfs::FileHandle entry = cppfs::fs::open(entryPath);
                if (entry.isDirectory()) {
                    // Check for manifest.json in the directory
                    std::string manifestPath = entryPath + "/manifest.json";
                    cppfs::FileHandle manifest = cppfs::fs::open(manifestPath);

                    if (manifest.exists() && manifest.isFile()) {
                        auto resource = std::make_unique<Resource>(entryPath);
                        if (resource->IsManifestValid()) {
                            std::string name = resource->GetName();

                            // Check for duplicate names
                            if (_resources.find(name) != _resources.end()) {
                                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Duplicate resource name '{}' found at: {}", name, entryPath);
                                continue;
                            }

                            _resources[name] = std::move(resource);
                            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Discovered resource: {} ({})", name, entryPath);
                        } else {
                            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Invalid manifest in: {}", entryPath);
                        }
                    }
                }
            }
        } else {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Resources directory not found: {}", _config.resourcesPath);
        }

        // Build dependency graph
        BuildDependencyGraph();

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Discovered {} resources", _resources.size());
        return _resources.size();
    }

    bool ResourceManager::DiscoverResource(const std::string &path) {
        std::lock_guard<std::mutex> lock(_resourcesMutex);

        cppfs::FileHandle dir = cppfs::fs::open(path);
        if (!dir.exists() || !dir.isDirectory()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Resource path not found: {}", path);
            return false;
        }

        std::string manifestPath = path + "/manifest.json";
        cppfs::FileHandle manifest = cppfs::fs::open(manifestPath);

        if (!manifest.exists() || !manifest.isFile()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("No manifest.json found in: {}", path);
            return false;
        }

        auto resource = std::make_unique<Resource>(path);
        if (!resource->IsManifestValid()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Invalid manifest in: {}", path);
            return false;
        }

        std::string name = resource->GetName();

        if (_resources.find(name) != _resources.end()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Resource '{}' already exists", name);
            return false;
        }

        _resources[name] = std::move(resource);

        // Update dependency graph
        BuildDependencyGraph();

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Discovered resource: {}", name);
        return true;
    }

    bool ResourceManager::HasLegacyGamemode() const {
        return _hasLegacyGamemode;
    }

    std::string ResourceManager::GetLegacyGamemodePath() const {
        return _legacyGamemodePath;
    }

    // Lifecycle Management

    ResourceOperationResult ResourceManager::StartAll() {
        std::vector<std::string> loadOrder = GetLoadOrder();
        std::vector<std::string> started;

        for (const auto &name : loadOrder) {
            auto result = StartResource(name);
            if (result.success) {
                started.push_back(name);
            } else {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to start resource '{}': {}", name, result.error);
                // Continue trying to start other resources
            }
        }

        return ResourceOperationResult::Success(started);
    }

    ResourceOperationResult ResourceManager::StopAll() {
        std::vector<std::string> unloadOrder;
        {
            std::lock_guard<std::mutex> lock(_graphMutex);
            unloadOrder = _dependencyGraph.GetUnloadOrder();
        }

        std::vector<std::string> stopped;

        for (const auto &name : unloadOrder) {
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

        if (!resource->IsManifestValid()) {
            return ResourceOperationResult::Failure("Resource has invalid manifest: " + name);
        }

        // Check dependencies are running
        auto deps = GetDependencies(name);
        for (const auto &dep : deps) {
            if (!IsResourceRunning(dep)) {
                // Try to start the dependency first
                auto depResult = StartResource(dep);
                if (!depResult.success) {
                    return ResourceOperationResult::Failure("Failed to start dependency '" + dep + "' for resource '" + name + "': " + depResult.error);
                }
            }
        }

        // Transition to Loading state
        ResourceState oldState = resource->GetState();
        if (!resource->TransitionTo(ResourceState::Loading)) {
            return ResourceOperationResult::Failure("Cannot start resource '" + name + "' from state: " + std::string(ResourceStateToString(oldState)));
        }
        FireOnResourceStateChanged(name, oldState, ResourceState::Loading);

        // Create environment
        auto env = CreateResourceEnvironment(name);
        if (!env) {
            resource->SetError("Failed to create environment");
            FireOnResourceError(name, "Failed to create environment");
            FireOnResourceStateChanged(name, ResourceState::Loading, ResourceState::Error);
            return ResourceOperationResult::Failure("Failed to create environment for: " + name);
        }
        resource->SetEnvironment(std::move(env));

        // Set current resource context for builtins
        SetCurrentResourceContext(name);

        // Execute scripts
        std::string execError;
        if (!ExecuteResourceScripts(*resource, execError)) {
            SetCurrentResourceContext(""); // Clear context on error
            resource->SetError(execError);
            FireOnResourceError(name, execError);
            FireOnResourceStateChanged(name, ResourceState::Loading, ResourceState::Error);
            return ResourceOperationResult::Failure(execError);
        }

        // Transition to Running state
        oldState = resource->GetState();
        resource->TransitionTo(ResourceState::Running);
        resource->SetLoadTimestamp();
        FireOnResourceStateChanged(name, oldState, ResourceState::Running);
        FireOnResourceStarted(name);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Started resource: {}", name);
        return ResourceOperationResult::Success({name});
    }

    ResourceOperationResult ResourceManager::StopResource(const std::string &name) {
        Resource *resource = GetResourceMutable(name);
        if (!resource) {
            return ResourceOperationResult::Failure("Resource not found: " + name);
        }

        if (!resource->IsRunning()) {
            return ResourceOperationResult::Success();
        }

        std::vector<std::string> stopped;

        // Handle dependents
        if (_config.cascadeStopDependents) {
            auto dependents = GetDependents(name);
            for (const auto &dep : dependents) {
                if (IsResourceRunning(dep)) {
                    auto result = StopResource(dep);
                    if (result.success) {
                        stopped.insert(stopped.end(), result.affectedResources.begin(), result.affectedResources.end());
                    }
                }
            }
        }

        // Transition to Stopping state
        ResourceState oldState = resource->GetState();
        if (!resource->TransitionTo(ResourceState::Stopping)) {
            return ResourceOperationResult::Failure("Cannot stop resource '" + name + "' from state: " + std::string(ResourceStateToString(oldState)));
        }
        FireOnResourceStateChanged(name, oldState, ResourceState::Stopping);

        // Clear event handlers
        resource->ClearEventHandlers();

        // Clear exports
        resource->ClearExports();

        // Clear environment
        if (resource->GetEnvironment()) {
            EnvironmentSandbox::ClearEnvironment(*resource->GetEnvironment());
        }
        resource->SetEnvironment(nullptr);

        // Transition to Stopped state
        oldState = resource->GetState();
        resource->TransitionTo(ResourceState::Stopped);
        FireOnResourceStateChanged(name, oldState, ResourceState::Stopped);
        FireOnResourceStopped(name);

        stopped.push_back(name);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Stopped resource: {}", name);
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
        // For now, reload is the same as restart
        // Future: implement state preservation
        return RestartResource(name);
    }

    // Registry Queries

    std::vector<std::string> ResourceManager::GetAllResourceNames() const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        std::vector<std::string> names;
        names.reserve(_resources.size());
        for (const auto &pair : _resources) {
            names.push_back(pair.first);
        }
        return names;
    }

    std::vector<std::string> ResourceManager::GetRunningResourceNames() const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        std::vector<std::string> names;
        for (const auto &pair : _resources) {
            if (pair.second->IsRunning()) {
                names.push_back(pair.first);
            }
        }
        return names;
    }

    std::vector<std::string> ResourceManager::GetLoadOrder() const {
        std::lock_guard<std::mutex> lock(_graphMutex);
        return _dependencyGraph.GetLoadOrder();
    }

    bool ResourceManager::HasResource(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        return _resources.find(name) != _resources.end();
    }

    bool ResourceManager::IsResourceRunning(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        auto it = _resources.find(name);
        if (it == _resources.end()) {
            return false;
        }
        return it->second->IsRunning();
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
        if (it == _resources.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    sol::table ResourceManager::GetResourceInfo(sol::state &luaState, const std::string &name) const {
        const Resource *resource = GetResource(name);
        if (!resource) {
            return sol::nil;
        }

        sol::table info = luaState.create_table();
        info["name"]        = resource->GetName();
        info["version"]     = resource->GetVersion();
        info["author"]      = resource->GetAuthor();
        info["description"] = resource->GetDescription();
        info["state"]       = ResourceStateToString(resource->GetState());
        info["path"]        = resource->GetPath();
        info["scriptCount"] = resource->GetScriptCount();

        // Dependencies
        sol::table deps = luaState.create_table();
        int i           = 1;
        for (const auto &dep : resource->GetManifest().dependencies) {
            deps[i++] = dep.name;
        }
        info["dependencies"] = deps;

        // Exports
        sol::table exports = luaState.create_table();
        i                  = 1;
        for (const auto &exp : resource->GetManifest().exports) {
            exports[i++] = exp;
        }
        info["exports"] = exports;

        // Permissions
        sol::table perms = luaState.create_table();
        i                = 1;
        for (const auto &perm : resource->GetManifest().permissions) {
            perms[i++] = perm;
        }
        info["permissions"] = perms;

        // Load timestamp
        auto loadTime              = resource->GetLoadTimestamp();
        auto loadTimeSinceEpoch    = std::chrono::duration_cast<std::chrono::seconds>(loadTime.time_since_epoch()).count();
        info["loadTime"]           = loadTimeSinceEpoch;

        return info;
    }

    // Dependency Queries

    std::set<std::string> ResourceManager::GetDependents(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_graphMutex);
        return _dependencyGraph.GetDirectDependents(name);
    }

    std::set<std::string> ResourceManager::GetDependencies(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_graphMutex);
        return _dependencyGraph.GetDirectDependencies(name);
    }

    // Exports Registry

    sol::object ResourceManager::GetExport(const std::string &resourceName, const std::string &exportName) const {
        const Resource *resource = GetResource(resourceName);
        if (!resource || !resource->IsRunning()) {
            return sol::nil;
        }
        return resource->GetExport(exportName);
    }

    std::vector<std::string> ResourceManager::ListExports(const std::string &resourceName) const {
        const Resource *resource = GetResource(resourceName);
        if (!resource) {
            return {};
        }
        return resource->GetRegisteredExportNames();
    }

    // Event Callbacks

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

    sol::state *ResourceManager::GetLuaState() const {
        return _luaState;
    }

    size_t ResourceManager::GetResourceCount() const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        return _resources.size();
    }

    size_t ResourceManager::GetRunningResourceCount() const {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        size_t count = 0;
        for (const auto &pair : _resources) {
            if (pair.second->IsRunning()) {
                count++;
            }
        }
        return count;
    }

    // Private methods

    Resource *ResourceManager::GetResourceMutable(const std::string &name) {
        std::lock_guard<std::mutex> lock(_resourcesMutex);
        auto it = _resources.find(name);
        if (it == _resources.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    void ResourceManager::BuildDependencyGraph() {
        std::lock_guard<std::mutex> lock(_graphMutex);
        _dependencyGraph.Clear();

        // Add all resources as nodes
        for (const auto &pair : _resources) {
            const auto &manifest = pair.second->GetManifest();
            _dependencyGraph.AddNode(pair.first, manifest.priority);
        }

        // Add dependency edges
        for (const auto &pair : _resources) {
            const auto &manifest = pair.second->GetManifest();
            for (const auto &dep : manifest.dependencies) {
                auto result = _dependencyGraph.AddDependency(pair.first, dep.name, dep.version);
                if (!result.success) {
                    if (!result.cycle.empty()) {
                        std::string cycleStr;
                        for (size_t i = 0; i < result.cycle.size(); ++i) {
                            if (i > 0)
                                cycleStr += " -> ";
                            cycleStr += result.cycle[i];
                        }
                        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Circular dependency detected: {}", cycleStr);
                    } else if (_config.warnOnMissingDependency) {
                        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Missing dependency '{}' for resource '{}'", dep.name, pair.first);
                    } else {
                        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Dependency error for '{}': {}", pair.first, result.error);
                    }
                }
            }
        }
    }

    bool ResourceManager::ValidateDependencies(std::string &outError) const {
        std::map<std::string, std::vector<std::string>> missing;

        std::lock_guard<std::mutex> lock(_graphMutex);
        if (!_dependencyGraph.ValidateDependencies(missing)) {
            std::string errorMsg = "Missing dependencies:\n";
            for (const auto &pair : missing) {
                errorMsg += "  " + pair.first + " requires: ";
                for (size_t i = 0; i < pair.second.size(); ++i) {
                    if (i > 0)
                        errorMsg += ", ";
                    errorMsg += pair.second[i];
                }
                errorMsg += "\n";
            }
            outError = errorMsg;
            return false;
        }

        return true;
    }

    bool ResourceManager::ExecuteResourceScripts(Resource &resource, std::string &outError) {
        if (!_luaState) {
            outError = "Lua state not initialized";
            return false;
        }

        sol::environment *env = resource.GetEnvironment();
        if (!env) {
            outError = "Resource environment not initialized";
            return false;
        }

        // Determine which scripts to run based on client/server
        const auto &scripts = _config.isClient ? resource.GetClientScriptPaths() : resource.GetServerScriptPaths();

        if (scripts.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Resource '{}' has no scripts to execute", resource.GetName());
            return true; // Not an error, just nothing to do
        }

        for (const auto &scriptPath : scripts) {
            if (!EnvironmentSandbox::ExecuteScript(*_luaState, *env, scriptPath, outError)) {
                return false;
            }
        }

        return true;
    }

    std::unique_ptr<sol::environment> ResourceManager::CreateResourceEnvironment(const std::string &resourceName) {
        if (!_luaState) {
            return nullptr;
        }

        auto env = EnvironmentSandbox::CreateEnvironment(*_luaState, resourceName);

        // Share safe globals
        EnvironmentSandbox::ShareGlobals(*_luaState, *env, EnvironmentSandbox::GetSafeGlobalNames());

        // Share framework builtins
        EnvironmentSandbox::ShareGlobals(*_luaState, *env, EnvironmentSandbox::GetFrameworkBuiltinNames());

        // Apply client-side sandboxing if needed
        if (_config.isClient) {
            EnvironmentSandbox::DisableDangerousFunctions(*env);
        }

        return env;
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

    // Current Resource Context

    void ResourceManager::SetCurrentResourceContext(const std::string &name) {
        std::lock_guard<std::mutex> lock(_contextMutex);
        _currentResourceContext = name;
    }

    std::string ResourceManager::GetCurrentResourceContext() const {
        std::lock_guard<std::mutex> lock(_contextMutex);
        return _currentResourceContext;
    }

    Resource *ResourceManager::GetCurrentResource() {
        std::string current = GetCurrentResourceContext();
        if (current.empty()) {
            return nullptr;
        }
        return GetResourceMutable(current);
    }

    // Inter-Resource Communication (Phase 3)

    bool ResourceManager::RegisterExport(const std::string &exportName, sol::object value) {
        Resource *resource = GetCurrentResource();
        if (!resource) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Cannot register export '{}': no current resource context", exportName);
            return false;
        }

        return resource->RegisterExport(exportName, value);
    }

    void ResourceManager::BroadcastGlobalEvent(const std::string &eventName, sol::variadic_args args) {
        std::lock_guard<std::mutex> lock(_globalEventsMutex);

        auto it = _globalEventHandlers.find(eventName);
        if (it == _globalEventHandlers.end()) {
            return;
        }

        // Iterate through all resources that have handlers for this event
        for (auto &resourcePair : it->second) {
            const std::string &resourceName = resourcePair.first;

            // Only invoke if resource is running
            if (!IsResourceRunning(resourceName)) {
                continue;
            }

            // Save current context and set to the handler's resource
            std::string previousContext = GetCurrentResourceContext();
            SetCurrentResourceContext(resourceName);

            for (auto &handler : resourcePair.second) {
                sol::protected_function_result result = handler(args);
                if (!result.valid()) {
                    sol::error err = result;
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Global event '{}' handler error: {}", resourceName, eventName, err.what());
                }
            }

            // Restore context
            SetCurrentResourceContext(previousContext);
        }
    }

    bool ResourceManager::EmitTargetedEvent(const std::string &targetResource, const std::string &eventName, sol::variadic_args args) {
        if (!IsResourceRunning(targetResource)) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Cannot emit targeted event '{}' to '{}': resource not running", eventName, targetResource);
            return false;
        }

        std::lock_guard<std::mutex> lock(_targetedEventsMutex);

        auto resourceIt = _targetedEventHandlers.find(targetResource);
        if (resourceIt == _targetedEventHandlers.end()) {
            return false;
        }

        auto eventIt = resourceIt->second.find(eventName);
        if (eventIt == resourceIt->second.end()) {
            return false;
        }

        // Save current context and set to target resource
        std::string previousContext = GetCurrentResourceContext();
        SetCurrentResourceContext(targetResource);

        for (auto &handler : eventIt->second) {
            sol::protected_function_result result = handler(args);
            if (!result.valid()) {
                sol::error err = result;
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Targeted event '{}' handler error: {}", targetResource, eventName, err.what());
            }
        }

        // Restore context
        SetCurrentResourceContext(previousContext);
        return true;
    }

    void ResourceManager::RegisterGlobalEventHandler(const std::string &eventName, sol::protected_function handler) {
        std::string resourceName = GetCurrentResourceContext();
        if (resourceName.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Cannot register global event handler '{}': no current resource context", eventName);
            return;
        }

        std::lock_guard<std::mutex> lock(_globalEventsMutex);
        _globalEventHandlers[eventName][resourceName].push_back(handler);
    }

    void ResourceManager::RegisterTargetedEventHandler(const std::string &eventName, sol::protected_function handler) {
        std::string resourceName = GetCurrentResourceContext();
        if (resourceName.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Cannot register targeted event handler '{}': no current resource context", eventName);
            return;
        }

        std::lock_guard<std::mutex> lock(_targetedEventsMutex);
        _targetedEventHandlers[resourceName][eventName].push_back(handler);
    }

    void ResourceManager::SendMessage(const std::string &targetResource, const std::string &messageType, sol::object payload) {
        if (!IsResourceRunning(targetResource)) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Cannot send message '{}' to '{}': resource not running", messageType, targetResource);
            return;
        }

        std::lock_guard<std::mutex> lock(_messageHandlersMutex);

        auto resourceIt = _messageHandlers.find(targetResource);
        if (resourceIt == _messageHandlers.end()) {
            return;
        }

        auto handlerIt = resourceIt->second.find(messageType);
        if (handlerIt == resourceIt->second.end()) {
            return;
        }

        // Save current context and set to target resource
        std::string previousContext = GetCurrentResourceContext();
        SetCurrentResourceContext(targetResource);

        // For fire-and-forget, we pass a no-op reply function
        sol::protected_function noOpReply = _luaState->load("return function() end")().get<sol::protected_function>();

        sol::protected_function_result result = handlerIt->second(payload, noOpReply);
        if (!result.valid()) {
            sol::error err = result;
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Message handler '{}' error: {}", targetResource, messageType, err.what());
        }

        // Restore context
        SetCurrentResourceContext(previousContext);
    }

    void ResourceManager::SendRequest(const std::string &targetResource, const std::string &messageType, sol::object payload, sol::protected_function callback) {
        if (!IsResourceRunning(targetResource)) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Cannot send request '{}' to '{}': resource not running", messageType, targetResource);
            return;
        }

        std::string sourceResource = GetCurrentResourceContext();

        // Generate request ID
        uint64_t requestId;
        {
            std::lock_guard<std::mutex> lock(_pendingRequestsMutex);
            requestId = _nextRequestId++;

            PendingRequest pending;
            pending.requestId      = requestId;
            pending.callback       = callback;
            pending.sourceResource = sourceResource;
            _pendingRequests[requestId] = pending;
        }

        // Get the handler
        sol::protected_function handler;
        {
            std::lock_guard<std::mutex> lock(_messageHandlersMutex);

            auto resourceIt = _messageHandlers.find(targetResource);
            if (resourceIt == _messageHandlers.end()) {
                std::lock_guard<std::mutex> pendingLock(_pendingRequestsMutex);
                _pendingRequests.erase(requestId);
                return;
            }

            auto handlerIt = resourceIt->second.find(messageType);
            if (handlerIt == resourceIt->second.end()) {
                std::lock_guard<std::mutex> pendingLock(_pendingRequestsMutex);
                _pendingRequests.erase(requestId);
                return;
            }

            handler = handlerIt->second;
        }

        // Create a reply function that queues the response
        // We need to capture self and requestId to queue the response
        ResourceManager *self = this;
        std::string replyFnName = "__reply_" + std::to_string(requestId);
        _luaState->set_function(replyFnName,
            [self, requestId](sol::object response) {
                std::lock_guard<std::mutex> lock(self->_responseQueueMutex);
                PendingResponse pendingResp;
                pendingResp.requestId = requestId;
                pendingResp.response  = response;
                self->_responseQueue.push_back(pendingResp);
            });
        sol::function replyFn = (*_luaState)[replyFnName];

        // Save current context and set to target resource
        std::string previousContext = GetCurrentResourceContext();
        SetCurrentResourceContext(targetResource);

        sol::protected_function_result result = handler(payload, replyFn);
        if (!result.valid()) {
            sol::error err = result;
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Request handler '{}' error: {}", targetResource, messageType, err.what());
        }

        // Restore context
        SetCurrentResourceContext(previousContext);

        // Clean up the temporary global function
        (*_luaState)[replyFnName] = sol::nil;
    }

    void ResourceManager::RegisterMessageHandler(const std::string &messageType, sol::protected_function handler) {
        std::string resourceName = GetCurrentResourceContext();
        if (resourceName.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Cannot register message handler '{}': no current resource context", messageType);
            return;
        }

        std::lock_guard<std::mutex> lock(_messageHandlersMutex);
        _messageHandlers[resourceName][messageType] = handler;
    }

    void ResourceManager::ProcessMessageQueue() {
        // Process pending responses
        std::vector<PendingResponse> responsesToProcess;
        {
            std::lock_guard<std::mutex> lock(_responseQueueMutex);
            responsesToProcess.swap(_responseQueue);
        }

        for (const auto &response : responsesToProcess) {
            PendingRequest pending;
            {
                std::lock_guard<std::mutex> lock(_pendingRequestsMutex);
                auto it = _pendingRequests.find(response.requestId);
                if (it == _pendingRequests.end()) {
                    continue;
                }
                pending = it->second;
                _pendingRequests.erase(it);
            }

            // Set context to the source resource before invoking callback
            std::string previousContext = GetCurrentResourceContext();
            SetCurrentResourceContext(pending.sourceResource);

            sol::protected_function_result result = pending.callback(response.response);
            if (!result.valid()) {
                sol::error err = result;
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Request callback error: {}", pending.sourceResource, err.what());
            }

            // Restore context
            SetCurrentResourceContext(previousContext);
        }
    }

} // namespace Framework::Scripting
