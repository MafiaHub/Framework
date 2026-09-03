/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "resource_manager.h"

#include "../builtins/events.h"
#include "../builtins/messages.h"

#include <utils/vfs.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <queue>
#include <stack>
#include <thread>

namespace Framework::Scripting {
    namespace {
        enum class LifecyclePromiseStatus {
            Fulfilled,
            Rejected,
            TimedOut,
        };

        struct LifecyclePromiseResult {
            LifecyclePromiseStatus status = LifecyclePromiseStatus::Fulfilled;
            std::string error;
        };

        std::string StringifyPromiseRejection(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> reason) {
            v8::TryCatch tryCatch(isolate);
            const auto stringify = [&](v8::Local<v8::Value> value) {
                v8::Local<v8::String> text;
                if (value.IsEmpty() || !value->ToString(context).ToLocal(&text)) {
                    tryCatch.Reset();
                    return std::string("Promise rejected");
                }
                v8::String::Utf8Value utf8(isolate, text);
                return *utf8 ? std::string(*utf8, utf8.length()) : std::string("Promise rejected");
            };

            std::string result = stringify(reason);
            if (!reason.IsEmpty() && reason->IsObject()) {
                v8::Local<v8::Value> errorsValue;
                if (reason.As<v8::Object>()->Get(context, v8::String::NewFromUtf8Literal(isolate, "errors")).ToLocal(&errorsValue) && errorsValue->IsArray()) {
                    auto errors = errorsValue.As<v8::Array>();
                    for (uint32_t i = 0; i < errors->Length(); ++i) {
                        v8::Local<v8::Value> item;
                        if (errors->Get(context, i).ToLocal(&item)) {
                            result += i == 0 ? ": " : "; ";
                            result += stringify(item);
                        }
                    }
                }
            }
            if (tryCatch.HasCaught()) {
                tryCatch.Reset();
            }
            return result;
        }

