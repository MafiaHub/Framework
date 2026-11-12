/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_engine.h"

#include "lua_runtime.h"
#include "lua_scheduler.h"

#include "utils/vfs.h"

#include <lua.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace Framework::Scripting {
    namespace {
        // The whole of a regular file on the disk.
        bool ReadDiskFile(const std::filesystem::path &path, std::string &contents) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) {
                return false;
            }
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                return false;
            }
            const auto size = file.tellg();
            if (size < 0) {
                return false;
            }
            file.seekg(0, std::ios::beg);
            contents.resize(static_cast<size_t>(size));
            if (size > 0) {
                file.read(contents.data(), size);
                if (file.fail() || file.gcount() != size) {
                    contents.clear();
                    return false;
                }
            }
            return true;
        }

        // A file of the VFS, and nothing the disk has under the same path.
        bool ReadVirtualFile(const std::string &path, std::string &contents) {
            const Utils::Vfs &vfs = Utils::Vfs::Get();
            return vfs.Contains(path) && vfs.Read(path, contents);
        }

        bool ReadScript(const std::string &path, std::string &contents) {
            if (Utils::Vfs::IsVirtualPath(path)) {
                return ReadVirtualFile(Utils::Vfs::NormalizeVirtual(path), contents);
            }
            return ReadDiskFile(path, contents);
        }

        // Where `path` is below the directory `root`, with '/' between its parts. Empty when it is not below it.
        // On the disk links are followed first, so a file reached through one that leads out of the root is not.
        std::string PathBelow(const std::string &root, const std::string &path) {
            if (root.empty() || Utils::Vfs::IsVirtualPath(root) != Utils::Vfs::IsVirtualPath(path)) {
                return {};
            }
            if (Utils::Vfs::IsVirtualPath(root)) {
                const std::string normalizedRoot = Utils::Vfs::NormalizeVirtual(root);
                const std::string normalizedPath = Utils::Vfs::NormalizeVirtual(path);
                if (normalizedRoot.empty() || !normalizedPath.starts_with(normalizedRoot + "/")) {
                    return {};
                }
                return normalizedPath.substr(normalizedRoot.size() + 1);
            }

            std::filesystem::path directory(root);
            if (!directory.has_filename()) {
                directory = directory.parent_path();
            }
            std::error_code error;
            const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(directory, error);
            if (error) {
                return {};
            }
            const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(std::filesystem::path(path), error);
            if (error) {
                return {};
            }
            const std::filesystem::path relative = canonicalPath.lexically_relative(canonicalRoot);
            if (relative.empty() || relative == "." || *relative.begin() == "..") {
                return {};
            }
            return relative.generic_string();
        }

        // What Lua shows for a file: the resource, then the path below its root. A file from elsewhere keeps its
        // name alone.
        std::string ChunkName(const std::string &resourceName, const std::string &root, const std::string &path) {
            std::string below = PathBelow(root, path);
            if (below.empty()) {
                below = path.substr(path.find_last_of("/\\") + 1);
            }
            return "@" + resourceName + "/" + below;
        }

        // Reads the module at `relativePath` of the resource whose directory is `root`.
        bool ReadModule(const std::string &root, const std::string &relativePath, std::string &source) {
            if (root.empty()) {
                return false;
            }
            if (Utils::Vfs::IsVirtualPath(root)) {
                const std::string path = root + "/" + relativePath;
                return !PathBelow(root, path).empty() && ReadVirtualFile(Utils::Vfs::NormalizeVirtual(path), source);
            }
            const std::filesystem::path path = std::filesystem::path(root) / relativePath;
            return !PathBelow(root, path.string()).empty() && ReadDiskFile(path, source);
        }

        struct SetupCall final {
            const LuaEngine::RuntimeSetup &setup;
            LuaRuntime &runtime;
        };

        // Runs the setup callback inside the state. A std::exception it throws becomes a Lua error carrying what(),
        // raised once the catch block is left. A Lua error passing through is not a std::exception and is left
        // alone.
        int RunSetup(lua_State *L) {
            const auto *call = static_cast<const SetupCall *>(lua_touserdata(L, 1));
            std::string error;
            try {
                call->setup(call->runtime);
                return 0;
            }
            catch (const std::exception &exception) {
                error = exception.what();
            }
            lua_pushlstring(L, error.data(), error.size());
            return lua_error(L);
        }
    } // namespace

    LuaEngine::LuaEngine(LuaEngineOptions options): _options(options) {}

    LuaEngine::~LuaEngine() {
        Shutdown();
    }

    ScriptingError LuaEngine::Init() {
        _lastError.clear();
        _initialized = true;
        return ScriptingError::SCRIPTING_NONE;
    }

    void LuaEngine::Shutdown() {
        _initialized = false;

        // Resources have normally stopped by now. Any runtime left is taken away as one destroyed from inside its
        // own call is, ahead of the ones that already were.
        std::vector<std::unique_ptr<LuaRuntime>> retired;
        retired.reserve(_runtimes.size() + _retiredRuntimes.size());
        for (auto &[name, runtime] : _runtimes) {
            runtime->Scheduler().Stop();
            retired.push_back(std::move(runtime));
        }
        _runtimes.clear();
        for (auto &runtime : _retiredRuntimes) {
            retired.push_back(std::move(runtime));
        }
        _retiredRuntimes = std::move(retired);

        // Called from a script, this would close the state that script is running in, and host code further up
        // the stack may be in the middle of any of the others: they all go once nothing runs in any of them.
        _shuttingDown = true;
        FlushRetiredRuntimes();
    }

    void LuaEngine::Tick() {
        Tick(std::chrono::steady_clock::now());
    }

    void LuaEngine::Tick(std::chrono::steady_clock::time_point now) {
        Tick(now, std::chrono::steady_clock::time_point::min());
    }

    void LuaEngine::Tick(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point after) {
        FlushRetiredRuntimes();
        if (!_initialized) {
            return;
        }

        // By name: a callback may create and destroy runtimes, the one it runs in included.
        std::vector<std::string> names;
        names.reserve(_runtimes.size());
        for (const auto &[name, runtime] : _runtimes) {
            names.push_back(name);
        }
        for (const std::string &name : names) {
            const auto found = _runtimes.find(name);
            if (found != _runtimes.end() && !found->second->IsExecuting()) {
                found->second->Tick(now, after);
            }
        }
        FinishShutdown();
    }

    std::optional<std::chrono::steady_clock::time_point> LuaEngine::NextDue(std::chrono::steady_clock::time_point after) const {
        std::optional<std::chrono::steady_clock::time_point> earliest;
        for (const auto &[name, runtime] : _runtimes) {
            if (runtime->IsExecuting()) {
                continue;
            }
            const auto due = runtime->Scheduler().NextDue(after);
            if (due && (!earliest || *due < *earliest)) {
                earliest = due;
            }
        }
        return earliest;
    }

    bool LuaEngine::CreateResourceRuntime(const std::string &resourceName) {
        if (!_initialized) {
            _lastError = "Engine not initialized";
            return false;
        }
        if (_runtimes.contains(resourceName)) {
            _lastError = "resource '" + resourceName + "' already has a Lua runtime";
            return false;
        }

        LuaRuntimeOptions options;
        options.resourceName     = resourceName;
        options.side             = _options.side;
        options.memoryLimitBytes = _options.memoryLimitBytes;
        options.entryDeadline    = _options.entryDeadline;
        options.rootPath         = ResourceRoot(resourceName);
        std::unique_ptr<LuaRuntime> created;
        try {
            created = std::make_unique<LuaRuntime>(std::move(options));
        }
        catch (const std::exception &exception) {
            _lastError = exception.what();
            return false;
        }

        // The root as it is when a module is asked for: it may be set once the runtime exists.
        created->SetModuleLoader([this, resourceName](const std::string &relativePath, std::string &source) {
            return ReadModule(ResourceRoot(resourceName), relativePath, source);
        });
        // Queued: the scheduler is still at work in the runtime when it tells, and what handles an error may stop
        // the resource.
        created->Scheduler().SetErrorHandler([this, resourceName](const std::string &message) {
            _pendingErrors.push_back({resourceName, message});
        });

        LuaRuntime &runtime = *created;
        _runtimes.emplace(resourceName, std::move(created));
        const bool ready = !_runtimeSetup || SetUp(resourceName, runtime);
        FinishShutdown();
        return ready;
    }

    bool LuaEngine::SetUp(const std::string &resourceName, LuaRuntime &runtime) {
        // In a protected call: a binding that fails to register raises, and outside one that is a panic.
        const SetupCall call {_runtimeSetup, runtime};
        lua_pushcfunction(runtime.State(), RunSetup);
        lua_pushlightuserdata(runtime.State(), const_cast<SetupCall *>(&call));
        const bool ready = runtime.Call(1, 0, _lastError);

        // The callback may have destroyed the runtime it was setting up, or shut the engine down. That runtime is
        // on its way out then, and the name may already be another's.
        if (GetRuntime(resourceName) != &runtime) {
            if (ready) {
                _lastError = "resource runtime was destroyed during setup";
            }
            return false;
        }
        if (!ready) {
            DestroyResourceRuntime(resourceName);
        }
        return ready;
    }

    void LuaEngine::DestroyResourceRuntime(const std::string &resourceName) {
        const auto found = _runtimes.find(resourceName);
        if (found == _runtimes.end()) {
            return;
        }
        std::unique_ptr<LuaRuntime> runtime = std::move(found->second);
        _runtimes.erase(found);

        // Destroyed from inside its own call, or with its scheduler at work, it has to outlive that: it is freed
        // at the next tick, from the top, and until then none of its timers fires and nothing that waits in it is
        // resumed. So it is while JavaScript is on the stack, which may be in the middle of a call into this
        // runtime (ScriptActivity).
        if (runtime->IsExecuting() || ScriptActivity::OfAnother(Language::Lua)) {
            runtime->Scheduler().Stop();
            _retiredRuntimes.push_back(std::move(runtime));
            return;
        }
        Teardown(std::move(runtime));
    }

    bool LuaEngine::ExecuteResourceFile(const std::string &resourceName, std::string_view filepath) {
        const auto found = _runtimes.find(resourceName);
        if (found == _runtimes.end()) {
            _lastError = "no Lua runtime for resource '" + resourceName + "'";
            return false;
        }
        LuaRuntime &runtime = *found->second;

        const std::string path(filepath);
        std::string source;
        if (!ReadScript(path, source)) {
            _lastError = "cannot read '" + path + "'";
            return false;
        }

        // The file may destroy the runtime it runs in, which is then not to be touched once this returns.
        const bool ran = runtime.RunString(source, ChunkName(resourceName, ResourceRoot(resourceName), path), _lastError);
        FinishShutdown();
        return ran;
    }

    void LuaEngine::SetRuntimeSetupCallback(RuntimeSetup setup) {
        _runtimeSetup = std::move(setup);
    }

    void LuaEngine::SetResourceRoot(const std::string &resourceName, const std::string &rootPath) {
        _resourceRoots[resourceName] = rootPath;
    }

    LuaRuntime *LuaEngine::GetRuntime(const std::string &resourceName) const {
        const auto found = _runtimes.find(resourceName);
        return found != _runtimes.end() ? found->second.get() : nullptr;
    }

    std::vector<LuaEngine::PendingError> LuaEngine::DrainPendingErrors() {
        std::vector<PendingError> errors;
        errors.swap(_pendingErrors);
        return errors;
    }

    void LuaEngine::AddRuntimeDisposingListener(RuntimeDisposing listener) {
        _runtimeDisposingListeners.push_back(std::move(listener));
    }

    std::string LuaEngine::ResourceRoot(const std::string &resourceName) const {
        const auto found = _resourceRoots.find(resourceName);
        return found != _resourceRoots.end() ? found->second : std::string();
    }

    void LuaEngine::Teardown(std::unique_ptr<LuaRuntime> runtime) {
        for (const auto &listener : _runtimeDisposingListeners) {
            listener(*runtime);
        }
        runtime.reset();
    }

    void LuaEngine::FlushRetiredRuntimes() {
        // A shutdown that found a runtime executing leaves them all until none is.
        const bool shutdown = _shuttingDown;
        if (_shuttingDown) {
            const bool executing = std::any_of(_retiredRuntimes.begin(), _retiredRuntimes.end(), [](const auto &runtime) {
                return runtime->IsExecuting();
            });
            if (executing) {
                return;
            }
            _shuttingDown = false;
        }

        // Taken out first: a teardown runs listeners, which may destroy, and so retire, further runtimes.
        std::vector<std::unique_ptr<LuaRuntime>> retired;
        retired.swap(_retiredRuntimes);
        // A shutdown closes what is not executing whoever calls it; a tick from under JavaScript closes nothing.
        const bool held = !shutdown && ScriptActivity::OfAnother(Language::Lua);
        for (auto &runtime : retired) {
            if (held || runtime->IsExecuting()) {
                _retiredRuntimes.push_back(std::move(runtime));
            }
            else {
                Teardown(std::move(runtime));
            }
        }
    }

    void LuaEngine::FinishShutdown() {
        if (_shuttingDown) {
            FlushRetiredRuntimes();
        }
    }
} // namespace Framework::Scripting
