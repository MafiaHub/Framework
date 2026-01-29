#include "js_resource.h"

#include <filesystem>

namespace Framework::Scripting::JS {

    const char *ResourceStateToString(ResourceState state) {
        switch (state) {
            case ResourceState::Unloaded: return "Unloaded";
            case ResourceState::Loading: return "Loading";
            case ResourceState::Running: return "Running";
            case ResourceState::Stopping: return "Stopping";
            case ResourceState::Stopped: return "Stopped";
            case ResourceState::Error: return "Error";
            default: return "Unknown";
        }
    }

    JSResource::JSResource(const std::string &path)
        : _path(path)
        , _stateTimestamp(std::chrono::system_clock::now()) {
        // Try to load the manifest
        std::filesystem::path manifestPath = std::filesystem::path(path) / "package.json";
        _manifestValid = _manifest.Parse(manifestPath.string());

        if (!_manifestValid) {
            _errorMessage = _manifest.GetError();
        }
    }

    JSResource::~JSResource() {
        ClearExports();
    }

    JSResource::JSResource(JSResource &&other) noexcept
        : _path(std::move(other._path))
        , _manifest(std::move(other._manifest))
        , _manifestValid(other._manifestValid)
        , _state(other._state)
        , _errorMessage(std::move(other._errorMessage))
        , _stateTimestamp(other._stateTimestamp)
        , _loadTimestamp(other._loadTimestamp)
        , _isolate(other._isolate)
        , _exports(std::move(other._exports))
        , _restartAttempts(std::move(other._restartAttempts)) {
        other._isolate = nullptr;
    }

    JSResource &JSResource::operator=(JSResource &&other) noexcept {
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

            other._isolate = nullptr;
        }
        return *this;
    }

    const std::string &JSResource::GetName() const {
        return _manifest.GetName();
    }

    const std::string &JSResource::GetVersion() const {
        return _manifest.GetVersion();
    }

    const std::string &JSResource::GetAuthor() const {
        return _manifest.GetAuthor();
    }

    const std::string &JSResource::GetDescription() const {
        return _manifest.GetDescription();
    }

    const std::string &JSResource::GetPath() const {
        return _path;
    }

    const PackageManifest &JSResource::GetManifest() const {
        return _manifest;
    }

    bool JSResource::HasExport(const std::string &exportName) const {
        const auto &exports = _manifest.GetMafiaHubConfig().exports;
        return std::find(exports.begin(), exports.end(), exportName) != exports.end();
    }

    bool JSResource::DependsOn(const std::string &resourceName) const {
        const auto &deps = _manifest.GetMafiaHubConfig().resourceDependencies;
        for (const auto &dep : deps) {
            if (dep.name == resourceName) {
                return true;
            }
        }
        return false;
    }

    std::string JSResource::GetServerEntryPoint() const {
        const auto &server = _manifest.GetMafiaHubConfig().server;
        if (server.empty()) {
            return "";
        }
        return (std::filesystem::path(_path) / server).string();
    }

    std::string JSResource::GetClientEntryPoint() const {
        const auto &client = _manifest.GetMafiaHubConfig().client;
        if (client.empty()) {
            return "";
        }
        return (std::filesystem::path(_path) / client).string();
    }

    ResourceState JSResource::GetState() const {
        return _state;
    }

    bool JSResource::IsRunning() const {
        return _state == ResourceState::Running;
    }

    bool JSResource::IsStopped() const {
        return _state == ResourceState::Stopped;
    }

    bool JSResource::HasError() const {
        return _state == ResourceState::Error;
    }

    bool JSResource::IsManifestValid() const {
        return _manifestValid;
    }

    const std::string &JSResource::GetErrorMessage() const {
        return _errorMessage;
    }

    std::chrono::system_clock::time_point JSResource::GetStateTimestamp() const {
        return _stateTimestamp;
    }

    std::chrono::system_clock::time_point JSResource::GetLoadTimestamp() const {
        return _loadTimestamp;
    }

    int JSResource::GetRestartAttemptCount() const {
        std::lock_guard<std::mutex> lock(_restartAttemptsMutex);
        return GetRestartAttemptCountUnlocked();
    }

    int JSResource::GetRestartAttemptCountUnlocked() const {
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

    bool JSResource::CanAutoRestart() const {
        const auto &config = _manifest.GetMafiaHubConfig();
        if (config.errorBehavior != "restart") {
            return false;
        }

        // Allow max 3 restarts within 5 minutes
        return GetRestartAttemptCount() < 3;
    }

    void JSResource::RecordRestartAttempt() {
        std::lock_guard<std::mutex> lock(_restartAttemptsMutex);
        _restartAttempts.push_back(std::chrono::system_clock::now());

        // Prune old attempts (older than 10 minutes)
        auto cutoff = std::chrono::system_clock::now() - std::chrono::minutes(10);
        _restartAttempts.erase(
            std::remove_if(_restartAttempts.begin(), _restartAttempts.end(),
                [&cutoff](const auto &t) { return t < cutoff; }),
            _restartAttempts.end());
    }

    void JSResource::ClearRestartAttempts() {
        std::lock_guard<std::mutex> lock(_restartAttemptsMutex);
        _restartAttempts.clear();
    }

    int JSResource::GetRestartBackoffMs() const {
        int attempts = GetRestartAttemptCount();
        // Exponential backoff: 1s, 2s, 4s, 8s, 16s max
        int delayMs = 1000 * (1 << std::min(attempts, 4));
        return delayMs;
    }

    bool JSResource::RegisterExport(const std::string &name, v8::Local<v8::Value> value) {
        if (!HasExport(name)) {
            return false;
        }

        if (!_isolate) {
            return false;
        }

        std::lock_guard<std::mutex> lock(_exportsMutex);
        _exports[name].Reset(_isolate, value);
        return true;
    }

    void JSResource::UnregisterExport(const std::string &name) {
        std::lock_guard<std::mutex> lock(_exportsMutex);
        auto it = _exports.find(name);
        if (it != _exports.end()) {
            it->second.Reset();
            _exports.erase(it);
        }
    }

    void JSResource::ClearExports() {
        std::lock_guard<std::mutex> lock(_exportsMutex);
        for (auto &[name, global] : _exports) {
            global.Reset();
        }
        _exports.clear();
    }

    bool JSResource::HasRegisteredExport(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_exportsMutex);
        return _exports.find(name) != _exports.end();
    }

    std::vector<std::string> JSResource::GetRegisteredExportNames() const {
        std::lock_guard<std::mutex> lock(_exportsMutex);
        std::vector<std::string> names;
        names.reserve(_exports.size());
        for (const auto &[name, _] : _exports) {
            names.push_back(name);
        }
        return names;
    }

    bool JSResource::TransitionTo(ResourceState newState) {
        if (!IsValidTransition(_state, newState)) {
            return false;
        }

        _state = newState;
        _stateTimestamp = std::chrono::system_clock::now();

        if (newState != ResourceState::Error) {
            ClearError();
        }

        return true;
    }

    void JSResource::SetError(const std::string &error) {
        _errorMessage = error;
        TransitionTo(ResourceState::Error);
    }

    void JSResource::ClearError() {
        _errorMessage.clear();
    }

    void JSResource::SetLoadTimestamp() {
        _loadTimestamp = std::chrono::system_clock::now();
    }

    bool JSResource::IsValidTransition(ResourceState from, ResourceState to) {
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

} // namespace Framework::Scripting::JS
