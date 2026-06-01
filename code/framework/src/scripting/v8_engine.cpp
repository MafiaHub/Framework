/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "v8_engine.h"
#include "v8_engine_callbacks.h"
#include "engine_helpers.h"
#include "builtins/messages.h"

#include <logging/logger.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

using namespace Framework::Scripting::V8EngineCallbacks;

namespace Framework::Scripting {

    std::unique_ptr<v8::Platform> V8Engine::_platform = nullptr;
    bool V8Engine::_platformInitialized = false;

    V8Engine::V8Engine(const V8EngineOptions &options)
        : _options(options) {}

    V8Engine::~V8Engine() {
        Shutdown();
    }

    ScriptingError V8Engine::Init() {
        _lastError.clear();

        if (_initialized) {
            return ScriptingError::SCRIPTING_NONE;
        }

        if (!InitializePlatform()) {
            return ScriptingError::SCRIPTING_PLATFORM_INIT_FAILED;
        }

        if (!CreateIsolateAndContext()) {
            return ScriptingError::SCRIPTING_ENGINE_INIT_FAILED;
        }

        if (_options.moduleRootPath.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("moduleRootPath is not set — require() can load any file on the filesystem");
        }

        _initialized = true;
        return ScriptingError::SCRIPTING_NONE;
    }

    void V8Engine::SetModuleRootPath(const std::string &path) {
        _options.moduleRootPath = path;
    }

    void V8Engine::ClearModuleCache() {
        // Must only be called after stopping resources.
        // RequireData stores backing pointers for module-scoped require().
        _moduleCache.clear();
        _requireDataStore.clear();
    }

    void V8Engine::Shutdown() {
        if (!_initialized) {
            return;
        }

        {
            v8::Locker locker(_isolate);
            v8::Isolate::Scope isolate_scope(_isolate);
            v8::HandleScope handle_scope(_isolate);
            v8::Local<v8::Context> context = _context.Get(_isolate);
            v8::Context::Scope context_scope(context);

            Messages::Shutdown();
        }

        // Clear all timer callbacks (prevent dangling references)
        _timers.clear();

        ClearModuleCache();

        // Reset the persistent context handle
        _context.Reset();

        // Dispose the isolate
        _isolate->Dispose();
        _isolate = nullptr;

        // Free the array buffer allocator (V8 docs: caller is responsible)
        delete _createParams.array_buffer_allocator;
        _createParams.array_buffer_allocator = nullptr;

        _initialized = false;
    }

    bool V8Engine::InitializePlatform() {
        if (_platformInitialized) {
            return true;
        }

        // Use V8's default platform (NOT Node.js MultiIsolatePlatform)
        _platform = v8::platform::NewDefaultPlatform();
        v8::V8::InitializePlatform(_platform.get());
        v8::V8::Initialize();

        _platformInitialized = true;
        return true;
    }

    bool V8Engine::CreateIsolateAndContext() {
        // Create isolate with default allocator
        _createParams.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
        _isolate = v8::Isolate::New(_createParams);

        if (!_isolate) {
            _lastError = "Failed to create V8 isolate";
            delete _createParams.array_buffer_allocator;
            _createParams.array_buffer_allocator = nullptr;
            return false;
        }

        // Install promise rejection callback
        _isolate->SetPromiseRejectCallback(PromiseRejectCallback);

        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolate_scope(_isolate);
        v8::HandleScope handle_scope(_isolate);

        // Create context
        v8::Local<v8::Context> context = v8::Context::New(_isolate);
        if (context.IsEmpty()) {
            _lastError = "Failed to create V8 context";
            _isolate->Dispose();
            _isolate = nullptr;
            delete _createParams.array_buffer_allocator;
            _createParams.array_buffer_allocator = nullptr;
            return false;
        }

        v8::Context::Scope context_scope(context);

        // Block eval() and new Function('') for security
        context->AllowCodeGenerationFromStrings(false);

        // Persist the context
        _context.Reset(_isolate, context);

        // Install global APIs
        InstallGlobals();

        return true;
    }

