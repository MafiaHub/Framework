#include "resource.h"

#include <logging/logger.h>
#include <utils/hashing.h>

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>

#include <algorithm>
#include <fstream>
#include <sstream>

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

    Resource::Resource(const std::string &path): _path(path), _stateTimestamp(std::chrono::system_clock::now()) {
        // Ensure path ends without separator
        if (!_path.empty() && (_path.back() == '/' || _path.back() == '\\')) {
            _path.pop_back();
        }

        // Load manifest
        std::string manifestPath = _path + "/manifest.json";
        auto result              = ResourceManifestParser::ParseFile(manifestPath);

        if (result.success) {
            _manifest      = std::move(result.manifest);
            _manifestValid = true;

            // Resolve script paths
            for (const auto &script : _manifest.serverFiles) {
                _serverScriptPaths.push_back(_path + "/" + script);
            }
            for (const auto &script : _manifest.clientFiles) {
                _clientScriptPaths.push_back(_path + "/" + script);
            }

            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Loaded resource manifest: {} v{}", _manifest.name, _manifest.version);
        } else {
            _manifestValid = false;
            _state         = ResourceState::Error;
            _errorMessage  = result.error;
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to load resource manifest from {}: {}", manifestPath, result.error);
        }
    }

    Resource::~Resource() {
        // Clear exports and event handlers
        ClearExports();
        ClearEventHandlers();
    }

    Resource::Resource(Resource &&other) noexcept
        : _path(std::move(other._path))
        , _manifest(std::move(other._manifest))
        , _manifestValid(other._manifestValid)
        , _state(other._state)
        , _errorMessage(std::move(other._errorMessage))
        , _stateTimestamp(other._stateTimestamp)
        , _loadTimestamp(other._loadTimestamp)
        , _environment(std::move(other._environment))
        , _eventHandlers(std::move(other._eventHandlers))
        , _exports(std::move(other._exports))
        , _serverScriptPaths(std::move(other._serverScriptPaths))
        , _clientScriptPaths(std::move(other._clientScriptPaths))
        , _restartAttempts(std::move(other._restartAttempts)) {
        other._manifestValid = false;
        other._state         = ResourceState::Unloaded;
    }

    Resource &Resource::operator=(Resource &&other) noexcept {
        if (this != &other) {
            _path              = std::move(other._path);
            _manifest          = std::move(other._manifest);
            _manifestValid     = other._manifestValid;
            _state             = other._state;
            _errorMessage      = std::move(other._errorMessage);
            _stateTimestamp    = other._stateTimestamp;
            _loadTimestamp     = other._loadTimestamp;
            _environment       = std::move(other._environment);
            _eventHandlers     = std::move(other._eventHandlers);
            _exports           = std::move(other._exports);
            _serverScriptPaths = std::move(other._serverScriptPaths);
            _clientScriptPaths = std::move(other._clientScriptPaths);
            _restartAttempts   = std::move(other._restartAttempts);

            other._manifestValid = false;
            other._state         = ResourceState::Unloaded;
        }
        return *this;
    }

    // Identity getters

    const std::string &Resource::GetName() const {
        return _manifest.name;
    }

    const std::string &Resource::GetVersion() const {
        return _manifest.version;
    }

    const std::string &Resource::GetAuthor() const {
        return _manifest.author;
    }

    const std::string &Resource::GetDescription() const {
        return _manifest.description;
    }

    const std::string &Resource::GetPath() const {
        return _path;
    }

    // Manifest access

    const ResourceManifest &Resource::GetManifest() const {
        return _manifest;
    }

    bool Resource::HasExport(const std::string &exportName) const {
        return _manifest.HasExport(exportName);
    }

    bool Resource::DependsOn(const std::string &resourceName) const {
        return _manifest.DependsOn(resourceName);
    }

    // State machine

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

    // Environment

    sol::environment *Resource::GetEnvironment() {
        return _environment.get();
    }

    const sol::environment *Resource::GetEnvironment() const {
        return _environment.get();
    }

    void Resource::SetEnvironment(std::unique_ptr<sol::environment> env) {
        _environment = std::move(env);
    }

    // Event handlers

    void Resource::RegisterEventHandler(const std::string &eventName, sol::protected_function handler) {
        std::lock_guard<std::mutex> lock(_eventHandlersMutex);
        _eventHandlers[eventName] = handler;
    }

    void Resource::UnregisterEventHandler(const std::string &eventName) {
        std::lock_guard<std::mutex> lock(_eventHandlersMutex);
        _eventHandlers.erase(eventName);
    }

    void Resource::ClearEventHandlers() {
        std::lock_guard<std::mutex> lock(_eventHandlersMutex);
        _eventHandlers.clear();
    }

    bool Resource::HasEventHandler(const std::string &eventName) const {
        std::lock_guard<std::mutex> lock(_eventHandlersMutex);
        return _eventHandlers.find(eventName) != _eventHandlers.end();
    }

    std::vector<std::string> Resource::GetEventNames() const {
        std::lock_guard<std::mutex> lock(_eventHandlersMutex);
        std::vector<std::string> names;
        names.reserve(_eventHandlers.size());
        for (const auto &pair : _eventHandlers) {
            names.push_back(pair.first);
        }
        return names;
    }

    // Exports

    bool Resource::RegisterExport(const std::string &name, sol::object value) {
        // Verify export is declared in manifest
        if (!_manifest.HasExport(name)) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Resource '{}' attempted to register undeclared export '{}'", _manifest.name, name);
            return false;
        }

        std::lock_guard<std::mutex> lock(_exportsMutex);
        _exports[name] = value;
        return true;
    }

    void Resource::UnregisterExport(const std::string &name) {
        std::lock_guard<std::mutex> lock(_exportsMutex);
        _exports.erase(name);
    }

    void Resource::ClearExports() {
        std::lock_guard<std::mutex> lock(_exportsMutex);
        _exports.clear();
    }

    sol::object Resource::GetExport(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_exportsMutex);
        auto it = _exports.find(name);
        if (it != _exports.end()) {
            return it->second;
        }
        return sol::nil;
    }

    bool Resource::HasRegisteredExport(const std::string &name) const {
        std::lock_guard<std::mutex> lock(_exportsMutex);
        return _exports.find(name) != _exports.end();
    }

    std::vector<std::string> Resource::GetRegisteredExportNames() const {
        std::lock_guard<std::mutex> lock(_exportsMutex);
        std::vector<std::string> names;
        names.reserve(_exports.size());
        for (const auto &pair : _exports) {
            names.push_back(pair.first);
        }
        return names;
    }

    // Scripts

    std::vector<std::string> Resource::GetServerScriptPaths() const {
        return _serverScriptPaths;
    }

    std::vector<std::string> Resource::GetClientScriptPaths() const {
        return _clientScriptPaths;
    }

    size_t Resource::GetScriptCount() const {
        return _serverScriptPaths.size() + _clientScriptPaths.size();
    }

    // State transitions

    bool Resource::TransitionTo(ResourceState newState) {
        if (!IsValidTransition(_state, newState)) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Invalid state transition for resource '{}': {} -> {}", _manifest.name, ResourceStateToString(_state), ResourceStateToString(newState));
            return false;
        }

        ResourceState oldState = _state;
        _state                 = newState;
        _stateTimestamp        = std::chrono::system_clock::now();

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Resource '{}' state: {} -> {}", _manifest.name, ResourceStateToString(oldState), ResourceStateToString(newState));

        return true;
    }

    void Resource::SetError(const std::string &error) {
        _errorMessage   = error;
        _state          = ResourceState::Error;
        _stateTimestamp = std::chrono::system_clock::now();
    }

    void Resource::ClearError() {
        _errorMessage.clear();
    }

    void Resource::SetLoadTimestamp() {
        _loadTimestamp = std::chrono::system_clock::now();
    }

    bool Resource::IsValidTransition(ResourceState from, ResourceState to) {
        // Define valid state transitions based on the state machine diagram
        switch (from) {
        case ResourceState::Unloaded:
            // Can only go to Loading
            return to == ResourceState::Loading;

        case ResourceState::Loading:
            // Can go to Running (success) or Error (failure)
            return to == ResourceState::Running || to == ResourceState::Error;

        case ResourceState::Running:
            // Can go to Stopping (normal stop) or Error (runtime error)
            return to == ResourceState::Stopping || to == ResourceState::Error;

        case ResourceState::Stopping:
            // Can only go to Stopped
            return to == ResourceState::Stopped;

        case ResourceState::Stopped:
            // Can go to Loading (restart) or Unloaded (cleanup)
            return to == ResourceState::Loading || to == ResourceState::Unloaded;

        case ResourceState::Error:
            // Can go to Loading (retry) or Unloaded (cleanup)
            return to == ResourceState::Loading || to == ResourceState::Unloaded;

        default: return false;
        }
    }

    int Resource::GetRestartAttemptCountUnlocked() const {
        if (!_manifest.autoRestart.enabled) {
            return 0;
        }

        // Count attempts within the time window
        auto now         = std::chrono::system_clock::now();
        auto windowStart = now - std::chrono::seconds(_manifest.autoRestart.timeWindowSeconds);

        int count = 0;
        for (const auto &timestamp : _restartAttempts) {
            if (timestamp >= windowStart) {
                count++;
            }
        }

        return count;
    }

    int Resource::GetRestartAttemptCount() const {
        std::lock_guard<std::mutex> lock(_restartAttemptsMutex);
        return GetRestartAttemptCountUnlocked();
    }

    bool Resource::CanAutoRestart() const {
        if (!_manifest.autoRestart.enabled) {
            return false;
        }

        std::lock_guard<std::mutex> lock(_restartAttemptsMutex);
        return GetRestartAttemptCountUnlocked() < _manifest.autoRestart.maxAttempts;
    }

    void Resource::RecordRestartAttempt() {
        std::lock_guard<std::mutex> lock(_restartAttemptsMutex);

        auto now = std::chrono::system_clock::now();
        _restartAttempts.push_back(now);

        // Clean up old attempts outside the time window
        auto windowStart = now - std::chrono::seconds(_manifest.autoRestart.timeWindowSeconds);
        _restartAttempts.erase(
            std::remove_if(_restartAttempts.begin(), _restartAttempts.end(),
                [&windowStart](const auto &timestamp) {
                    return timestamp < windowStart;
                }),
            _restartAttempts.end());
    }

    void Resource::ClearRestartAttempts() {
        std::lock_guard<std::mutex> lock(_restartAttemptsMutex);
        _restartAttempts.clear();
    }

    int Resource::GetRestartBackoffMs() const {
        std::lock_guard<std::mutex> lock(_restartAttemptsMutex);

        int attemptCount = static_cast<int>(_restartAttempts.size());
        if (attemptCount == 0) {
            return 0;
        }

        // Cap at 60 seconds
        constexpr int64_t maxBackoffMs = 60000;

        // Exponential backoff: base * 2^(attempts-1)
        // Clamp exponent to prevent undefined behavior from large shifts
        constexpr int maxShift = 30;
        int exponent = std::min(attemptCount - 1, maxShift);

        int64_t baseMs = _manifest.autoRestart.backoffBaseMilliseconds;
        int64_t backoff = baseMs * (static_cast<int64_t>(1) << exponent);

        return static_cast<int>(std::min(backoff, maxBackoffMs));
    }

    uint32_t Resource::GetContentHash() const {
        std::lock_guard<std::mutex> lock(_contentHashMutex);

        if (!_contentHashValid) {
            _contentHash = CalculateContentHash();
            _contentHashValid = true;
        }

        return _contentHash;
    }

    void Resource::InvalidateContentHash() {
        std::lock_guard<std::mutex> lock(_contentHashMutex);
        _contentHashValid = false;
        _contentHash = 0;
    }

    uint32_t Resource::CalculateContentHash() const {
        std::ostringstream combinedContent;

        // Include manifest metadata in hash
        combinedContent << _manifest.name << "|";
        combinedContent << _manifest.version << "|";
        combinedContent << _manifest.author << "|";

        // Hash all script files
        auto hashFiles = [&combinedContent](const std::vector<std::string> &files) {
            for (const auto &filePath : files) {
                std::ifstream file(filePath, std::ios::binary);
                if (file.is_open()) {
                    combinedContent << filePath << ":";
                    combinedContent << file.rdbuf();
                    combinedContent << "|";
                }
            }
        };

        hashFiles(_serverScriptPaths);
        hashFiles(_clientScriptPaths);

        // Also include the manifest.json file itself
        std::string manifestPath = _path + "/manifest.json";
        std::ifstream manifestFile(manifestPath, std::ios::binary);
        if (manifestFile.is_open()) {
            combinedContent << "manifest:";
            combinedContent << manifestFile.rdbuf();
        }

        std::string content = combinedContent.str();
        return Utils::Hashing::CalculateCRC32(content.c_str(), content.size());
    }

} // namespace Framework::Scripting
