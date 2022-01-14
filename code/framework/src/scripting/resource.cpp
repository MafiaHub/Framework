/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "resource.h"

#include "engine.h"
#include "events.h"
#include "v8_helpers/v8_string.h"
#include "v8_helpers/v8_try_catch.h"

#include <logging/logger.h>
#include <nlohmann/json.hpp>
#include <optick.h>

static const char envBootstrapper[] = R"(
const publicRequire = require("module").createRequire(process.cwd() + "/resources/" + "{}" + "/");
globalThis.require = publicRequire;
require("vm").runInThisContext(process.argv[1]);
)";

namespace Framework::Scripting {
    Resource::Resource(Engine *engine, std::string &path, SDKRegisterCallback cb): _loaded(false), _isShuttingDown(false), _path(path), _engine(engine), _regCb(cb) {
        if (LoadPackageFile()) {
            if (Init(cb)) {
                _loaded = true;

                // Install the watch handler
                WatchChanges();
            }
        }
    }

    Resource::~Resource() {
        if (_loaded) {
            Shutdown();
            _loaded = false;
        }
    }

    bool Resource::LoadPackageFile() {
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Loading '{}'", _path);
        cppfs::FileHandle packageFile = cppfs::fs::open(_path + "/package.json");
        // Is the file valid ?
        if (!packageFile.exists()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The package.json file does not exists");
            return false;
        }

        if (!packageFile.isFile()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The package.json entry is not a file");
            return false;
        }

        // Read the content
        std::string content = packageFile.readFile();
        if (content.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The package.json is empty");
            return false;
        }

        // Apply settings
        auto root = nlohmann::json::parse(content);
        try {
            _name       = root["name"].get<std::string>();
            _version    = root["version"].get<std::string>();
            _entryPoint = root["entry_point"].get<std::string>();
        }
        catch (nlohmann::detail::type_error &err) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("The package.json is not valid:\n\t{}", err.what());
            return false;
        }
        return true;
    }

    bool Resource::WatchChanges() {
        cppfs::FileHandle dir = cppfs::fs::open(_path);
        if (!dir.isDirectory()) {
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Watching '{}' changes", dir.path().c_str());
        _watcher.add(dir, cppfs::FileCreated | cppfs::FileRemoved | cppfs::FileModified | cppfs::FileAttrChanged, cppfs::Recursive);
        _watcher.addHandler([this](cppfs::FileHandle &fh, cppfs::FileEvent ev) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Resource '{}' is reloaded due to the file changes", _name);
            // Close the resource first, we'll start with a clean slate
            Shutdown();

            if (LoadPackageFile()) {
                Init(_regCb);
            }
        });

        _nextFileWatchUpdate = Utils::Time::Add(Utils::Time::GetTimePoint(), _fileWatchUpdatePeriod);
        return true;
    }

    bool Resource::Init(SDKRegisterCallback cb) {
        if (_loaded) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Resource '{}' is already loaded", _name);
            return false;
        }

        // Is the file valid?
        cppfs::FileHandle entryPointFile = cppfs::fs::open(_path + "/" + _entryPoint);
        if (!entryPointFile.exists() || !entryPointFile.isFile()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The specified entrypoint '{}' from '{}' is not a file", _entryPoint, _name);
            return false;
        }

        // Read the file content
        std::string content = entryPointFile.readFile();
        if (content.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The specified entrypoint file '{}' from '{}' is empty", _entryPoint, _name);
            return false;
        }

        // Acquire the isolate and scopes
        const auto isolate = _engine->GetIsolate();
        if (!isolate) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Cannot acquire isolate instance");
            return false;
        }

        v8::Locker isolateLock(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);

        // Initialize the execution context
        v8::Local<v8::Context> context = v8::Context::New(isolate, nullptr, _engine->GetObjectTemplate().Get(isolate));
        if (context.IsEmpty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Context init failed");
            return false;
        }

        // Register the SDK
        _sdk = new SDK(cb);
        _sdk->RegisterModules(context);

        // We put the local resource instance inside the context
        context->SetAlignedPointerInEmbedderData(0, this);
        _context.Reset(isolate, context);

        // Create the event loop
        _uvLoop      = uv_loop_new();
        _isolateData = node::CreateIsolateData(isolate, _uvLoop, _engine->GetPlatform());

        // Start initializing the environment
        v8::Context::Scope contextScope(context);
        v8::TryCatch tryCatch(isolate);

        node::EnvironmentFlags::Flags flags = (node::EnvironmentFlags::Flags)(node::EnvironmentFlags::kOwnsProcessState);
        _environment                        = node::CreateEnvironment(_isolateData, context, {_entryPoint, entryPointFile.path()}, {}, flags);
        if (!_environment) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Environment init failed");
            return false;
        }

        // Apply the isolate settings to the local resource
        node::IsolateSettings is;
        node::SetIsolateUpForNode(isolate, is);

        if (!node::LoadEnvironment(_environment, fmt::format(envBootstrapper, _name).c_str()).IsEmpty())
            return false;

        Compile(content, entryPointFile.path());
        Run();

        // Notify the resource load
        std::vector<v8::Local<v8::Value>> args = {Helpers::MakeString(isolate, _name).ToLocalChecked()};
        InvokeEvent(Events[EventIDs::RESOURCE_LOADED], args);

        _loaded = true;
        return true;
    }

    void Resource::Update(bool force) {
        // We don't want to update a shutdown-in-progress resource
        if (_isShuttingDown && !force) {
            return;
        }

        OPTICK_EVENT();

        const auto isolate = _engine->GetIsolate();

        // Process the event handlers
        for (auto it = _remoteHandlers.begin(); it != _remoteHandlers.end();) {
            if (it->second._removed) {
                it = _remoteHandlers.erase(it);
            }
            else {
                ++it;
            }
        }

        // Process the scripting layer
        {
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Context::Scope scope(GetContext());

            uv_run(_uvLoop, UV_RUN_NOWAIT);
        }

        if (!_isShuttingDown && Utils::Time::Compare(_nextFileWatchUpdate, Utils::Time::GetTimePoint()) < 0) {
            // Process the file changes watcher
            _watcher.watch(0);
            _nextFileWatchUpdate = Utils::Time::Add(Utils::Time::GetTimePoint(), _fileWatchUpdatePeriod);
        }
    }

    bool Resource::Shutdown() {
        if (!_loaded) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Resource is already unloaded");
            return false;
        }

        if (!_environment) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Invalid environment instance");
            return false;
        }

        if (_isShuttingDown) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Resource '{}' is already shutting down", _name);
            return false;
        }

        // Flag the resource
        _isShuttingDown = true;

        const auto isolate = _engine->GetIsolate();

        // Notify the resource unload
        {
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);

            std::vector<v8::Local<v8::Value>> args = {Helpers::MakeString(isolate, _name).ToLocalChecked()};
            InvokeEvent(Events[EventIDs::RESOURCE_UNLOADING], args);
        }

        // Tick one last time
        Update(true);

        // Clear the event handlers
        _remoteHandlers.clear();

        // Clear the node instance
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);

        node::EmitBeforeExit(_environment);
        node::EmitExit(_environment);
        node::RunAtExit(_environment);

        node::FreeEnvironment(_environment);
        node::FreeIsolateData(_isolateData);

        // Reset our members
        _script.Reset();
        _context.Reset();
        _environment = nullptr;

        _loaded         = false;
        _isShuttingDown = false;

        return true;
    }

    bool Resource::Compile(const std::string &str, const std::string &path) {
        const auto isolate = _engine->GetIsolate();

        OPTICK_EVENT();

        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);

        Helpers::TryCatch([&] {
            const auto context = _context.Get(isolate);
            v8::Context::Scope contextScope(context);

            const auto source = Helpers::MakeString(isolate, str).ToLocalChecked();
            v8::MaybeLocal<v8::Script> script;
            if (path.empty()) {
                script = v8::Script::Compile(context, source);
            }
            else {
                auto originValue = Helpers::MakeString(isolate, path).ToLocalChecked();
                v8::ScriptOrigin scriptOrigin(originValue);

                script = v8::Script::Compile(context, source, &scriptOrigin);
            }
            if (script.IsEmpty())
                return false;

            _script.Reset(isolate, script.ToLocalChecked());
            return true;
        });
        return true;
    }

    bool Resource::Run() {
        if (_script.IsEmpty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Invalid resource script instance");
            return false;
        }

        OPTICK_EVENT();

        const auto isolate = _engine->GetIsolate();

        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        Helpers::TryCatch([&] {
            auto context = _context.Get(isolate);
            v8::Context::Scope contextScope(context);

            v8::MaybeLocal<v8::Value> returnValue = _script.Get(isolate)->Run(context);
            return true;
        });
        return true;
    }

    void Resource::SubscribeEvent(const std::string &ev, v8::Local<v8::Function> cb, Helpers::SourceLocation &&loc, bool once) {
        _remoteHandlers.insert({ev, Helpers::EventCallback {_engine->GetIsolate(), cb, std::move(loc), once}});
    }

    void Resource::InvokeErrorEvent(const std::string &error, const std::string &stackTrace, const std::string &file, int32_t line) {
        std::vector<v8::Local<v8::Value>> args = {Helpers::MakeString(_engine->GetIsolate(), error).ToLocalChecked(),
            Helpers::MakeString(_engine->GetIsolate(), stackTrace).ToLocalChecked(), Helpers::MakeString(_engine->GetIsolate(), file).ToLocalChecked(),
            v8::Integer::New(_engine->GetIsolate(), line)};
        InvokeEvent("resourceError", args);
    }

    void Resource::InvokeEvent(const std::string &eventName, std::vector<v8::Local<v8::Value>> &args, bool suppressLog) {
        auto entry = _remoteHandlers.find(eventName);
        if (entry == _remoteHandlers.end()) {
            if (!suppressLog)
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to find such event '{}' for resource '{}'", eventName, _name);
            return;
        }

        auto callback = &entry->second;
        if (callback->_removed) {
            Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("Event '{}' in '{}' was already fired, not supposed to be here", eventName, _name);
            return;
        }

        const auto isolate = _engine->GetIsolate();

        Helpers::TryCatch(
            [&] {
                v8::MaybeLocal<v8::Value> retn = callback->_fn.Get(isolate)->Call(_context.Get(isolate), v8::Undefined(isolate), args.size(), args.data());
                if (retn.IsEmpty()) {
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to invoke event '{}' for '{}'", eventName, _name);
                    return false;
                }

                if (!suppressLog)
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Successfully invoked event '{}' for '{}'", eventName, _name);
                return true;
            },
            isolate, _context.Get(isolate));

        if (callback->_once) {
            callback->_removed = true;
        }
    };

    v8::Local<v8::Context> Resource::GetContext() {
        auto isolate = _engine->GetIsolate();
        return _context.Get(isolate);
    }
} // namespace Framework::Scripting