    void V8Engine::InstallGlobals() {
        v8::Local<v8::Context> context = _context.Get(_isolate);
        v8::Local<v8::Object> global = context->Global();

        // Create globalThis.Framework = {}
        v8::Local<v8::Object> frameworkObj = v8::Object::New(_isolate);
        global->Set(context,
            v8::String::NewFromUtf8Literal(_isolate, "Framework"),
            frameworkObj).Check();

        // Create globalThis.Core = {}
        v8::Local<v8::Object> coreObj = v8::Object::New(_isolate);
        global->Set(context,
            v8::String::NewFromUtf8Literal(_isolate, "Core"),
            coreObj).Check();

        InstallTimerFunctions();
        InstallRequireFunction();

        // Install queueMicrotask
        v8::Local<v8::FunctionTemplate> queueMicrotaskTmpl =
            v8::FunctionTemplate::New(_isolate, QueueMicrotaskCallback);
        global->Set(context,
            v8::String::NewFromUtf8Literal(_isolate, "queueMicrotask"),
            queueMicrotaskTmpl->GetFunction(context).ToLocalChecked()).Check();
    }

    void V8Engine::InstallTimerFunctions() {
        v8::Local<v8::Context> context = _context.Get(_isolate);
        v8::Local<v8::Object> global = context->Global();

        // Use member structs — no heap allocation, lifetime matches engine
        _setTimeoutData = {this, false};
        _setIntervalData = {this, true};
        v8::Local<v8::External> setTimeoutExternal = v8::External::New(_isolate, &_setTimeoutData);
        v8::Local<v8::External> setIntervalExternal = v8::External::New(_isolate, &_setIntervalData);

        v8::Local<v8::External> engineExternal = v8::External::New(_isolate, this);

        // setTimeout
        v8::Local<v8::FunctionTemplate> setTimeoutTmpl =
            v8::FunctionTemplate::New(_isolate, SetTimerCallback, setTimeoutExternal);
        global->Set(context,
            v8::String::NewFromUtf8Literal(_isolate, "setTimeout"),
            setTimeoutTmpl->GetFunction(context).ToLocalChecked()).Check();

        // setInterval
        v8::Local<v8::FunctionTemplate> setIntervalTmpl =
            v8::FunctionTemplate::New(_isolate, SetTimerCallback, setIntervalExternal);
        global->Set(context,
            v8::String::NewFromUtf8Literal(_isolate, "setInterval"),
            setIntervalTmpl->GetFunction(context).ToLocalChecked()).Check();

        // clearTimeout / clearInterval (same implementation)
        v8::Local<v8::FunctionTemplate> clearTimerTmpl =
            v8::FunctionTemplate::New(_isolate, ClearTimerCallback, engineExternal);
        v8::Local<v8::Function> clearTimerFn = clearTimerTmpl->GetFunction(context).ToLocalChecked();

        global->Set(context,
            v8::String::NewFromUtf8Literal(_isolate, "clearTimeout"),
            clearTimerFn).Check();
        global->Set(context,
            v8::String::NewFromUtf8Literal(_isolate, "clearInterval"),
            clearTimerFn).Check();
    }

    void V8Engine::InstallRequireFunction() {
        v8::Local<v8::Context> context = _context.Get(_isolate);
        v8::Local<v8::Object> global = context->Global();

        // Default require uses cwd as the reference directory
        std::string cwd = std::filesystem::current_path().string();
        auto data = std::make_unique<RequireData>(RequireData{this, cwd});
        auto *dataPtr = data.get();
        _requireDataStore.push_back(std::move(data));
        v8::Local<v8::External> external = v8::External::New(_isolate, dataPtr);

        v8::Local<v8::FunctionTemplate> requireTmpl =
            v8::FunctionTemplate::New(_isolate, RequireCallback, external);
        global->Set(context,
            v8::String::NewFromUtf8Literal(_isolate, "require"),
            requireTmpl->GetFunction(context).ToLocalChecked()).Check();
    }