        // Lifecycle entry points remain synchronous to their C++ callers, but the JS work they gate
        // is asynchronous. Pump the shared scripting engine so microtasks, timers, and Node/libuv I/O
        // can make progress; a plain blocking wait here would deadlock every meaningful async handler.
        LifecyclePromiseResult AwaitLifecyclePromise(Engine *engine, Builtins::Events &events, v8::Global<v8::Promise> &promise, int timeoutMs) {
            if (!engine || !engine->IsInitialized() || promise.IsEmpty()) {
                return {};
            }

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(timeoutMs, 1));
            for (;;) {
                {
                    v8::Isolate *isolate = engine->GetIsolate();
                    v8::Locker locker(isolate);
                    v8::Isolate::Scope isolateScope(isolate);
                    v8::HandleScope handleScope(isolate);
                    v8::Local<v8::Context> context = engine->GetContext();
                    v8::Context::Scope contextScope(context);
                    auto local = promise.Get(isolate);
                    if (local.IsEmpty()) {
                        promise.Reset();
                        return {};
                    }
                    if (local->State() == v8::Promise::PromiseState::kFulfilled) {
                        promise.Reset();
                        return {};
                    }
                    if (local->State() == v8::Promise::PromiseState::kRejected) {
                        auto error = StringifyPromiseRejection(isolate, context, local->Result());
                        promise.Reset();
                        return {LifecyclePromiseStatus::Rejected, std::move(error)};
                    }
                }

                if (std::chrono::steady_clock::now() >= deadline) {
                    v8::Isolate *isolate = engine->GetIsolate();
                    v8::Locker locker(isolate);
                    v8::Isolate::Scope isolateScope(isolate);
                    v8::HandleScope handleScope(isolate);
                    auto local = promise.Get(isolate);
                    if (!local.IsEmpty()) {
                        events.CancelPendingEmission(isolate, local);
                    }
                    promise.Reset();
                    return {LifecyclePromiseStatus::TimedOut, {}};
                }

                engine->Tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        bool IsPathInsideRoot(const std::filesystem::path &path, const std::filesystem::path &root) {
            auto canonicalRoot = std::filesystem::weakly_canonical(root);
            auto canonicalPath = std::filesystem::weakly_canonical(path);
            auto rootStr = canonicalRoot.string();
            auto pathStr = canonicalPath.string();

            // Path must start with root AND sit inside the directory
            // (not just sharing a prefix like /app/cache vs /app/cache-evil)
            if (pathStr.size() < rootStr.size() ||
                pathStr.compare(0, rootStr.size(), rootStr) != 0) {
                return false;
            }
            // If path is longer than root, ensure there's a separator boundary
            if (pathStr.size() > rootStr.size()) {
                char sep = std::filesystem::path::preferred_separator;
                if (pathStr[rootStr.size()] != sep && rootStr.back() != sep) {
                    return false;
                }
            }
            return true;
        }

        // Newest file mtime under a resource directory, in impl-defined clock
        // ticks (monotonic for comparison). 0 if the directory can't be scanned.
        int64_t ComputeResourceMTime(const std::string &path) {
            // Packaged resources have no files on disk to stat.
            if (Utils::Vfs::IsVirtualPath(path)) {
                return 0;
            }
            std::error_code ec;
            int64_t newest = 0;
            std::filesystem::recursive_directory_iterator it(
                path, std::filesystem::directory_options::skip_permission_denied, ec);
            const std::filesystem::recursive_directory_iterator end;
            if (ec) {
                return newest;
            }
            for (; it != end; it.increment(ec)) {
                if (ec) {
                    break;
                }
                // Skip large, non-hand-edited trees.
                if (it->is_directory(ec) && !ec) {
                    const auto dirName = it->path().filename().string();
                    if (dirName == "node_modules" || dirName == ".git") {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                if (!it->is_regular_file(ec) || ec) {
                    continue;
                }
                auto t = std::filesystem::last_write_time(it->path(), ec);
                if (ec) {
                    continue;
                }
                int64_t ticks = static_cast<int64_t>(t.time_since_epoch().count());
                if (ticks > newest) {
                    newest = ticks;
                }
            }
            return newest;
        }
    } // anonymous namespace

    ResourceManager::ResourceManager(Engine *jsEngine, const ResourceManagerConfig &config)
        : _config(config)
        , _jsEngine(jsEngine) {
        if (_jsEngine) {
            _jsEngine->SetResourceManager(this);
        }
    }

    ResourceManager::~ResourceManager() {
        StopAll();
        if (_jsEngine) {
            _jsEngine->SetResourceManager(nullptr);
        }
    }

    const ResourceManagerConfig &ResourceManager::GetConfig() const {
        return _config;
    }

    void ResourceManager::SetConfig(const ResourceManagerConfig &config) {
        _config = config;
    }

    size_t ResourceManager::DiscoverResources() {
        size_t count = 0;

        // Deduplicated by DiscoverResource.
        for (const auto &name : Utils::Vfs::Get().EnumerateDirectories(_config.resourcesPath)) {
            const std::string mountedPath = _config.resourcesPath + "/" + name;
            if (Utils::Vfs::Get().Contains(mountedPath + "/package.json")) {
                if (DiscoverResource(mountedPath)) {
                    ++count;
                }
            }
        }

        if (Utils::Vfs::IsVirtualPath(_config.resourcesPath)) {
            if (count == 0) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("No resources mounted under {}", _config.resourcesPath);
            }
            return count;
        }

        std::filesystem::path resourcesDir(_config.resourcesPath);
        std::error_code ec;
        if (!std::filesystem::exists(resourcesDir, ec) || ec) {
            if (count == 0) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Resources directory not found: {}", _config.resourcesPath);
            }
            return count;
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
            std::scoped_lock lock(_resourcesMutex);
            if (_resources.contains(name)) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Duplicate resource name: {}", name);
                return false;
            }

            _resources[name] = std::move(resource);
        }

        // Discovery keeps the graph current: mounted-package discovery and the client's hot-reload
        // path both start a resource straight after adding it, and an empty graph silently costs
        // them both dependency ordering.
        BuildDependencyGraph();

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Discovered JS resource: {} v{}", name, version);
        return true;
    }

