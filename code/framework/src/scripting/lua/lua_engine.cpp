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

    class LuaEngine::Entered final {
      public:
        explicit Entered(ResourceRuntime &runtime): _runtime(runtime) {
            ++_runtime.entered;
        }

        ~Entered() {
            --_runtime.entered;
        }

        Entered(const Entered &)            = delete;
        Entered &operator=(const Entered &) = delete;

      private:
        ResourceRuntime &_runtime;
    };

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
        // Resources have normally stopped by now. A teardown runs listeners, which may destroy further runtimes.
        while (!_runtimes.empty() || !_retiredRuntimes.empty()) {
            if (!_runtimes.empty()) {
                auto node = _runtimes.extract(_runtimes.begin());
                Teardown(std::move(node.mapped()));
                continue;
            }
            std::unique_ptr<ResourceRuntime> runtime = std::move(_retiredRuntimes.front());
            _retiredRuntimes.erase(_retiredRuntimes.begin());
            Teardown(std::move(runtime));
        }
        _initialized = false;
    }

    void LuaEngine::Tick() {
        Tick(std::chrono::steady_clock::now());
    }

    void LuaEngine::Tick(std::chrono::steady_clock::time_point now) {
        if (!_initialized) {
            return;
        }
        FlushRetiredRuntimes();

        // By name: a callback may create and destroy runtimes, the one it runs in included.
        std::vector<std::string> names;
        names.reserve(_runtimes.size());
        for (const auto &[name, runtime] : _runtimes) {
            names.push_back(name);
        }
        for (const std::string &name : names) {
            const auto found = _runtimes.find(name);
            if (found == _runtimes.end() || IsBusy(*found->second)) {
                continue;
            }
            ResourceRuntime &runtime = *found->second;
            const Entered entered(runtime);
            runtime.runtime->Tick(now);
        }
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

        auto created          = std::make_unique<ResourceRuntime>();
        created->resourceName = resourceName;
        if (const auto root = _resourceRoots.find(resourceName); root != _resourceRoots.end()) {
            created->rootPath = root->second;
        }

        LuaRuntimeOptions options;
        options.resourceName     = resourceName;
        options.side             = _options.side;
        options.memoryLimitBytes = _options.memoryLimitBytes;
        options.entryDeadline    = _options.entryDeadline;
        options.rootPath         = created->rootPath;
        try {
            created->runtime = std::make_unique<LuaRuntime>(std::move(options));
        }
        catch (const std::exception &exception) {
            _lastError = exception.what();
            return false;
        }

        created->runtime->SetModuleLoader([root = created->rootPath](const std::string &relativePath, std::string &source) {
            return ReadModule(root, relativePath, source);
        });
        // Queued: the scheduler is still at work in the runtime when it tells, and what handles an error may stop
        // the resource.
        created->runtime->Scheduler().SetErrorHandler([this, resourceName](const std::string &message) {
            _pendingErrors.push_back({resourceName, message});
        });

        ResourceRuntime &runtime = *created;
        _runtimes.emplace(resourceName, std::move(created));
        if (!_runtimeSetup) {
            return true;
        }

        // In a protected call: a binding that fails to register raises, and outside one that is a panic.
        bool ready = false;
        {
            const SetupCall call {_runtimeSetup, *runtime.runtime};
            lua_State *L = runtime.runtime->State();
            lua_pushcfunction(L, RunSetup);
            lua_pushlightuserdata(L, const_cast<SetupCall *>(&call));
            const Entered entered(runtime);
            ready = runtime.runtime->Call(1, 0, _lastError);
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
        std::unique_ptr<ResourceRuntime> runtime = std::move(found->second);
        _runtimes.erase(found);

        // Destroyed from inside its own call, it has to outlive that call: it is freed at the next tick, from the
        // top, and until then none of its timers fires and nothing that waits in it is resumed.
        if (IsBusy(*runtime)) {
            runtime->runtime->Scheduler().Stop();
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
        ResourceRuntime &runtime = *found->second;

        const std::string path(filepath);
        std::string source;
        if (!ReadScript(path, source)) {
            _lastError = "cannot read '" + path + "'";
            return false;
        }

        // The file may destroy the runtime it runs in, which then waits for this to return.
        const Entered entered(runtime);
        return runtime.runtime->RunString(source, ChunkName(resourceName, runtime.rootPath, path), _lastError);
    }

    void LuaEngine::SetRuntimeSetupCallback(RuntimeSetup setup) {
        _runtimeSetup = std::move(setup);
    }

    void LuaEngine::SetResourceRoot(const std::string &resourceName, const std::string &rootPath) {
        _resourceRoots[resourceName] = rootPath;
    }

    LuaRuntime *LuaEngine::GetRuntime(const std::string &resourceName) const {
        const auto found = _runtimes.find(resourceName);
        return found != _runtimes.end() ? found->second->runtime.get() : nullptr;
    }

    std::vector<LuaEngine::PendingError> LuaEngine::DrainPendingErrors() {
        std::vector<PendingError> errors;
        errors.swap(_pendingErrors);
        return errors;
    }

    void LuaEngine::AddRuntimeDisposingListener(RuntimeDisposing listener) {
        _runtimeDisposingListeners.push_back(std::move(listener));
    }

    bool LuaEngine::IsBusy(const ResourceRuntime &runtime) {
        return runtime.entered > 0 || runtime.runtime->IsExecuting();
    }

    void LuaEngine::Teardown(std::unique_ptr<ResourceRuntime> runtime) {
        for (const auto &listener : _runtimeDisposingListeners) {
            listener(*runtime->runtime);
        }
        runtime->runtime.reset();
    }

    void LuaEngine::FlushRetiredRuntimes() {
        // Taken out first: a teardown runs listeners, which may destroy, and so retire, further runtimes.
        std::vector<std::unique_ptr<ResourceRuntime>> retired;
        retired.swap(_retiredRuntimes);
        for (auto &runtime : retired) {
            if (IsBusy(*runtime)) {
                _retiredRuntimes.push_back(std::move(runtime));
            }
            else {
                Teardown(std::move(runtime));
            }
        }
    }
} // namespace Framework::Scripting
