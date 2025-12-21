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
        size_t resourceCount = 0;

        {
            std::lock_guard<std::mutex> lock(_resourcesMutex);

            _resources.clear();

            cppfs::FileHandle resourcesDir = cppfs::fs::open(_config.resourcesPath);

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

            resourceCount = _resources.size();
        }

        // Build dependency graph (acquires its own locks)
        BuildDependencyGraph();

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Discovered {} resources", resourceCount);
        return resourceCount;
    }

    bool ResourceManager::DiscoverResource(const std::string &path) {
        std::string name;

        {
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

            name = resource->GetName();

            if (_resources.find(name) != _resources.end()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Resource '{}' already exists", name);
                return false;
            }

            _resources[name] = std::move(resource);
        }

        // Update dependency graph (acquires its own locks)
        BuildDependencyGraph();

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Discovered resource: {}", name);
        return true;
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
        const auto &manifest = resource->GetManifest();
        for (const auto &dep : manifest.dependencies) {
            if (!IsResourceRunning(dep.name)) {
                // Try to start the dependency first
                auto depResult = StartResource(dep.name);
                if (!depResult.success) {
                    if (dep.optional) {
                        // Optional dependency failed, notify but continue
                        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("[{}] Optional dependency '{}' not available: {}", name, dep.name, depResult.error);
                    } else {
                        // Required dependency failed
                        return ResourceOperationResult::Failure("Failed to start dependency '" + dep.name + "' for resource '" + name + "': " + depResult.error);
                    }
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

        // Fire onResourceLoad event after environment is created and scripts are loaded
        FireResourceLifecycleEventInternal(name, "onResourceLoad", {});

        // Check for preserved state from hot-reload
        sol::object preservedState = sol::nil;
        {
            std::lock_guard<std::mutex> lock(_preservedStatesMutex);
            auto it = _preservedStates.find(name);
            if (it != _preservedStates.end()) {
                preservedState = it->second;
                _preservedStates.erase(it);
            }
        }

        // Transition to Running state
        oldState = resource->GetState();
        resource->TransitionTo(ResourceState::Running);
        resource->SetLoadTimestamp();
        FireOnResourceStateChanged(name, oldState, ResourceState::Running);

        FireResourceLifecycleEventWithState(name, "onResourceStart", preservedState);

        // Fire C++ callback
        FireOnResourceStarted(name);

        BroadcastResourceAwarenessEvent("onResourceStarted", name);

        // Clear the resource context now that startup is complete
        SetCurrentResourceContext("");

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

        // Set context for lifecycle events
        SetCurrentResourceContext(name);

        // Fire onResourceStop event - the handler can return a state table to be preserved
        sol::object preservedState = sol::nil;
        sol::environment *env      = resource->GetEnvironment();
        if (env) {
            sol::object handlerObj = (*env)["onResourceStop"];
            if (handlerObj.valid() && handlerObj.is<sol::protected_function>()) {
                sol::protected_function handler = handlerObj.as<sol::protected_function>();
                sol::protected_function_result result = handler();
                if (result.valid()) {
                    // Check if handler returned a state table
                    sol::object returnValue = result;
                    if (returnValue.valid() && returnValue.is<sol::table>()) {
                        preservedState = returnValue;
                        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] onResourceStop returned state for preservation", name);
                    }
                } else {
                    sol::error err = result;
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] onResourceStop error: {}", name, err.what());
                }
            }
        }

        if (preservedState.valid() && preservedState != sol::nil) {
            std::lock_guard<std::mutex> lock(_preservedStatesMutex);
            _preservedStates[name] = preservedState;
        }

        // Clear event handlers owned by this resource
        resource->ClearEventHandlers();

        // Clear exports from this resource
        resource->ClearExports();

        // Remove global event handlers for this resource
        {
            std::lock_guard<std::mutex> lock(_globalEventsMutex);
            for (auto &eventPair : _globalEventHandlers) {
                eventPair.second.erase(name);
            }
        }

        // Remove targeted event handlers for this resource
        {
            std::lock_guard<std::mutex> lock(_targetedEventsMutex);
            _targetedEventHandlers.erase(name);
        }

        // Remove message handlers for this resource
        {
            std::lock_guard<std::mutex> lock(_messageHandlersMutex);
            _messageHandlers.erase(name);
        }

        // Fire onResourceUnload event - final cleanup before environment is cleared
        if (env) {
            sol::object handlerObj = (*env)["onResourceUnload"];
            if (handlerObj.valid() && handlerObj.is<sol::protected_function>()) {
                sol::protected_function handler = handlerObj.as<sol::protected_function>();
                sol::protected_function_result result = handler();
                if (!result.valid()) {
                    sol::error err = result;
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] onResourceUnload error: {}", name, err.what());
                }
            }
        }

        // Clear environment
        if (env) {
            EnvironmentSandbox::ClearEnvironment(*env);
        }
        resource->SetEnvironment(nullptr);

        // Clear context
        SetCurrentResourceContext("");

        // Transition to Stopped state
        oldState = resource->GetState();
        resource->TransitionTo(ResourceState::Stopped);
        FireOnResourceStateChanged(name, oldState, ResourceState::Stopped);

        // Fire C++ callback
        FireOnResourceStopped(name);

        BroadcastResourceAwarenessEvent("onResourceStopped", name);

        auto dependents = GetDependents(name);
        for (const auto &dep : dependents) {
            if (IsResourceRunning(dep)) {
                std::vector<sol::object> args;
                args.push_back(sol::make_object(*_luaState, name));
                FireResourceLifecycleEventInternal(dep, "onDependencyLost", args);
            }
        }

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
        Resource *resource = GetResourceMutable(name);
        if (!resource) {
            return ResourceOperationResult::Failure("Resource not found: " + name);
        }

        // Store the resource path before stopping
        std::string resourcePath = resource->GetPath();
        bool wasRunning          = resource->IsRunning();

        // Stop the resource (this will preserve state via onResourceStop)
        if (wasRunning) {
            auto stopResult = StopResource(name);
            if (!stopResult.success) {
                return stopResult;
            }
        }

        // Re-read the manifest from disk
        // Preserve the original resource before removing it from the map
        std::unique_ptr<Resource> preservedResource;
        {
            std::lock_guard<std::mutex> lock(_resourcesMutex);
            auto it = _resources.find(name);
            if (it != _resources.end()) {
                preservedResource = std::move(it->second);
                _resources.erase(it);
            }
        }

        // Re-discover the resource (will re-parse manifest)
        auto newResource = std::make_unique<Resource>(resourcePath);
        if (!newResource->IsManifestValid()) {
            // Restore the original resource
            if (preservedResource) {
                std::lock_guard<std::mutex> lock(_resourcesMutex);
                _resources[name] = std::move(preservedResource);
            }
            return ResourceOperationResult::Failure("Failed to reload resource '" + name + "': invalid manifest");
        }

        // Check if the name changed in the manifest
        std::string newName = newResource->GetName();
        if (newName != name) {
            // Clear preserved state for the old name since name changed
            {
                std::lock_guard<std::mutex> lock(_preservedStatesMutex);
                auto stateIt = _preservedStates.find(name);
                if (stateIt != _preservedStates.end()) {
                    _preservedStates[newName] = stateIt->second;
                    _preservedStates.erase(stateIt);
                }
            }
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Resource name changed during reload: '{}' -> '{}'", name, newName);
        }

        // Add the new resource to the registry
        {
            std::lock_guard<std::mutex> lock(_resourcesMutex);
            _resources[newName] = std::move(newResource);
        }

        // Rebuild dependency graph
        BuildDependencyGraph();

        // Restart the resource if it was running
        if (wasRunning) {
            auto startResult = StartResource(newName);
            if (!startResult.success) {
                return startResult;
            }

            FireResourceLifecycleEventInternal(newName, "onResourceReload", {});
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Reloaded resource: {}", newName);
        return ResourceOperationResult::Success({newName});
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

    sol::table ResourceManager::GetResourceInfo(sol::state_view luaState, const std::string &name) const {
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

        // Track the export call chain for debugging
        std::string caller = GetCurrentResourceContext();
        if (!caller.empty()) {
            // Check for potential infinite loop
            if (!PushExportCall(caller, resourceName, exportName)) {
                // Cycle detected or max depth reached - error was already logged
                return sol::nil;
            }
        }

        sol::object result = resource->GetExport(exportName);

        // Pop the call from the chain if we pushed it
        if (!caller.empty()) {
            PopExportCall();
        }

        return result;
    }

    std::vector<std::string> ResourceManager::ListExports(const std::string &resourceName) const {
        const Resource *resource = GetResource(resourceName);
        if (!resource) {
            return {};
        }
        return resource->GetRegisteredExportNames();
    }

    // Export Call Chain Tracking

    bool ResourceManager::PushExportCall(const std::string &callerResource, const std::string &targetResource, const std::string &exportName) const {
        std::lock_guard<std::mutex> lock(_exportCallChainMutex);

        // Check for max depth
        if (_exportCallChain.size() >= kMaxExportCallDepth) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "Export call depth exceeded maximum ({}) - possible infinite loop detected.\nCall chain:\n{}",
                kMaxExportCallDepth, FormatExportCallChain());
            return false;
        }

        // Check for cycle (same target+export already in chain indicates recursion)
        if (HasCycleInExportCallChain(targetResource, exportName)) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "Infinite export loop detected! '{}' is calling '{}:{}' which creates a cycle.\nCall chain:\n{}",
                callerResource, targetResource, exportName, FormatExportCallChain());
            return false;
        }

        _exportCallChain.push_back({callerResource, targetResource, exportName});
        return true;
    }

    void ResourceManager::PopExportCall() const {
        std::lock_guard<std::mutex> lock(_exportCallChainMutex);
        if (!_exportCallChain.empty()) {
            _exportCallChain.pop_back();
        }
    }

    bool ResourceManager::HasCycleInExportCallChain(const std::string &targetResource, const std::string &exportName) const {
        // Check if this target+export combination already exists in the call chain
        // This would indicate a cycle (A calls B calls C calls A scenario)
        for (const auto &entry : _exportCallChain) {
            if (entry.targetResource == targetResource && entry.exportName == exportName) {
                return true;
            }
        }
        return false;
    }

    std::string ResourceManager::GetExportCaller() const {
        std::lock_guard<std::mutex> lock(_exportCallChainMutex);
        if (_exportCallChain.empty()) {
            return "";
        }
        return _exportCallChain.back().callerResource;
    }

    std::vector<ExportCallEntry> ResourceManager::GetExportCallChain() const {
        std::lock_guard<std::mutex> lock(_exportCallChainMutex);
        return _exportCallChain;
    }

    size_t ResourceManager::GetExportCallDepth() const {
        std::lock_guard<std::mutex> lock(_exportCallChainMutex);
        return _exportCallChain.size();
    }

    bool ResourceManager::IsInExportCall() const {
        std::lock_guard<std::mutex> lock(_exportCallChainMutex);
        return !_exportCallChain.empty();
    }

    std::string ResourceManager::FormatExportCallChain() const {
        // Note: Caller must hold _exportCallChainMutex or call this from a context where it's safe
        if (_exportCallChain.empty()) {
            return "  (empty)";
        }

        std::string result;
        for (size_t i = 0; i < _exportCallChain.size(); ++i) {
            const auto &entry = _exportCallChain[i];
            result += fmt::format("  [{}] {} -> {}:{}\n", i + 1, entry.callerResource, entry.targetResource, entry.exportName);
        }
        return result;
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
        // Lock both mutexes using scoped_lock to ensure consistent lock ordering
        // and avoid potential deadlocks. scoped_lock uses a deadlock-avoidance algorithm.
        std::scoped_lock lock(_resourcesMutex, _graphMutex);
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
                        // During incremental discovery, dependencies may not exist yet.
                        // The real validation happens when starting resources.
                        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Dependency '{}' for resource '{}' not yet discovered", dep.name, pair.first);
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

        // Apply sandboxing based on context
        if (_config.isClient) {
            // Client: full lockdown, disable all dangerous functions including require
            EnvironmentSandbox::SetupClientSandbox(*env);
        } else {
            // Server: allow require but scoped to the resources path
            EnvironmentSandbox::SetupServerSandbox(*_luaState, *env, _config.resourcesPath);
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

    bool ResourceManager::RegisterExport(const std::string &exportName, sol::object value) {
        Resource *resource = GetCurrentResource();
        if (!resource) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Cannot register export '{}': no current resource context", exportName);
            return false;
        }

        return resource->RegisterExport(exportName, value);
    }

    void ResourceManager::BroadcastGlobalEvent(const std::string &eventName, sol::variadic_args args) {
        // Copy handlers while holding the lock, then release before invoking
        // This avoids deadlocks from calling IsResourceRunning/context methods under lock
        std::map<std::string, std::vector<sol::protected_function>> handlersCopy;
        {
            std::lock_guard<std::mutex> lock(_globalEventsMutex);

            auto it = _globalEventHandlers.find(eventName);
            if (it == _globalEventHandlers.end()) {
                return;
            }

            handlersCopy = it->second;
        }

        // Iterate through all resources that have handlers for this event
        for (auto &resourcePair : handlersCopy) {
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

        // Copy handlers while holding the lock, then release before invoking
        // to avoid deadlock with _contextMutex
        std::vector<sol::protected_function> handlersCopy;
        {
            std::lock_guard<std::mutex> lock(_targetedEventsMutex);

            auto resourceIt = _targetedEventHandlers.find(targetResource);
            if (resourceIt == _targetedEventHandlers.end()) {
                return false;
            }

            auto eventIt = resourceIt->second.find(eventName);
            if (eventIt == resourceIt->second.end()) {
                return false;
            }

            handlersCopy = eventIt->second;
        }

        // Save current context and set to target resource
        std::string previousContext = GetCurrentResourceContext();
        SetCurrentResourceContext(targetResource);

        for (auto &handler : handlersCopy) {
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

        // Copy handler while holding the lock, then release before invoking
        // to avoid deadlock with _contextMutex
        sol::protected_function handlerCopy;
        {
            std::lock_guard<std::mutex> lock(_messageHandlersMutex);

            auto resourceIt = _messageHandlers.find(targetResource);
            if (resourceIt == _messageHandlers.end()) {
                return;
            }

            auto handlerIt = resourceIt->second.find(messageType);
            if (handlerIt == resourceIt->second.end()) {
                return;
            }

            handlerCopy = handlerIt->second;
        }

        // Save current context and set to target resource
        std::string previousContext = GetCurrentResourceContext();
        SetCurrentResourceContext(targetResource);

        // For fire-and-forget, use cached no-op reply function
        if (!_noOpReplyFn.valid()) {
            _noOpReplyFn = _luaState->load("return function() end")().get<sol::protected_function>();
        }

        sol::protected_function_result result = handlerCopy(payload, _noOpReplyFn);
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

        // RAII guard to ensure cleanup on all exit paths (normal return or exception)
        auto cleanupGuard = [luaState = _luaState, replyFnName, previousContext, this]() {
            (*luaState)[replyFnName] = sol::nil;
            SetCurrentResourceContext(previousContext);
        };
        struct ScopeGuard {
            std::function<void()> cleanup;
            ~ScopeGuard() { cleanup(); }
        } guard{cleanupGuard};

        sol::protected_function_result result = handler(payload, replyFn);
        if (!result.valid()) {
            sol::error err = result;
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Request handler '{}' error: {}", targetResource, messageType, err.what());
        }
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

            // RAII guard to ensure context is restored on all exit paths (normal return or exception)
            auto restoreContext = [previousContext, this]() {
                SetCurrentResourceContext(previousContext);
            };
            struct ScopeGuard {
                std::function<void()> cleanup;
                ~ScopeGuard() { cleanup(); }
            } guard{restoreContext};

            sol::protected_function_result result = pending.callback(response.response);
            if (!result.valid()) {
                sol::error err = result;
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Request callback error: {}", pending.sourceResource, err.what());
            }
        }
    }

    bool ResourceManager::FireResourceLifecycleEventInternal(const std::string &resourceName, const std::string &eventName, std::vector<sol::object> args) {
        Resource *resource = GetResourceMutable(resourceName);
        if (!resource) {
            return false;
        }

        sol::environment *env = resource->GetEnvironment();
        if (!env) {
            return false;
        }

        // Look for the event handler in the resource's environment
        sol::object handlerObj = (*env)[eventName];
        if (!handlerObj.valid() || !handlerObj.is<sol::protected_function>()) {
            // No handler registered, that's okay
            return true;
        }

        sol::protected_function handler = handlerObj.as<sol::protected_function>();

        // Save and set context
        std::string previousContext = GetCurrentResourceContext();
        SetCurrentResourceContext(resourceName);

        // Call the handler with provided arguments
        sol::protected_function_result result;
        switch (args.size()) {
        case 0: result = handler(); break;
        case 1: result = handler(args[0]); break;
        case 2: result = handler(args[0], args[1]); break;
        case 3: result = handler(args[0], args[1], args[2]); break;
        default:
            // For more arguments, use sol's table unpacking
            sol::table argsTable = _luaState->create_table();
            for (size_t i = 0; i < args.size(); ++i) {
                argsTable[i + 1] = args[i];
            }
            result = handler(sol::as_args(argsTable));
            break;
        }

        // Restore context
        SetCurrentResourceContext(previousContext);

        if (!result.valid()) {
            sol::error err = result;
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Lifecycle event '{}' error: {}", resourceName, eventName, err.what());
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] Fired lifecycle event '{}'", resourceName, eventName);
        return true;
    }

    bool ResourceManager::FireResourceLifecycleEvent(const std::string &resourceName, const std::string &eventName, sol::variadic_args args) {
        std::vector<sol::object> argsVec;
        for (auto arg : args) {
            argsVec.push_back(arg);
        }
        return FireResourceLifecycleEventInternal(resourceName, eventName, argsVec);
    }

    bool ResourceManager::FireResourceLifecycleEventWithState(const std::string &resourceName, const std::string &eventName, sol::object state) {
        std::vector<sol::object> args;
        if (state.valid() && state != sol::nil) {
            args.push_back(state);
        }
        return FireResourceLifecycleEventInternal(resourceName, eventName, args);
    }

    void ResourceManager::BroadcastResourceAwarenessEvent(const std::string &eventName, const std::string &affectedResourceName) {
        // Get all running resources
        std::vector<std::string> runningResources = GetRunningResourceNames();

        for (const auto &resourceName : runningResources) {
            // Don't notify the resource about itself
            if (resourceName == affectedResourceName) {
                continue;
            }

            // Fire the event with the affected resource name as argument
            std::vector<sol::object> args;
            args.push_back(sol::make_object(*_luaState, affectedResourceName));
            FireResourceLifecycleEventInternal(resourceName, eventName, args);
        }
    }

    bool ResourceManager::HasPreservedState(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_preservedStatesMutex);
        return _preservedStates.find(name) != _preservedStates.end();
    }

    void ResourceManager::ClearPreservedState(const std::string &name) {
        std::lock_guard<std::mutex> lock(_preservedStatesMutex);
        _preservedStates.erase(name);
    }

    void ResourceManager::ClearAllPreservedStates() {
        std::lock_guard<std::mutex> lock(_preservedStatesMutex);
        _preservedStates.clear();
    }

    sol::object ResourceManager::GetPreservedState(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_preservedStatesMutex);
        auto it = _preservedStates.find(name);
        if (it != _preservedStates.end()) {
            return it->second;
        }
        return sol::nil;
    }

    void ResourceManager::HandleResourceRuntimeError(const std::string &resourceName, const std::string &error, const std::string &stackTrace) {
        Resource *resource = GetResourceMutable(resourceName);
        if (!resource) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Cannot handle error for unknown resource: {}", resourceName);
            return;
        }

        // Log the error with resource tag
        if (stackTrace.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Runtime error: {}", resourceName, error);
        } else {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Runtime error: {}\n    Stack trace:\n{}", resourceName, error, stackTrace);
        }

        // Fire error callback
        FireOnResourceError(resourceName, error);

        // Handle based on error behavior from manifest
        const auto &manifest = resource->GetManifest();
        switch (manifest.errorBehavior) {
        case ResourceErrorBehavior::Continue:
            // Just log the error, keep resource running
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("[{}] Error behavior: continue - resource keeps running", resourceName);
            break;

        case ResourceErrorBehavior::Stop:
            // Stop the resource
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("[{}] Error behavior: stop - stopping resource", resourceName);
            StopResource(resourceName);
            break;

        case ResourceErrorBehavior::Restart:
            // Stop and schedule restart
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("[{}] Error behavior: restart - scheduling auto-restart", resourceName);
            StopResource(resourceName);
            ScheduleAutoRestart(resourceName);
            break;
        }
    }

    bool ResourceManager::ScheduleAutoRestart(const std::string &resourceName) {
        Resource *resource = GetResourceMutable(resourceName);
        if (!resource) {
            return false;
        }

        // Check if auto-restart is allowed
        if (!resource->CanAutoRestart()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("[{}] Auto-restart denied: max attempts exceeded in time window", resourceName);

            // Fire onDependencyLost to dependents since we're not restarting
            auto dependents = GetDependents(resourceName);
            for (const auto &dep : dependents) {
                if (IsResourceRunning(dep)) {
                    std::vector<sol::object> args;
                    args.push_back(sol::make_object(*_luaState, resourceName));
                    FireResourceLifecycleEventInternal(dep, "onDependencyLost", args);
                }
            }

            return false;
        }

        // Record the restart attempt
        resource->RecordRestartAttempt();

        // Calculate backoff delay
        int backoffMs = resource->GetRestartBackoffMs();

        // Schedule the restart
        auto restartTime = std::chrono::system_clock::now() + std::chrono::milliseconds(backoffMs);

        {
            std::lock_guard<std::mutex> lock(_scheduledRestartsMutex);

            // Remove any existing scheduled restart for this resource
            _scheduledRestarts.erase(
                std::remove_if(_scheduledRestarts.begin(), _scheduledRestarts.end(),
                    [&resourceName](const ScheduledRestart &sr) {
                        return sr.resourceName == resourceName;
                    }),
                _scheduledRestarts.end());

            // Add new scheduled restart
            ScheduledRestart sr;
            sr.resourceName  = resourceName;
            sr.scheduledTime = restartTime;
            _scheduledRestarts.push_back(sr);
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("[{}] Auto-restart scheduled in {}ms (attempt {})",
            resourceName, backoffMs, resource->GetRestartAttemptCount());

        return true;
    }

    void ResourceManager::ProcessScheduledRestarts() {
        auto now = std::chrono::system_clock::now();
        std::vector<std::string> resourcesToRestart;

        {
            std::lock_guard<std::mutex> lock(_scheduledRestartsMutex);

            // Find restarts that are due
            for (auto it = _scheduledRestarts.begin(); it != _scheduledRestarts.end();) {
                if (it->scheduledTime <= now) {
                    resourcesToRestart.push_back(it->resourceName);
                    it = _scheduledRestarts.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Process the restarts outside the lock
        for (const auto &resourceName : resourcesToRestart) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("[{}] Executing scheduled auto-restart", resourceName);

            auto result = StartResource(resourceName);
            if (result.success) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("[{}] Auto-restart successful", resourceName);

                // Clear restart attempts on successful start
                Resource *resource = GetResourceMutable(resourceName);
                if (resource) {
                    resource->ClearRestartAttempts();
                }
            } else {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Auto-restart failed: {}", resourceName, result.error);

                // Try to reschedule if allowed
                ScheduleAutoRestart(resourceName);
            }
        }
    }

    void ResourceManager::ClearResourceRestartAttempts(const std::string &name) {
        Resource *resource = GetResourceMutable(name);
        if (!resource) {
            return;
        }
        resource->ClearRestartAttempts();
    }

} // namespace Framework::Scripting
