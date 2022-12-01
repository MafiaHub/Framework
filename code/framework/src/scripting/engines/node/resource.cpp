/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <nlohmann/json.hpp>

#include <logging/logger.h>

#include "engine.h"
#include "resource.h"

#include "../../events.h"

#include "v8_helpers/v8_string.h"
#include "v8_helpers/v8_try_catch.h"

static const char bootstrap_code[] = R"(
const publicRequire = require("module").createRequire(process.cwd() + "/resources/" + "{}" + "/");
globalThis.require = publicRequire;
require("vm").runInThisContext(process.argv[1]);
)";

namespace Framework::Scripting::Engines::Node {
    Resource::Resource(IEngine *engine, v8::Isolate *isolate, std::string &path): _engine(engine), _isolate(isolate), _loaded(false), _isShuttingDown(false), _path(path) {}

    Resource::~Resource() {
        if (_loaded) {
            Shutdown();
        }
    }

    bool Resource::Init() {
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

        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);

        // Allocate our context
        v8::Local<v8::Context> context = v8::Context::New(_isolate, nullptr, reinterpret_cast<Node::Engine *>(_engine)->GetGlobalObjectTemplate());
        v8::Context::Scope scope(context);

        // Assign ourselves in the context (allows to be fetched later on)
        context->SetAlignedPointerInEmbedderData(0, this);
        _context.Reset(_isolate, context);

        // Initialize our uv loop
        _uvLoop = new uv_loop_t;
        uv_loop_init(_uvLoop);

        // Initialize our execution environment
        node::EnvironmentFlags::Flags flags = (node::EnvironmentFlags::Flags)(node::EnvironmentFlags::kOwnsProcessState);
        _nodeData                           = node::CreateIsolateData(_isolate, _uvLoop, reinterpret_cast<Node::Engine *>(_engine)->GetPlatform());
        std::vector<std::string> argv       = {"mafiahub-resource"};
        _env                                = node::CreateEnvironment(_nodeData, context, argv, argv, flags);

        // Make sure isolate is linked to our node environment
        node::IsolateSettings is;
        node::SetIsolateUpForNode(_isolate, is);

        // Load the environment
        node::LoadEnvironment(_env, fmt::format(bootstrap_code, _name).c_str());

        // Create our resource representation
        _asyncResource.Reset(_isolate, v8::Object::New(_isolate));
        _asyncContext = node::EmitAsyncInit(_isolate, _asyncResource.Get(_isolate), "Node::Resource");

        Compile(content, entryPointFile.path());
        Run();

        _loaded = true;
        InvokeEvent(Events[EventIDs::RESOURCE_LOADED], _name);
        return true;
    }

    bool Resource::Shutdown() {
        if (!_loaded) {
            return false;
        }

        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);

        // Exit node environment
        node::EmitProcessBeforeExit(_env);
        node::EmitProcessExit(_env);

        // Unregister the SDK
        // TODO: fix me

        // Stop node environment
        node::Stop(_env);

        // Release memory
        node::FreeEnvironment(_env);
        node::FreeIsolateData(_nodeData);

        // Mark as unloaded
        _loaded = false;

        // Clear the UV loop
        uv_loop_close(_uvLoop);
        delete _uvLoop;
        return true;
    }

    void Resource::Update(bool force) {
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);

        v8::Context::Scope scope(_context.Get(_isolate));
        node::CallbackScope callbackScope(_isolate, _asyncResource.Get(_isolate), _asyncContext);

        uv_run(_uvLoop, UV_RUN_NOWAIT);

        // Update the file watcher
        if (!_isShuttingDown && Utils::Time::Compare(_nextFileWatchUpdate, Utils::Time::GetTimePoint()) < 0) {
            // Process the file changes watcher
            _watcher.watch(0);
            _nextFileWatchUpdate = Utils::Time::Add(Utils::Time::GetTimePoint(), _fileWatchUpdatePeriod);
        }

        // Notify the resource
        InvokeEvent(Events[EventIDs::RESOURCE_UPDATED]);
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
                Init();
            }
        });

        _nextFileWatchUpdate = Utils::Time::Add(Utils::Time::GetTimePoint(), _fileWatchUpdatePeriod);
        return true;
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
            _entryPoint = root["main"].get<std::string>();
        }
        catch (nlohmann::detail::type_error &err) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("The package.json is not valid:\n\t{}", err.what());
            return false;
        }
        return true;
    }

    bool Resource::Compile(const std::string &str, const std::string &path) {
        const auto isolate = reinterpret_cast<Node::Engine *>(_engine)->GetIsolate();

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
                // v8::ScriptOrigin scriptOrigin(originValue);

                script = v8::Script::Compile(context, source, nullptr);
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

        const auto isolate = reinterpret_cast<Node::Engine *>(_engine)->GetIsolate();

        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        Helpers::TryCatch([&] {
            auto context = _context.Get(isolate);
            v8::Context::Scope contextScope(context);

            _script.Get(isolate)->Run(context);
            return true;
        });
        return true;
    }
    void Resource::Preload() {
        if (LoadPackageFile()) {
            if (Init()) {
                WatchChanges();
            }
        }
    }
} // namespace Framework::Scripting::Engines::Node
