/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "resource.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <filesystem>

namespace Framework::Scripting {

    const char *ResourceStateToString(ResourceState state) {
        switch (state) {
            case ResourceState::Unloaded: return "unloaded";
            case ResourceState::Loading: return "loading";
            case ResourceState::Running: return "running";
            case ResourceState::Stopping: return "stopping";
            case ResourceState::Stopped: return "stopped";
            case ResourceState::Error: return "error";
            default: return "unknown";
        }
    }

    Resource::Resource(const std::string &path)
        : _path(path)
        , _stateTimestamp(std::chrono::system_clock::now()) {
        // Try to load the manifest
        std::filesystem::path manifestPath = std::filesystem::path(path) / "package.json";
        _manifestValid = _manifest.Parse(manifestPath.string());

        if (!_manifestValid) {
            _errorMessage = _manifest.GetError();
            _state = ResourceState::Error;
        }
    }

    Resource::~Resource() {
        ClearExports();
    }

    Resource::Resource(Resource &&other) noexcept
        : _path(std::move(other._path))
        , _manifest(std::move(other._manifest))
        , _manifestValid(other._manifestValid)
        , _state(other._state)
        , _errorMessage(std::move(other._errorMessage))
        , _stateTimestamp(other._stateTimestamp)
        , _loadTimestamp(other._loadTimestamp)
        , _isolate(other._isolate)
        , _exports(std::move(other._exports))
        , _restartAttempts(std::move(other._restartAttempts))
        , _ownedEntities(std::move(other._ownedEntities)) {
        other._isolate = nullptr;
    }

    Resource &Resource::operator=(Resource &&other) noexcept {
        if (this != &other) {
            ClearExports();

            _path = std::move(other._path);
            _manifest = std::move(other._manifest);
            _manifestValid = other._manifestValid;
            _state = other._state;
            _errorMessage = std::move(other._errorMessage);
            _stateTimestamp = other._stateTimestamp;
            _loadTimestamp = other._loadTimestamp;
            _isolate = other._isolate;
            _exports = std::move(other._exports);
            _restartAttempts = std::move(other._restartAttempts);
            _ownedEntities = std::move(other._ownedEntities);

            other._isolate = nullptr;
        }
        return *this;
    }

    const std::string &Resource::GetName() const {
        return _manifest.GetName();
    }

    const std::string &Resource::GetVersion() const {
        return _manifest.GetVersion();
    }

    const std::string &Resource::GetAuthor() const {
        return _manifest.GetAuthor();
    }

    const std::string &Resource::GetDescription() const {
        return _manifest.GetDescription();
    }

    const std::string &Resource::GetPath() const {
        return _path;
    }

    const PackageManifest &Resource::GetManifest() const {
        return _manifest;
    }

    bool Resource::HasExport(std::string_view exportName) const {
        const auto &exports = _manifest.GetMafiaHubConfig().exports;
        return std::find(exports.begin(), exports.end(), exportName) != exports.end();
    }

    bool Resource::DependsOn(std::string_view resourceName) const {
        const auto &deps = _manifest.GetMafiaHubConfig().resourceDependencies;
        for (const auto &dep : deps) {
            if (dep.name == resourceName) {
                return true;
            }
        }
        return false;
    }

    std::string Resource::GetServerEntryPoint() const {
        const auto &server = _manifest.GetMafiaHubConfig().server;
        if (server.empty()) {
            return "";
        }
        return (std::filesystem::path(_path) / server).string();
    }

    std::string Resource::GetClientEntryPoint() const {
        const auto &client = _manifest.GetMafiaHubConfig().client;
        if (client.empty()) {
            return "";
        }
        return (std::filesystem::path(_path) / client).string();
    }

    ResourceState Resource::GetState() const {
        return _state;
    }

    bool Resource::IsRunning() const {
        return _state == ResourceState::Running;
    }

    bool Resource::IsStopped() const {
        return _state == ResourceState::Stopped || _state == ResourceState::Unloaded;
    }

    bool Resource::HasError() const {
        return _state == ResourceState::Error;
    }

    bool Resource::IsManifestValid() const {
        return _manifestValid;
    }

    const std::string &Resource::GetErrorMessage() const {
        return _errorMessage;
    }

    std::chrono::system_clock::time_point Resource::GetStateTimestamp() const {
        return _stateTimestamp;
    }

    std::chrono::system_clock::time_point Resource::GetLoadTimestamp() const {
        return _loadTimestamp;
    }

    int Resource::GetRestartAttemptCount() const {
        std::scoped_lock lock(_restartAttemptsMutex);
        return GetRestartAttemptCountUnlocked();
    }

    int Resource::GetRestartAttemptCountUnlocked() const {
        // Count attempts within the last 5 minutes
        auto now = std::chrono::system_clock::now();
        auto windowStart = now - std::chrono::minutes(5);

        int count = 0;
        for (const auto &attempt : _restartAttempts) {
            if (attempt >= windowStart) {
                ++count;
            }
        }
        return count;
    }

