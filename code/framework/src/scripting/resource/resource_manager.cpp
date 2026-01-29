#include "resource_manager.h"

#include "../builtins/events.h"

#include <algorithm>
#include <filesystem>
#include <queue>
#include <stack>

namespace {
    // Escapes a string for safe embedding in a JavaScript single-quoted string literal.
    // Handles: backslash, single quote, newline, carriage return, and tab.
    std::string EscapeForSingleQuotedJSString(const std::string &input) {
        std::string result;
        result.reserve(input.size() + input.size() / 8); // Pre-allocate with some margin

        for (char c : input) {
            switch (c) {
            case '\\': result += "\\\\"; break;
            case '\'': result += "\\'"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
            }
        }
        return result;
    }

    // Converts a filesystem path to a proper file:// URL.
    // - Uses forward slashes (generic_string)
    // - Windows: file:///C:/path (three slashes before drive letter)
    // - POSIX: file:///path (path already starts with /)
    std::string PathToFileURL(const std::filesystem::path &absPath) {
        std::string genericPath = absPath.generic_string();

        // Check for Windows drive letter (e.g., "C:/...")
        if (genericPath.size() >= 2 && std::isalpha(static_cast<unsigned char>(genericPath[0])) && genericPath[1] == ':') {
            // Windows path: needs file:/// prefix (three slashes, then drive letter)
            return "file:///" + genericPath;
        }

        // POSIX path: already starts with /, so file:// + path gives file:///path
        return "file://" + genericPath;
    }
} // anonymous namespace

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

        std::string name    = resource->GetName();
        std::string version = resource->GetVersion();

        {
            std::lock_guard<std::mutex> lock(_resourcesMutex);
            if (_resources.find(name) != _resources.end()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Duplicate resource name: {}", name);
                return false;
            }
            _resources[name] = std::move(resource);
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Discovered JS resource: {} v{}", name, version);
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
                    if (_config.warnOnMissingDependency) {
                        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Resource '{}' depends on missing resource '{}'", name, depName);
                    } else {
                        outError = "Resource '" + name + "' depends on missing resource '" + depName + "'";
                        return false;
                    }
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

        // For CommonJS modules, emit resourceStart immediately (synchronous loading)
        // For ES modules, the event is emitted from within the async loader wrapper
        if (!resource->GetManifest().IsESModule() && _jsEngine && _jsEngine->IsInitialized()) {
            v8::Isolate *isolate = _jsEngine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _jsEngine->GetContext();
            v8::Context::Scope contextScope(context);

            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, name));

            SetCurrentResourceContext(name);
            _events.EmitReserved(isolate, context, "resourceStart", args);
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
            _events.EmitReserved(isolate, context, "resourceStop", args);
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
        std::filesystem::path absPath = std::filesystem::absolute(entryPoint);
        std::string absPathStr = absPath.string();

        // Escape resource name for safe embedding in JS strings
        std::string escapedResourceName = EscapeForSingleQuotedJSString(resourceName);

        std::string code;
        if (resource.GetManifest().IsESModule()) {
            // ES Module: Load asynchronously, emit resourceStart after handlers are registered
            // Track pending load so we know when all modules are ready
            // Use proper file:// URL format and escape for JS string
            std::string fileUrl = PathToFileURL(absPath);
            std::string escapedFileUrl = EscapeForSingleQuotedJSString(fileUrl);

            IncrementPendingLoads();
            code =
                "(async function() {\n"
                "    try {\n"
                "        await import('" + escapedFileUrl + "');\n"
                "        Framework.__internal.emitResourceStart('" + escapedResourceName + "');\n"
                "    } catch (err) {\n"
                "        console.error('[" + escapedResourceName + "] Failed to load:', err.stack || err);\n"
                "        Framework.__internal.onLoadError('" + escapedResourceName + "');\n"
                "        throw err;\n"
                "    }\n"
                "})();\n";
        } else {
            // CommonJS: require() takes paths, use forward slashes and escape for JS string
            std::string genericPath = absPath.generic_string();
            std::string escapedPath = EscapeForSingleQuotedJSString(genericPath);
            code = "(function() { require('" + escapedPath + "'); })();\n";
        }

        bool result = _jsEngine->Execute(code, absPathStr);
        if (!result) {
            outError = _jsEngine->GetLastError();
        }
        return result;
    }

    bool ResourceManager::CallResourceStop(const std::string &resourceName) {
        // Cleanup handlers before resource fully stops
        _events.CleanupResource(resourceName);
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

    Events &ResourceManager::GetEvents() {
        return _events;
    }

    void ResourceManager::SetCurrentResourceContext(const std::string &name) {
        std::lock_guard<std::mutex> lock(_contextMutex);
        _currentResourceContext = name;
    }

    std::string ResourceManager::GetCurrentResourceContext() const {
        std::lock_guard<std::mutex> lock(_contextMutex);
        return _currentResourceContext;
    }

    std::string ResourceManager::GetResourceContextFromStack(v8::Isolate *isolate) const {
        if (!isolate) {
            return "";
        }

        // Get stack trace with up to 20 frames
        v8::Local<v8::StackTrace> stackTrace = v8::StackTrace::CurrentStackTrace(isolate, 20);
        if (stackTrace.IsEmpty()) {
            return "";
        }

        // Look for the first frame with a file path in the resources directory
        for (int i = 0; i < stackTrace->GetFrameCount(); ++i) {
            v8::Local<v8::StackFrame> frame = stackTrace->GetFrame(isolate, i);
            v8::Local<v8::String> scriptName = frame->GetScriptName();
            if (scriptName.IsEmpty()) {
                continue;
            }

            v8::String::Utf8Value scriptPath(isolate, scriptName);
            if (!*scriptPath) {
                continue;
            }

            std::string path(*scriptPath);

            // Check if path is in resources directory
            // Path format: .../resources/<resource-name>/...
            size_t resourcesPos = path.find("/resources/");
            if (resourcesPos == std::string::npos) {
                // Also try file:// URL format
                resourcesPos = path.find("file:///");
                if (resourcesPos != std::string::npos) {
                    path = path.substr(resourcesPos + 7); // Remove "file://"
                    resourcesPos = path.find("/resources/");
                }
            }

            if (resourcesPos != std::string::npos) {
                // Extract resource name
                size_t nameStart = resourcesPos + 11; // Skip "/resources/"
                size_t nameEnd = path.find('/', nameStart);
                if (nameEnd != std::string::npos) {
                    std::string resourceName = path.substr(nameStart, nameEnd - nameStart);
                    // Verify this resource exists
                    if (GetResource(resourceName) != nullptr) {
                        return resourceName;
                    }
                }
            }
        }

        return "";
    }

    Resource *ResourceManager::GetCurrentResource() {
        std::string name = GetCurrentResourceContext();
        if (name.empty()) {
            return nullptr;
        }
        return GetResourceMutable(name);
    }

    Resource *ResourceManager::GetCurrentResourceWithStackFallback(v8::Isolate *isolate) {
        std::string name = GetCurrentResourceContext();

        // If no context set (e.g., during async ES module loading), try to get from stack
        if (name.empty() && isolate) {
            name = GetResourceContextFromStack(isolate);
        }

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

    void ResourceManager::IncrementPendingLoads() {
        ++_pendingESModuleLoads;
    }

    void ResourceManager::DecrementPendingLoads() {
        --_pendingESModuleLoads;
        if (_pendingESModuleLoads == 0) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("All ES modules loaded");
        }
    }

    bool ResourceManager::HasPendingLoads() const {
        return _pendingESModuleLoads > 0;
    }

} // namespace Framework::Scripting