    void ResourceManager::BuildDependencyGraph() {
        std::scoped_lock graphLock(_graphMutex);
        std::scoped_lock resourceLock(_resourcesMutex);

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

    bool ResourceManager::IsDependencyOptionalUnlocked(std::string_view name, std::string_view depName) const {
        auto it = _resources.find(name);
        if (it == _resources.end() || !it->second) {
            return false;
        }
        return it->second->IsOptionalDependency(depName);
    }

    bool ResourceManager::IsDependencyOptional(std::string_view name, std::string_view depName) const {
        std::scoped_lock resourceLock(_resourcesMutex);
        return IsDependencyOptionalUnlocked(name, depName);
    }

    bool ResourceManager::ValidateDependencies(std::string &outError) const {
        std::scoped_lock graphLock(_graphMutex);
        std::scoped_lock resourceLock(_resourcesMutex);

        for (const auto &[name, deps] : _dependencies) {
            for (const auto &depName : deps) {
                if (_resources.contains(depName)) {
                    continue;
                }

                if (IsDependencyOptionalUnlocked(name, depName)) {
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Resource '{}' skips missing optional dependency '{}'", name, depName);
                    continue;
                }

                if (_config.warnOnMissingDependency) {
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Resource '{}' depends on missing resource '{}'", name, depName);
                } else {
                    outError = "Resource '" + name + "' depends on missing resource '" + depName + "'";
                    return false;
                }
            }
        }
        return true;
    }

    std::vector<std::string> ResourceManager::ComputeLoadOrder() const {
        std::scoped_lock graphLock(_graphMutex);
        std::scoped_lock resourceLock(_resourcesMutex);

        std::vector<std::string> result;
        std::map<std::string, int> inDegree;
        std::queue<std::string> queue;

        // Initialize in-degrees (only count dependencies that exist in _resources)
        for (const auto &[name, _] : _resources) {
            int degree = 0;
            auto it = _dependencies.find(name);
            if (it != _dependencies.end()) {
                for (const auto &dep : it->second) {
                    if (_resources.contains(dep)) {
                        ++degree;
                    }
                }
            }
            inDegree[name] = degree;
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
            return ResourceOperationResult(depError);
        }

        auto loadOrder = ComputeLoadOrder();
        if (loadOrder.empty() && GetResourceCount() > 0) {
            return ResourceOperationResult("Dependency cycle detected");
        }

        std::vector<std::string> started;
        std::vector<std::string> failed;
        for (const auto &name : loadOrder) {
            auto result = StartResource(name);
            if (result) {
                started.push_back(name);
            } else {
                failed.push_back(name);
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to start resource {}: {}", name, result.GetError());
            }
        }

        // A partial start is still a failure; the started list rides along on the error.
        if (!failed.empty()) {
            std::string names;
            for (const auto &name : failed) {
                names += names.empty() ? name : ", " + name;
            }
            return ResourceOperationResult(std::to_string(failed.size()) + " of " + std::to_string(loadOrder.size()) + " resources failed to start: " + names, std::move(started));
        }

        return ResourceOperationResult::Ok(started);
    }

    ResourceOperationResult ResourceManager::StopAll() {
        auto loadOrder = GetLoadOrder();
        // Stop in reverse order
        std::reverse(loadOrder.begin(), loadOrder.end());

        std::vector<std::string> stopped;
        for (const auto &name : loadOrder) {
            if (IsResourceRunning(name)) {
                auto result = StopResource(name);
                if (result) {
                    stopped.push_back(name);
                }
            }
        }

        return ResourceOperationResult::Ok(stopped);
    }

    void ResourceManager::Reset() {
        const std::vector<std::string> resourceNames = GetAllResourceNames();
        StopAll();

        if (_jsEngine && _jsEngine->IsInitialized()) {
            v8::Isolate *isolate = _jsEngine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _jsEngine->GetContext();
            v8::Context::Scope contextScope(context);

            _events.Reset();
            for (const std::string &resourceName : resourceNames) {
                Builtins::Messages::CleanupResource(isolate, context, resourceName);
            }
            Builtins::Messages::Shutdown();
        }
        else {
            _events.Reset();
            Builtins::Messages::Shutdown();
        }

        {
            std::scoped_lock lock(_graphMutex, _resourcesMutex);
            _dependencies.clear();
            _dependents.clear();
            _resources.clear();
        }
        {
            std::scoped_lock lock(_contextMutex);
            _currentResourceContext.clear();
        }
        {
            std::scoped_lock lock(_scheduledRestartsMutex);
            _scheduledRestarts.clear();
        }
        _watchSnapshots.clear();
        _lastFileWatchPoll = {};
    }

    ResourceOperationResult ResourceManager::StartResource(std::string_view name) {
        Resource *resource = GetResourceMutable(name);
        if (!resource) {
            return ResourceOperationResult("Resource not found: " + std::string(name));
        }

        if (resource->IsRunning()) {
            return ResourceOperationResult::Ok({std::string(name)});
        }

        // Start dependencies first
        auto deps = GetDependencies(name);
        for (const auto &depName : deps) {
            // Uninstalled optional dependency: ValidateDependencies already let it through, so
            // recursing here would fail the dependent with "Resource not found".
            if (!HasResource(depName) && IsDependencyOptional(name, depName)) {
                continue;
            }

            if (!IsResourceRunning(depName)) {
                auto result = StartResource(depName);
                if (!result) {
                    return ResourceOperationResult("Failed to start dependency '" + depName + "': " + result.GetError());
                }
            }
        }

        // Transition to Loading
        if (!resource->TransitionTo(ResourceState::Loading)) {
            return ResourceOperationResult("Invalid state transition for resource: " + std::string(name));
        }

        // Execute the entry point script
        std::string error;
        if (!ExecuteResourceScript(*resource, error)) {
            CleanupResourceRuntime(*resource, name);
            resource->SetError(error);
            FireOnResourceError(std::string(name), error);
            return ResourceOperationResult(error);
        }

        // Emit resourceStart event (bypass running check since resource is still Loading)
        v8::Global<v8::Promise> startPromise;
        if (_jsEngine && _jsEngine->IsInitialized()) {
            v8::Isolate *isolate = _jsEngine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _jsEngine->GetContext();
            v8::Context::Scope contextScope(context);

            std::string nameStr(name);
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, nameStr));

            SetCurrentResourceContext(nameStr);
            auto emitted = _events.EmitReserved(isolate, context, "resourceStart", args);
            emitted->MarkAsHandled(); // C++ observes State()/Result(); avoid an unhandled-rejection echo
            startPromise.Reset(isolate, emitted);
            SetCurrentResourceContext("");
        }