    uint32_t V8Engine::AddTimer(std::unique_ptr<TimerEntry> entry) {
        if (_timers.size() >= kMaxTimers) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Timer limit ({}) reached, ignoring new timer", kMaxTimers);
            return 0;
        }
        uint32_t id = _nextTimerId++;
        if (_nextTimerId == 0) {
            _nextTimerId = 1;
        }
        entry->id = id;
        _timers.push_back(std::move(entry));
        return id;
    }

    void V8Engine::CancelTimer(uint32_t id) {
        for (auto &timer : _timers) {
            if (timer->id == id) {
                timer->cancelled = true;
                return;
            }
        }
    }

    void V8Engine::ProcessTimers() {
        auto now = std::chrono::steady_clock::now();

        // Collect IDs of due timers (not raw pointers, which would dangle
        // if a callback calls AddTimer and _timers reallocates).
        std::vector<uint32_t> dueIds;
        for (auto &timer : _timers) {
            if (!timer->cancelled && timer->fireTime <= now) {
                dueIds.push_back(timer->id);
            }
        }

        v8::Local<v8::Context> context = _context.Get(_isolate);

        for (uint32_t id : dueIds) {
            // Look up the timer by ID each iteration (safe across reallocations).
            TimerEntry *timer = nullptr;
            for (auto &t : _timers) {
                if (t->id == id) {
                    timer = t.get();
                    break;
                }
            }
            if (!timer || timer->cancelled) continue;

            v8::TryCatch tryCatch(_isolate);

            v8::Local<v8::Function> callback = timer->callback.Get(_isolate);

            // Build args array
            std::vector<v8::Local<v8::Value>> args;
            args.reserve(timer->args.size());
            for (auto &arg : timer->args) {
                args.push_back(arg.Get(_isolate));
            }

            v8::MaybeLocal<v8::Value> result = callback->Call(
                context, v8::Undefined(_isolate),
                static_cast<int>(args.size()),
                args.empty() ? nullptr : args.data());

            if (tryCatch.HasCaught()) {
                std::string error = FormatV8Exception(_isolate, tryCatch, "Timer callback error");
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("{}", error);
            }

            // Re-lookup: callback may have mutated _timers.
            timer = nullptr;
            for (auto &t : _timers) {
                if (t->id == id) {
                    timer = t.get();
                    break;
                }
            }
            if (!timer) continue;

            // Re-queue intervals
            if (timer->intervalMs > 0 && !timer->cancelled) {
                timer->fireTime = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timer->intervalMs);
            } else {
                timer->cancelled = true;
            }
        }

        // Remove cancelled timers
        _timers.erase(
            std::remove_if(_timers.begin(), _timers.end(),
                [](const std::unique_ptr<TimerEntry> &t) { return t->cancelled; }),
            _timers.end());
    }

    std::string V8Engine::ResolveModulePath(std::string_view requested,
                                             std::string_view fromDir) {
        namespace fs = std::filesystem;

        fs::path base(fromDir);
        fs::path resolved;

        if (requested.starts_with("./") || requested.starts_with("../")) {
            resolved = base / requested;
        } else if (fs::path(requested).is_absolute()) {
            resolved = fs::path(requested);
        } else {
            // Bare specifier — rejected by RequireCallback before reaching here
            return "";
        }

        resolved = fs::weakly_canonical(resolved);

        // Security: validate against moduleRootPath if set
        if (!_options.moduleRootPath.empty()) {
            fs::path root = fs::weakly_canonical(fs::path(_options.moduleRootPath));
            std::string rootStr = root.string();
            std::string resolvedStr = resolved.string();

#ifdef _WIN32
            // Windows filesystems are case-insensitive; normalize both paths
            // to lowercase so the prefix check cannot be bypassed via casing.
            std::transform(rootStr.begin(), rootStr.end(), rootStr.begin(),
                [](unsigned char c) { return std::tolower(c); });
            std::transform(resolvedStr.begin(), resolvedStr.end(), resolvedStr.begin(),
                [](unsigned char c) { return std::tolower(c); });
#endif

            // Ensure resolved path starts with root AND sits inside the
            // directory (not just sharing a prefix).  Without the boundary
            // check, moduleRootPath="/app/cache" would also allow
            // "/app/cache-evil/malicious.js".
            if (resolvedStr.size() < rootStr.size() ||
                resolvedStr.compare(0, rootStr.size(), rootStr) != 0) {
                return ""; // Path escapes module root
            }
            if (resolvedStr.size() > rootStr.size()) {
                char next = resolvedStr[rootStr.size()];
                if (next != '/' && next != '\\') {
                    return ""; // Prefix match but not at a directory boundary
                }
            }
        }

        // Try exact path
        if (fs::exists(resolved) && fs::is_regular_file(resolved)) {
            return resolved.string();
        }

        // Try with .js extension
        fs::path withJs = resolved;
        withJs += ".js";
        if (fs::exists(withJs) && fs::is_regular_file(withJs)) {
            return withJs.string();
        }

        // Try as directory with index.js
        fs::path indexJs = resolved / "index.js";
        if (fs::exists(indexJs) && fs::is_regular_file(indexJs)) {
            return indexJs.string();
        }

        return "";
    }

    v8::MaybeLocal<v8::Value> V8Engine::LoadModule(std::string_view requestedPath,
                                                    std::string_view referencingDir) {
        v8::EscapableHandleScope handleScope(_isolate);
        v8::Local<v8::Context> context = _context.Get(_isolate);

        std::string resolvedPath = ResolveModulePath(requestedPath, referencingDir);
        if (resolvedPath.empty()) {
            std::string msg = "Cannot find module '";
            msg.append(requestedPath);
            msg += "' from '";
            msg.append(referencingDir);
            msg += "'";
            _isolate->ThrowException(v8::Exception::Error(
                v8::String::NewFromUtf8(_isolate, msg.c_str()).ToLocalChecked()));
            return v8::MaybeLocal<v8::Value>();
        }

        // Check cache
        auto cacheIt = _moduleCache.find(resolvedPath);
        if (cacheIt != _moduleCache.end()) {
            return handleScope.Escape(cacheIt->second.Get(_isolate));
        }

        // Read the file
        std::string source;
        if (!ReadFileContents(resolvedPath, source)) {
            std::string msg = "Cannot read module '" + resolvedPath + "'";
            _isolate->ThrowException(v8::Exception::Error(
                v8::String::NewFromUtf8(_isolate, msg.c_str()).ToLocalChecked()));
            return v8::MaybeLocal<v8::Value>();
        }

        // Get the directory of the resolved file for relative requires within it
        std::string moduleDir = std::filesystem::path(resolvedPath).parent_path().string();

        // Create module and exports objects
        v8::Local<v8::Object> moduleObj = v8::Object::New(_isolate);
        v8::Local<v8::Object> exportsObj = v8::Object::New(_isolate);
        moduleObj->Set(context,
            v8::String::NewFromUtf8Literal(_isolate, "exports"),
            exportsObj).Check();

        // Insert provisional cache entry before module execution to support
        // circular dependencies (A -> B -> A) without recursive re-entry.
        v8::Global<v8::Value> provisionalExports;
        provisionalExports.Reset(_isolate, exportsObj);
        _moduleCache[resolvedPath] = std::move(provisionalExports);

        // Create a require function bound to this module's directory (owned by engine)
        auto requireData = std::make_unique<RequireData>(RequireData{this, moduleDir});
        auto *requireDataPtr = requireData.get();
        _requireDataStore.push_back(std::move(requireData));
        v8::Local<v8::External> requireExternal = v8::External::New(_isolate, requireDataPtr);
        v8::Local<v8::FunctionTemplate> requireTmpl =
            v8::FunctionTemplate::New(_isolate, RequireCallback, requireExternal);
        v8::Local<v8::Function> requireFn = requireTmpl->GetFunction(context).ToLocalChecked();

        // Wrap source in CommonJS function
        std::string wrapped = "(function(exports, require, module, __filename, __dirname) { " +
            source + "\n})";

        v8::TryCatch tryCatch(_isolate);

        // Use the resolved path as the script filename for stack traces
        v8::Local<v8::String> v8Source =
            v8::String::NewFromUtf8(_isolate, wrapped.c_str()).ToLocalChecked();
        v8::ScriptOrigin origin(
            v8::String::NewFromUtf8(_isolate, resolvedPath.c_str()).ToLocalChecked());

        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(context, v8Source, &origin).ToLocal(&script)) {
            _moduleCache.erase(resolvedPath);
            if (tryCatch.HasCaught()) {
                tryCatch.ReThrow();
            }
            return v8::MaybeLocal<v8::Value>();
        }

        v8::Local<v8::Value> wrapperValue;
        if (!script->Run(context).ToLocal(&wrapperValue) || !wrapperValue->IsFunction()) {
            _moduleCache.erase(resolvedPath);
            if (tryCatch.HasCaught()) {
                tryCatch.ReThrow();
            }
            return v8::MaybeLocal<v8::Value>();
        }

        v8::Local<v8::Function> wrapper = v8::Local<v8::Function>::Cast(wrapperValue);

        // Prepare arguments: exports, require, module, __filename, __dirname
        // Use generic_string() for forward slashes on all platforms
        std::string genericPath = std::filesystem::path(resolvedPath).generic_string();
        std::string genericDir = std::filesystem::path(moduleDir).generic_string();

        v8::Local<v8::Value> callArgs[] = {
            exportsObj,
            requireFn,
            moduleObj,
            v8::String::NewFromUtf8(_isolate, genericPath.c_str()).ToLocalChecked(),
            v8::String::NewFromUtf8(_isolate, genericDir.c_str()).ToLocalChecked()
        };

        v8::MaybeLocal<v8::Value> callResult = wrapper->Call(
            context, v8::Undefined(_isolate), 5, callArgs);

        if (tryCatch.HasCaught()) {
            _moduleCache.erase(resolvedPath);
            tryCatch.ReThrow();
            return v8::MaybeLocal<v8::Value>();
        }

        if (callResult.IsEmpty()) {
            _moduleCache.erase(resolvedPath);
            return v8::MaybeLocal<v8::Value>();
        }

        // Get module.exports (may have been reassigned by the module)
        v8::Local<v8::Value> moduleExports;
        if (!moduleObj->Get(context, v8::String::NewFromUtf8Literal(_isolate, "exports"))
                .ToLocal(&moduleExports)) {
            moduleExports = exportsObj;
        }

        // Cache the exports
        v8::Global<v8::Value> cachedExports;
        cachedExports.Reset(_isolate, moduleExports);
        _moduleCache[resolvedPath] = std::move(cachedExports);

        return handleScope.Escape(moduleExports);
    }

    void V8Engine::Tick() {
        if (!_initialized) {
            return;
        }

        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolate_scope(_isolate);
        v8::HandleScope handle_scope(_isolate);
        v8::Context::Scope context_scope(GetContext());

        ProcessTimers();
        _isolate->PerformMicrotaskCheckpoint();
    }

    bool V8Engine::ExecuteFile(std::string_view filepath) {
        if (!_initialized) {
            _lastError = "Engine not initialized";
            return false;
        }

        // Convert to absolute path
        std::filesystem::path absPath = std::filesystem::absolute(filepath);
        std::string absPathStr = absPath.string();
        std::string dirStr = absPath.parent_path().string();

        // Load the module directly via C++ (bypasses JS require() which
        // only allows relative paths). This is the internal entry point
        // used by ResourceManager::ExecuteResourceScript().
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolate_scope(_isolate);
        v8::HandleScope handle_scope(_isolate);
        v8::Local<v8::Context> context = _context.Get(_isolate);
        v8::Context::Scope context_scope(context);

        v8::TryCatch tryCatch(_isolate);

        // Use LoadModule with a synthetic relative path "./<filename>"
        // resolved from the file's parent directory
        std::string filename = absPath.filename().string();
        v8::MaybeLocal<v8::Value> result = LoadModule("./" + filename, dirStr);

        if (result.IsEmpty()) {
            if (tryCatch.HasCaught()) {
                _lastError = FormatV8Exception(_isolate, tryCatch, "File execution error");
            } else {
                _lastError = "Failed to execute file: " + absPathStr;
            }
            return false;
        }

        return true;
    }

    v8::Isolate *V8Engine::GetIsolate() const {
        return _isolate;
    }

    v8::Local<v8::Context> V8Engine::GetContext() const {
        if (_isolate && !_context.IsEmpty()) {
            return _context.Get(_isolate);
        }
        return v8::Local<v8::Context>();
    }

} // namespace Framework::Scripting
