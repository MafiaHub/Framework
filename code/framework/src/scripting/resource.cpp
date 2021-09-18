#include "resource.h"

#include "engine.h"
#include "events.h"
#include "v8_helpers/v8_try_catch.h"

#include <logging/logger.h>
#include <nlohmann/json.hpp>

namespace Framework::Scripting {
    Resource::Resource(Engine *engine, std::string &path, SDKRegisterCallback cb): _loaded(false), _path(path), _engine(engine) {
        if (LoadPackageFile()) {
            if (Init(cb)) {
                _loaded = true;
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
        auto root   = nlohmann::json::parse(content);
        _name       = root["name"].get<std::string>();
        _version    = root["version"].get<std::string>();
        _entryPoint = root["entry_point"].get<std::string>();
        return true;
    }

    bool Resource::WatchChanges() {
        cppfs::FileHandle dir = cppfs::fs::open(_path);
        if (!dir.isDirectory()) {
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Watching {} changes", dir.path().c_str());
        _watcher.add(dir, cppfs::FileCreated | cppfs::FileRemoved | cppfs::FileModified | cppfs::FileAttrChanged, cppfs::Recursive);
        _watcher.addHandler([this](cppfs::FileHandle &fh, cppfs::FileEvent ev) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Resource {} is reloaded due to the file changes", _name);
            ReloadChanges();
        });
        return true;
    }

    void Resource::ReloadChanges() {
        cppfs::FileHandle entryPointFile = cppfs::fs::open(_path + "/" + _entryPoint);
        if (!entryPointFile.exists() || !entryPointFile.isFile()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The specified entrypoint is not a file");
            return;
        }

        // Read the file content
        std::string content = entryPointFile.readFile();
        if (content.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The specified entrypoint file is empty");
            return;
        }

        const auto isolate = _engine->GetIsolate();
        if (!isolate) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Cannot acquire isolate instance");
            return;
        }

        v8::Locker isolateLock(isolate);
        v8::Isolate::Scope isolateScope(isolate);

        Compile(content, entryPointFile.path());
        Run();

        std::vector<v8::Local<v8::Value>> args = {v8::String::NewFromUtf8(isolate, _name.c_str(), v8::NewStringType::kNormal).ToLocalChecked()};
        InvokeEvent(Events[EventIDs::RESOURCE_RELOADED], args);
    }

    bool Resource::Init(SDKRegisterCallback cb) {
        // Is the file valid?
        cppfs::FileHandle entryPointFile = cppfs::fs::open(_path + "/" + _entryPoint);
        if (!entryPointFile.exists() || !entryPointFile.isFile()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The specified entrypoint is not a file");
            return false;
        }

        // Read the file content
        std::string content = entryPointFile.readFile();
        if (content.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The specified entrypoint file is empty");
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

        // Start initializing the environment
        v8::Context::Scope contextScope(context);
        v8::TryCatch tryCatch(isolate);
        _environment = node::CreateEnvironment(_engine->GetIsolateData(), context, {_entryPoint, entryPointFile.path()}, {}, node::EnvironmentFlags::kNoFlags);
        if (!_environment) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Environment init failed");
            return false;
        }

        std::string init = "const publicRequire ="
                           "require('module').createRequire(process.cwd() + '/resources/"
                           + _name
                           + "/');"
                             "globalThis.require = publicRequire;"
                             "require('vm').runInThisContext(process.argv[1]);";
        node::LoadEnvironment(_environment, init.c_str());
        Compile(content, entryPointFile.path());
        Run();

        // Notify the resource load
        std::vector<v8::Local<v8::Value>> args = {v8::String::NewFromUtf8(isolate, _name.c_str(), v8::NewStringType::kNormal).ToLocalChecked()};
        InvokeEvent(Events[EventIDs::RESOURCE_LOADED], args);

        // Install the watch handler
        WatchChanges();
        return true;
    }

    void Resource::Update() {
        const auto isolate = _engine->GetIsolate();

        // Process the event handlers
        for (auto it = _remoteHandlers.begin(); it != _remoteHandlers.end();) {
            if (it->second._removed) {
                it = _remoteHandlers.erase(it);
            } else {
                ++it;
            }
        }

        // Process the scripting layer
        v8::HandleScope scope(isolate);
        v8::Local<v8::Context> context = _context.Get(isolate);
        context->Enter();
        context->Exit();

        // Process the file changes watcher
        _watcher.watch();
    }

    bool Resource::Shutdown() {
        if (!_environment) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Invalid environment instance");
            return false;
        }

        const auto isolate = _engine->GetIsolate();

        v8::Locker lock(isolate);
        v8::HandleScope handleScope(isolate);

        node::EmitBeforeExit(_environment);
        node::EmitExit(_environment);
        node::Stop(_environment);

        _script.Reset();
        _context.Reset();
        return true;
    }

    bool Resource::Compile(const std::string &str, const std::string &path) {
        const auto isolate = _engine->GetIsolate();

        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);

        Helpers::TryCatch([&] {
            const auto context = _context.Get(isolate);
            v8::Context::Scope contextScope(context);

            const auto source = v8::String::NewFromUtf8(isolate, str.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
            v8::MaybeLocal<v8::Script> script;
            if (path.empty()) {
                script = v8::Script::Compile(context, source);
            } else {
                auto originValue = v8::String::NewFromUtf8(isolate, path.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
                v8::ScriptOrigin scriptOrigin(originValue);

                script = v8::Script::Compile(context, source, &scriptOrigin);
            }

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
        std::vector<v8::Local<v8::Value>> args = {v8::String::NewFromUtf8(_engine->GetIsolate(), error.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
                                                  v8::String::NewFromUtf8(_engine->GetIsolate(), stackTrace.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
                                                  v8::String::NewFromUtf8(_engine->GetIsolate(), file.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
                                                  v8::Integer::New(_engine->GetIsolate(), line)};
        InvokeEvent("resourceError", args);
    }

    void Resource::InvokeEvent(const std::string &eventName, std::vector<v8::Local<v8::Value>> &args) {
        auto entry = _remoteHandlers.find(eventName);
        if (entry == _remoteHandlers.end()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to find such event");
            return;
        }

        auto callback = &entry->second;
        if (callback->_removed) {
            Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("Event was already fired, not supposed to be here");
            return;
        }

        const auto isolate = _engine->GetIsolate();

        Helpers::TryCatch([&] {
            v8::MaybeLocal<v8::Value> retn = callback->_fn.Get(isolate)->Call(_context.Get(isolate), v8::Undefined(isolate), args.size(), args.data());
            if (retn.IsEmpty()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to invoke event {}", eventName);
                return false;
            }

            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Successfully invoked event {}", eventName);
            return true;
        });

        if (callback->_once) {
            callback->_removed = true;
        }
    };

    v8::Local<v8::Context> Resource::GetContext() {
        return _context.Get(_engine->GetIsolate());
    }
} // namespace Framework::Scripting