        const auto startLifecycle = AwaitLifecyclePromise(_jsEngine, _events, startPromise, _config.resourceStartTimeoutMs);
        if (startLifecycle.status != LifecyclePromiseStatus::Fulfilled) {
            error = startLifecycle.status == LifecyclePromiseStatus::TimedOut ? "resourceStart timed out after " + std::to_string(std::max(_config.resourceStartTimeoutMs, 1)) + "ms" : "resourceStart rejected: " + startLifecycle.error;
            CleanupResourceRuntime(*resource, name);
            resource->SetError(error);
            FireOnResourceError(std::string(name), error);
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to start JS resource '{}': {}", name, error);
            return ResourceOperationResult(error);
        }

        // Transition to Running
        if (!resource->TransitionTo(ResourceState::Running)) {
            return ResourceOperationResult("Failed to transition to Running state");
        }

        resource->SetLoadTimestamp();
        resource->ClearRestartAttempts();

        FireOnResourceStarted(std::string(name));
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Started JS resource: {}", name);

        return ResourceOperationResult::Ok({std::string(name)});
    }

    ResourceOperationResult ResourceManager::StopResource(std::string_view name) {
        Resource *resource = GetResourceMutable(name);
        if (!resource) {
            return ResourceOperationResult("Resource not found: " + std::string(name));
        }

        if (!resource->IsRunning()) {
            return ResourceOperationResult::Ok({});
        }

        std::vector<std::string> stopped;

        // Stop dependents first if configured
        if (_config.cascadeStopDependents) {
            auto dependents = GetDependents(name);
            for (const auto &depName : dependents) {
                if (IsResourceRunning(depName)) {
                    auto result = StopResource(depName);
                    if (result) {
                        const auto &affected = result.GetValue();
                        stopped.insert(stopped.end(), affected.begin(), affected.end());
                    }
                }
            }
        }

        // Transition to Stopping
        resource->TransitionTo(ResourceState::Stopping);

        // Emit resourceStop event before cleanup
        v8::Global<v8::Promise> stopPromise;
        if (_jsEngine && _jsEngine->IsInitialized()) {
            v8::Isolate *isolate = _jsEngine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _jsEngine->GetContext();
            v8::Context::Scope contextScope(context);

            std::string nameStr(name);
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, nameStr));

            SetCurrentResourceContext(nameStr);
            auto emitted = _events.EmitReserved(isolate, context, "resourceStop", args);
            emitted->MarkAsHandled();
            stopPromise.Reset(isolate, emitted);
            SetCurrentResourceContext("");
        }

        const auto stopLifecycle = AwaitLifecyclePromise(_jsEngine, _events, stopPromise, _config.resourceStopTimeoutMs);
        if (stopLifecycle.status == LifecyclePromiseStatus::Rejected) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("resourceStop for '{}' rejected; forcing cleanup: {}", name, stopLifecycle.error);
        }
        else if (stopLifecycle.status == LifecyclePromiseStatus::TimedOut) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("resourceStop for '{}' timed out after {}ms; forcing cleanup", name, std::max(_config.resourceStopTimeoutMs, 1));
        }

        CleanupResourceRuntime(*resource, name);

        // Transition to Stopped
        resource->TransitionTo(ResourceState::Stopped);

        stopped.emplace_back(name);
        FireOnResourceStopped(std::string(name));
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Stopped JS resource: {}", name);

        return ResourceOperationResult::Ok(stopped);
    }

    ResourceOperationResult ResourceManager::RestartResource(std::string_view name) {
        // Evict the resource's modules between stop and start: in a shared
        // runtime a plain stop+start would re-run against stale caches and not
        // reload code. See docs/resource_hot_reload.md.
        std::string resourceRoot;
        {
            std::scoped_lock lock(_resourcesMutex);
            auto it = _resources.find(name);
            if (it != _resources.end() && it->second) {
                resourceRoot = it->second->GetPath();
            }
        }

        auto stopResult = StopResource(name);
        if (!stopResult) {
            return stopResult;
        }

        // Evict only after stop (engine cache contract).
        if (_jsEngine && !resourceRoot.empty()) {
            _jsEngine->EvictModulesUnderPath(resourceRoot);
        }

        // Restart the resource plus any dependents the stop cascaded down. If it
        // wasn't running, the stop reports nothing, so start the resource itself.
        auto toRestart = stopResult.GetValue();
        if (toRestart.empty()) {
            toRestart.push_back(std::string(name));
        }
        std::vector<std::string> affected;
        for (const auto &n : toRestart) {
            auto result = StartResource(n);
            if (result) {
                const auto &a = result.GetValue();
                affected.insert(affected.end(), a.begin(), a.end());
            }
        }
        return ResourceOperationResult::Ok(affected);
    }

    bool ResourceManager::HasDependencyCycle() const {
        // ComputeLoadOrder (Kahn's) drops nodes that never reach in-degree 0, so
        // a shorter result than the registry means a cycle.
        return ComputeLoadOrder().size() < GetResourceCount();
    }

    ResourceOperationResult ResourceManager::ReloadResource(std::string_view name) {
        return RestartResource(name);
    }

    ResourceOperationResult ResourceManager::RefreshResource(std::string_view name) {
        std::string path;
        bool wasRunning = false;
        {
            std::scoped_lock lock(_resourcesMutex);
            auto it = _resources.find(name);
            if (it == _resources.end() || !it->second) {
                return ResourceOperationResult("Resource not found: " + std::string(name));
            }
            path        = it->second->GetPath();
            wasRunning  = it->second->IsRunning();
        }

        // Stop first (cascades to dependents); remember what went down to
        // restart it all afterwards.
        std::vector<std::string> toRestart;
        if (wasRunning) {
            auto stopResult = StopResource(name);
            if (!stopResult) {
                return stopResult;
            }
            toRestart = stopResult.GetValue();
        }

        if (_jsEngine && !path.empty()) {
            _jsEngine->EvictModulesUnderPath(path);
        }

        // Re-parse package.json so manifest edits (deps, entry points) apply.
        auto reparsed = std::make_unique<Resource>(path);
        if (!reparsed->IsManifestValid()) {
            return ResourceOperationResult("Invalid package.json in " + path + ": " + reparsed->GetErrorMessage());
        }
        const std::string oldName = std::string(name);
        std::string newName       = reparsed->GetName();
        {
            std::scoped_lock lock(_resourcesMutex);
            if (newName != oldName && _resources.count(newName) > 0) {
                return ResourceOperationResult("Rename '" + oldName + "' -> '" + newName + "' collides with an existing resource");
            }
            if (newName != oldName) {
                _resources.erase(oldName);
            }
            _resources[newName] = std::move(reparsed);
        }
        BuildDependencyGraph();

        if (!wasRunning) {
            return ResourceOperationResult::Ok({newName});
        }

        // A manifest edit could introduce a cycle; bail before recursing into it.
        if (HasDependencyCycle()) {
            return ResourceOperationResult("Dependency cycle after reloading '" + newName + "'; left stopped");
        }

        // Restart the resource plus the dependents that cascaded down.
        std::vector<std::string> affected;
        for (const auto &stoppedName : toRestart) {
            const std::string startName = (stoppedName == oldName) ? newName : stoppedName;
            auto result = StartResource(startName);
            if (result) {
                const auto &a = result.GetValue();
                affected.insert(affected.end(), a.begin(), a.end());
            }
        }
        return ResourceOperationResult::Ok(affected);
    }

    ResourceOperationResult ResourceManager::RefreshAll() {
        // Remember who was running so we restart exactly them.
        auto running = GetRunningResourceNames();

        // Stop everything currently running (reverse dependency order).
        StopAll();

        // Evict + re-parse every known resource so code/manifest edits apply.
        // Track renames so we can restart the right names afterwards.
        std::map<std::string, std::string> renamed; // old -> new
        for (const auto &name : GetAllResourceNames()) {
            std::string path;
            {
                std::scoped_lock lock(_resourcesMutex);
                auto it = _resources.find(name);
                if (it != _resources.end() && it->second) {
                    path = it->second->GetPath();
                }
            }
            if (path.empty()) {
                continue;
            }
            if (_jsEngine) {
                _jsEngine->EvictModulesUnderPath(path);
            }
            auto reparsed = std::make_unique<Resource>(path);
            if (!reparsed->IsManifestValid()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)
                    ->warn("Skipping invalid package.json in {}: {}", path, reparsed->GetErrorMessage());
                continue;
            }
            std::string newName = reparsed->GetName();
            std::scoped_lock lock(_resourcesMutex);
            if (newName != name && _resources.count(newName) > 0) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)
                    ->warn("Skipping rename '{}' -> '{}': name already in use", name, newName);
                continue;
            }
            if (newName != name) {
                _resources.erase(name);
                renamed[name] = newName;
            }
            _resources[newName] = std::move(reparsed);
        }

        // Pick up brand-new resource directories, then rebuild the graph once.
        RescanResources();
        BuildDependencyGraph();

        if (HasDependencyCycle()) {
            return ResourceOperationResult("Dependency cycle after refresh; nothing restarted");
        }

        // Restart exactly what was running before (following any renames).
        std::vector<std::string> affected;
        for (const auto &name : running) {
            const auto it = renamed.find(name);
            const std::string &startName = (it != renamed.end()) ? it->second : name;
            auto result = StartResource(startName);
            if (result) {
                const auto &a = result.GetValue();
                affected.insert(affected.end(), a.begin(), a.end());
            }
        }
        return ResourceOperationResult::Ok(affected);
    }

    std::vector<std::string> ResourceManager::Rescan() {
        auto added = RescanResources();
        BuildDependencyGraph();
        return added;
    }

    std::vector<std::string> ResourceManager::RescanResources() {
        std::vector<std::string> added;

        std::error_code ec;
        std::filesystem::path resourcesDir(_config.resourcesPath);
        if (!std::filesystem::exists(resourcesDir, ec)) {
            return added;
        }

        for (const auto &entry : std::filesystem::directory_iterator(resourcesDir, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory()) {
                continue;
            }
            if (!std::filesystem::exists(entry.path() / "package.json")) {
                continue;
            }

            Resource probe(entry.path().string());
            if (!probe.IsManifestValid()) {
                continue;
            }

            bool exists;
            {
                std::scoped_lock lock(_resourcesMutex);
                exists = _resources.contains(probe.GetName());
            }
            if (!exists && DiscoverResource(entry.path().string())) {
                added.push_back(probe.GetName());
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)
                    ->info("Discovered new JS resource: {}", added.back());
            }
        }
        return added;
    }

    bool ResourceManager::ExecuteResourceScript(Resource &resource, std::string &outError) {
        if (!_jsEngine || !_jsEngine->IsInitialized()) {
            outError = "JavaScript engine not initialized";
            return false;
        }

        const auto scripts = _config.isClient ? resource.GetClientScripts() : resource.GetServerScripts();
        if (scripts.empty()) {
            return true;
        }

        const std::string resourceName = resource.GetName();
        const bool virtualRoot         = Utils::Vfs::IsVirtualPath(resource.GetPath());

        std::filesystem::path resourceRoot = std::filesystem::path(resource.GetPath());
        std::error_code ec;
        if (!virtualRoot) {
            resourceRoot = std::filesystem::weakly_canonical(resourceRoot, ec);
            if (ec) {
                outError = "Failed to resolve resource root path: " + resource.GetPath();
                return false;
            }
        }

        for (const auto &script : scripts) {
            std::string resolved;

            if (virtualRoot) {
                // Lexical: see Vfs::IsVirtualPath. PhysicsFS resolves nothing outside its mounts,
                // so a prefix test is the whole containment check.
                const std::string root = Utils::Vfs::NormalizeVirtual(resource.GetPath());
                resolved               = Utils::Vfs::NormalizeVirtual(script);
                if (root.empty() || resolved.empty() || !resolved.starts_with(root + "/")) {
                    outError = "Script escapes resource directory: " + script;
                    return false;
                }
                if (!Utils::Vfs::Get().Contains(resolved)) {
                    outError = "Script not found: " + resolved;
                    return false;
                }
            }
            else {
                const std::filesystem::path candidate = std::filesystem::weakly_canonical(std::filesystem::path(script), ec);
                if (ec || !IsPathInsideRoot(candidate, resourceRoot)) {
                    outError = "Script escapes resource directory: " + script;
                    return false;
                }
                if (!std::filesystem::exists(candidate)) {
                    outError = "Script not found: " + candidate.string();
                    return false;
                }
                if (!std::filesystem::is_regular_file(candidate)) {
                    outError = "Script is not a regular file: " + candidate.string();
                    return false;
                }
                resolved = candidate.string();
            }

            // Set resource context so Events.on() etc. know which resource is executing.
            SetCurrentResourceContext(resourceName);
            const bool ok = _jsEngine->ExecuteFile(resolved);
            SetCurrentResourceContext("");

            if (!ok) {
                outError = _jsEngine->GetLastError();
                return false;
            }
        }

        return true;
    }

    bool ResourceManager::CallResourceStop(std::string_view resourceName) {
        // Cleanup handlers before resource fully stops
        _events.CleanupResource(resourceName);

        // Reject and drop message requests tied to this resource. Rejecting the awaiting
        // Promises needs an active isolate/context, so scope one here.
        if (_jsEngine && _jsEngine->IsInitialized()) {
            v8::Isolate *isolate = _jsEngine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _jsEngine->GetContext();
            v8::Context::Scope contextScope(context);
            Builtins::Messages::CleanupResource(isolate, context, std::string(resourceName));
        }
        return true;
    }

    void ResourceManager::CleanupResourceRuntime(Resource &resource, std::string_view resourceName) {
        CallResourceStop(resourceName);
        if (_jsEngine) {
            _jsEngine->ClearResourceTimers(std::string(resourceName));
        }
        resource.ClearExports();
    }

    std::vector<std::string> ResourceManager::GetAllResourceNames() const {
        std::scoped_lock lock(_resourcesMutex);
        std::vector<std::string> names;
        names.reserve(_resources.size());
        for (const auto &[name, _] : _resources) {
            names.push_back(name);
        }
        return names;
    }

    std::vector<std::string> ResourceManager::GetRunningResourceNames() const {
        std::scoped_lock lock(_resourcesMutex);
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

    bool ResourceManager::HasResource(std::string_view name) const {
        std::scoped_lock lock(_resourcesMutex);
        return _resources.contains(name);
    }

    bool ResourceManager::IsResourceRunning(std::string_view name) const {
        std::scoped_lock lock(_resourcesMutex);
        auto it = _resources.find(name);
        return it != _resources.end() && it->second->IsRunning();
    }

    ResourceState ResourceManager::GetResourceState(std::string_view name) const {
        std::scoped_lock lock(_resourcesMutex);
        auto it = _resources.find(name);
        if (it == _resources.end()) {
            return ResourceState::Unloaded;
        }
        return it->second->GetState();
    }

    const Resource *ResourceManager::GetResource(std::string_view name) const {
        std::scoped_lock lock(_resourcesMutex);
        auto it = _resources.find(name);
        return it != _resources.end() ? it->second.get() : nullptr;
    }

    Resource *ResourceManager::GetResourceMutable(std::string_view name) {
        std::scoped_lock lock(_resourcesMutex);
        auto it = _resources.find(name);
        return it != _resources.end() ? it->second.get() : nullptr;
    }

    std::set<std::string> ResourceManager::GetDependents(std::string_view name) const {
        std::scoped_lock lock(_graphMutex);
        auto it = _dependents.find(name);
        if (it == _dependents.end()) return {};
        return {it->second.begin(), it->second.end()};
    }

    std::set<std::string> ResourceManager::GetDependencies(std::string_view name) const {
        std::scoped_lock lock(_graphMutex);
        auto it = _dependencies.find(name);
        if (it == _dependencies.end()) return {};
        return {it->second.begin(), it->second.end()};
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

    Builtins::Events &ResourceManager::GetEvents() {
        return _events;
    }

    void ResourceManager::SetCurrentResourceContext(const std::string &name) {
        std::scoped_lock lock(_contextMutex);
        _currentResourceContext = name;
    }

    std::string ResourceManager::GetCurrentResourceContext() const {
        std::scoped_lock lock(_contextMutex);
        return _currentResourceContext;
    }

    std::string ResourceManager::GetResourceNameFromScriptPath(const std::string &scriptPath) const {
        std::string path = scriptPath;

        // Strip file:// or file:/// prefix if present
        const std::string filePrefix = "file://";
        if (path.starts_with(filePrefix)) {
            path = path.substr(filePrefix.size());
            // On Windows, file:///C:/... needs to strip one more slash
            // On Unix, file:///path... also has an extra slash to strip
            if (!path.empty() && path[0] == '/') {
#ifdef _WIN32
                // On Windows, check if it's file:///C:/ format
                if (path.size() > 2 && std::isalpha(path[1]) && path[2] == ':') {
                    path = path.substr(1); // Remove leading slash to get C:/...
                }
#endif
            }
        }

        if (Utils::Vfs::IsVirtualPath(_config.resourcesPath)) {
            const std::string root = Utils::Vfs::NormalizeVirtual(_config.resourcesPath) + "/";
            const std::string normalised = Utils::Vfs::NormalizeVirtual(path);
            if (normalised.size() > root.size() && normalised.compare(0, root.size(), root) == 0) {
                const size_t nameEnd = normalised.find('/', root.size());
                if (nameEnd != std::string::npos) {
                    const std::string resourceName = normalised.substr(root.size(), nameEnd - root.size());
                    if (GetResource(resourceName) != nullptr) {
                        return resourceName;
                    }
                }
            }
            return "";
        }

        // Get the configured resources root as a canonical path for matching
        std::filesystem::path resourcesRoot(_config.resourcesPath);
        std::error_code ec;
        auto canonicalRoot = std::filesystem::weakly_canonical(resourcesRoot, ec);
        if (ec) {
            // If we can't canonicalize, use the original path
            canonicalRoot = resourcesRoot;
        }
        std::string rootStr = canonicalRoot.string();
        // Ensure the root ends with a separator for proper matching
        if (!rootStr.empty() && rootStr.back() != '/' && rootStr.back() != '\\') {
#ifdef _WIN32
            rootStr += '\\';
#else
            rootStr += '/';
#endif
        }

        // Normalize the script path
        std::filesystem::path scriptFilePath(path);
        auto canonicalScript = std::filesystem::weakly_canonical(scriptFilePath, ec);
        if (ec) {
            canonicalScript = scriptFilePath;
        }
        std::string normalizedPath = canonicalScript.string();

        // Check if the script path starts with or contains the resources root
        size_t resourcesPos = normalizedPath.find(rootStr);
        if (resourcesPos != std::string::npos) {
            // Extract resource name (the directory immediately after the resources root)
            size_t nameStart = resourcesPos + rootStr.size();
            size_t nameEnd = normalizedPath.find_first_of("/\\", nameStart);
            if (nameEnd != std::string::npos) {
                std::string resourceName = normalizedPath.substr(nameStart, nameEnd - nameStart);
                // Verify this resource exists
                if (GetResource(resourceName) != nullptr) {
                    return resourceName;
                }
            }
        }

        return "";
    }

    std::string ResourceManager::GetResourceNameFromFunction(v8::Isolate *isolate, v8::Local<v8::Function> fn) const {
        if (!isolate || fn.IsEmpty()) {
            return "";
        }

        v8::ScriptOrigin origin = fn->GetScriptOrigin();
        v8::Local<v8::Value> resourceName = origin.ResourceName();
        if (resourceName.IsEmpty() || !resourceName->IsString()) {
            return "";
        }

        v8::String::Utf8Value scriptPath(isolate, resourceName);
        if (!*scriptPath) {
            return "";
        }

        return GetResourceNameFromScriptPath(*scriptPath);
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

            std::string result = GetResourceNameFromScriptPath(*scriptPath);
            if (!result.empty()) {
                return result;
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
        std::scoped_lock lock(_resourcesMutex);
        return _resources.size();
    }

    size_t ResourceManager::GetRunningResourceCount() const {
        std::scoped_lock lock(_resourcesMutex);
        size_t count = 0;
        for (const auto &[_, resource] : _resources) {
            if (resource->IsRunning()) {
                ++count;
            }
        }
        return count;
    }

    void ResourceManager::OnEntityCreated(uint64_t networkId) {
        // The stack fallback touches V8; CreateEntity also fires for avatars outside a JS context.
        v8::Isolate *isolate = _jsEngine ? _jsEngine->GetIsolate() : nullptr;
        Resource *resource = (isolate && isolate->InContext()) ? GetCurrentResourceWithStackFallback(isolate) : GetCurrentResource();
        if (resource) {
            resource->TrackEntity(networkId);
        }
    }

    void ResourceManager::OnEntityDestroyed(uint64_t networkId) {
        std::scoped_lock lock(_resourcesMutex);
        for (auto &[name, resource] : _resources) {
            resource->UntrackEntity(networkId);
        }
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

        std::scoped_lock lock(_scheduledRestartsMutex);
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
            std::scoped_lock lock(_scheduledRestartsMutex);
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

    void ResourceManager::ProcessFileWatch() {
        if (!_config.devMode) {
            return;
        }

        // Throttle polls to the configured interval.
        const auto now = std::chrono::steady_clock::now();
        if (_lastFileWatchPoll.time_since_epoch().count() != 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - _lastFileWatchPoll).count();
            if (elapsed < _config.fileWatchIntervalMs) {
                return;
            }
        }
        _lastFileWatchPoll = now;

        for (const auto &name : GetRunningResourceNames()) {
            std::string path;
            {
                std::scoped_lock lock(_resourcesMutex);
                auto it = _resources.find(name);
                if (it != _resources.end() && it->second) {
                    path = it->second->GetPath();
                }
            }
            if (path.empty()) {
                continue;
            }

            const int64_t current = ComputeResourceMTime(path);
            auto snapIt = _watchSnapshots.find(name);
            if (snapIt == _watchSnapshots.end()) {
                // First time we've seen this resource: seed, don't reload.
                _watchSnapshots[name] = current;
                continue;
            }
            if (current > snapIt->second) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)
                    ->info("Detected change in resource '{}', hot-reloading", name);
                // Update before reloading so we don't re-trigger on it.
                snapIt->second = current;
                RefreshResource(name);
            }
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