    bool Resource::CanAutoRestart() const {
        const auto &config = _manifest.GetMafiaHubConfig();
        if (config.errorBehavior != "restart") {
            return false;
        }

        // Allow max 3 restarts within 5 minutes
        return GetRestartAttemptCount() < 3;
    }

    void Resource::RecordRestartAttempt() {
        std::scoped_lock lock(_restartAttemptsMutex);
        _restartAttempts.push_back(std::chrono::system_clock::now());

        // Prune old attempts (older than 10 minutes)
        auto cutoff = std::chrono::system_clock::now() - std::chrono::minutes(10);
        _restartAttempts.erase(
            std::remove_if(_restartAttempts.begin(), _restartAttempts.end(),
                [&cutoff](const auto &t) { return t < cutoff; }),
            _restartAttempts.end());
    }

    void Resource::ClearRestartAttempts() {
        std::scoped_lock lock(_restartAttemptsMutex);
        _restartAttempts.clear();
    }

    int Resource::GetRestartBackoffMs() const {
        int attempts = GetRestartAttemptCount();
        if (attempts == 0) {
            return 0;
        }
        // Exponential backoff: 1s, 2s, 4s, 8s, 16s max
        int delayMs = 1000 * (1 << std::min(attempts - 1, 4));
        return delayMs;
    }

    bool Resource::RegisterExport(std::string_view name, v8::Local<v8::Value> value) {
        if (!HasExport(name)) {
            return false;
        }

        if (!_isolate) {
            return false;
        }

        std::scoped_lock lock(_exportsMutex);
        _exports[std::string(name)].Reset(_isolate, value);
        return true;
    }

    void Resource::UnregisterExport(std::string_view name) {
        std::scoped_lock lock(_exportsMutex);
        auto it = _exports.find(name);
        if (it != _exports.end()) {
            it->second.Reset();
            _exports.erase(it);
        }
    }

    void Resource::ClearExports() {
        std::scoped_lock lock(_exportsMutex);
        for (auto &[name, global] : _exports) {
            global.Reset();
        }
        _exports.clear();
    }

    bool Resource::HasRegisteredExport(std::string_view name) const {
        std::scoped_lock lock(_exportsMutex);
        return _exports.contains(name);
    }

    std::vector<std::string> Resource::GetRegisteredExportNames() const {
        std::scoped_lock lock(_exportsMutex);
        std::vector<std::string> names;
        names.reserve(_exports.size());
        for (const auto &[name, _] : _exports) {
            names.push_back(name);
        }
        return names;
    }

    v8::Local<v8::Value> Resource::GetExportValue(std::string_view name) const {
        std::scoped_lock lock(_exportsMutex);
        auto it = _exports.find(name);
        if (it == _exports.end() || it->second.IsEmpty()) {
            return v8::Local<v8::Value>();
        }
        return it->second.Get(_isolate);
    }

    bool Resource::TransitionTo(ResourceState newState) {
        if (!IsValidTransition(_state, newState)) {
            return false;
        }

        _state = newState;
        _stateTimestamp = std::chrono::system_clock::now();

        if (newState == ResourceState::Stopped || newState == ResourceState::Error) {
            DestroyOwnedEntities();
        }

        if (newState != ResourceState::Error) {
            ClearError();
        }

        return true;
    }

    void Resource::TrackEntity(uint64_t networkId) {
        std::scoped_lock lock(_ownedEntitiesMutex);
        _ownedEntities.insert(networkId);
    }

    void Resource::UntrackEntity(uint64_t networkId) {
        std::scoped_lock lock(_ownedEntitiesMutex);
        _ownedEntities.erase(networkId);
    }

    void Resource::DestroyOwnedEntities() {
        // Swap out first: DestroyEntity re-enters UntrackEntity via the destroy hook.
        std::unordered_set<uint64_t> owned;
        {
            std::scoped_lock lock(_ownedEntitiesMutex);
            owned.swap(_ownedEntities);
        }

        auto *replication = CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        for (uint64_t networkId : owned) {
            if (auto *entity = replication->GetEntityByNetworkID(networkId)) {
                replication->DestroyEntity(entity);
            }
        }
    }

    void Resource::SetError(const std::string &error) {
        _errorMessage = error;
        TransitionTo(ResourceState::Error);
    }

    void Resource::ClearError() {
        _errorMessage.clear();
    }

    void Resource::SetLoadTimestamp() {
        _loadTimestamp = std::chrono::system_clock::now();
    }

    bool Resource::IsValidTransition(ResourceState from, ResourceState to) {
        switch (from) {
            case ResourceState::Unloaded:
                return to == ResourceState::Loading;

            case ResourceState::Loading:
                return to == ResourceState::Running ||
                       to == ResourceState::Error;

            case ResourceState::Running:
                return to == ResourceState::Stopping ||
                       to == ResourceState::Error;

            case ResourceState::Stopping:
                return to == ResourceState::Stopped ||
                       to == ResourceState::Error;

            case ResourceState::Stopped:
                return to == ResourceState::Loading;

            case ResourceState::Error:
                return to == ResourceState::Loading ||
                       to == ResourceState::Unloaded;

            default:
                return false;
        }
    }

} // namespace Framework::Scripting
