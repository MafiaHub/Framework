/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_runtime_ut.h"
#include "node_test_helpers.h"
#include "resource_manager_ut.h"

#include "integrations/server/scripting/module.h"
#include "scripting/lua/lua_engine.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace LuaResourcesTest {

    using Scripts = std::vector<std::pair<std::string, std::string>>;

    // What scripts of either language told the host through log(text), in the order they did. It outlives the
    // runtime that wrote it.
    inline std::vector<std::string> &Log() {
        static std::vector<std::string> log;
        return log;
    }

    inline std::string LogText() {
        std::string text;
        for (const std::string &entry : Log()) {
            text += (text.empty() ? "" : ",") + entry;
        }
        return text;
    }

    inline void InstallJavaScriptLog(v8::Isolate *isolate, v8::Local<v8::Context> context) {
        const auto log = [](const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::String::Utf8Value text(info.GetIsolate(), info[0]);
            Log().emplace_back(*text ? *text : "");
        };
        context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "log"), v8::Function::New(context, log).ToLocalChecked()).Check();
    }

    inline int LuaLog(lua_State *L) {
        Log().emplace_back(luaL_checkstring(L, 1));
        return 0;
    }

    // A resources directory on disk, removed with the object.
    struct Files final {
        std::filesystem::path root = std::filesystem::temp_directory_path() / ("framework_lua_resources_ut_" + std::to_string(std::random_device {}()));

        Files() {
            std::filesystem::remove_all(root);
            std::filesystem::create_directories(root);
        }

        ~Files() {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }

        std::filesystem::path Path(const std::string &resource, const std::string &file) const {
            return root / resource / file;
        }

        // Writes a file of a resource. A file written over gets a later modification time than it had, whatever
        // the resolution of the clock of the file system.
        void Write(const std::string &resource, const std::string &file, const std::string &content) const {
            const std::filesystem::path path = Path(resource, file);
            std::filesystem::create_directories(path.parent_path());
            std::error_code missing;
            const auto before = std::filesystem::last_write_time(path, missing);
            std::ofstream(path, std::ios::binary) << content;
            if (!missing) {
                std::filesystem::last_write_time(path, before + std::chrono::seconds(5));
            }
        }

        // Writes a manifest whose "mafiahub" object is `{<mafiahub>}`.
        void Manifest(const std::string &resource, const std::string &mafiahub) const {
            Write(resource, "package.json", "{\"name\":\"" + resource + "\",\"version\":\"1.0.0\",\"mafiahub\":{" + mafiahub + "}}");
        }

        // Writes a resource whose server scripts are `scripts`, run in that order. `extra` goes into the
        // "mafiahub" object as it is: `,"errorBehavior":"continue"`.
        void Resource(const std::string &resource, const Scripts &scripts, const std::string &extra = "") const {
            std::string list;
            for (const auto &[file, content] : scripts) {
                list += (list.empty() ? "\"" : ",\"") + file + "\"";
                Write(resource, file, content);
            }
            Manifest(resource, "\"serverScripts\":[" + list + "]" + extra);
        }
    };

    // A Node engine, a Lua engine and a manager over a resources directory of its own. Every runtime of either
    // language gets log(text) and stop(name), which stops a resource. A Lua one also gets resource(), the resource
    // the manager says is executing. The Lua engine is on a clock the test moves.
    struct Rig final {
        Files files;
        bool ready                                  = false;
        std::chrono::steady_clock::time_point clock = std::chrono::steady_clock::now();
        // What the manager told its listeners: "<resource>" for each stop, "<resource>: <first line>" for each error.
        std::vector<std::string> stopped;
        std::vector<std::string> errors;
        Framework::Scripting::NodeEngine node;
        Framework::Scripting::LuaEngine lua;
        // Last, so that it goes first: it stops what still runs, with both engines and the lists above still there.
        Framework::Scripting::ResourceManager manager;

        struct Options final {
            bool withLua  = true;
            bool isClient = false;
            bool devMode  = false;
        };

        Rig(): Rig(Options {}) {}

        explicit Rig(Options options): manager(&node, Config(files, options)) {
            Log().clear();
            ready = node.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE && lua.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE;
            if (!ready) {
                return;
            }
            TestManagerHelper::RegisterEvents(node, manager, [this](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                InstallJavaScriptLog(isolate, context);
                context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "stop"), v8::Function::New(context, JavaScriptStop, v8::External::New(isolate, this)).ToLocalChecked()).Check();
            });
            lua.SetRuntimeSetupCallback([this](Framework::Scripting::LuaRuntime &runtime) {
                lua_State *L = runtime.State();
                lua_pushcfunction(L, LuaLog);
                lua_setglobal(L, "log");
                lua_pushlightuserdata(L, this);
                lua_pushcclosure(L, CurrentResource, 1);
                lua_setglobal(L, "resource");
                lua_pushlightuserdata(L, this);
                lua_pushcclosure(L, Stop, 1);
                lua_setglobal(L, "stop");
            });
            if (options.withLua) {
                manager.SetLuaEngine(&lua);
            }
            manager.AddOnResourceStopped([this](const std::string &name) {
                stopped.push_back(name);
            });
            manager.AddOnResourceError([this](const std::string &name, const std::string &error) {
                errors.push_back(name + ": " + LuaRuntimeTest::FirstLine(error));
            });
        }

        static Framework::Scripting::ResourceManagerConfig Config(const Files &files, const Options &options) {
            Framework::Scripting::ResourceManagerConfig config;
            config.resourcesPath       = files.root.string();
            config.isClient            = options.isClient;
            config.devMode             = options.devMode;
            config.fileWatchIntervalMs = 0;
            return config;
        }

        static int CurrentResource(lua_State *L) {
            auto *self             = static_cast<Rig *>(lua_touserdata(L, lua_upvalueindex(1)));
            const std::string name = self->manager.GetCurrentResourceContext();
            lua_pushlstring(L, name.data(), name.size());
            return 1;
        }

        static int Stop(lua_State *L) {
            auto *self = static_cast<Rig *>(lua_touserdata(L, lua_upvalueindex(1)));
            self->manager.StopResource(luaL_checkstring(L, 1));
            return 0;
        }

        static void JavaScriptStop(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = static_cast<Rig *>(info.Data().As<v8::External>()->Value());
            v8::String::Utf8Value name(info.GetIsolate(), info[0]);
            self->manager.StopResource(*name ? *name : "");
        }

        // Discovers what was written and starts one resource. Empty when it started, the error otherwise.
        std::string Start(const std::string &resource) {
            if (!manager.HasResource(resource)) {
                manager.DiscoverResources();
            }
            const auto result = manager.StartResource(resource);
            return result ? std::string() : result.GetError();
        }

        // Moves the Lua clock on and ticks, then hands the manager the errors nobody waited for, as the server
        // module does.
        void Advance(int ms) {
            clock += std::chrono::milliseconds(ms);
            lua.Tick(clock);
            for (const auto &error : lua.DrainPendingErrors()) {
                manager.HandleResourceRuntimeError(error.resourceName, error.errorMessage);
            }
        }

        bool HasLuaRuntime(const std::string &resource) const {
            return lua.GetRuntime(resource) != nullptr;
        }

        bool HasJavaScriptRuntime(const std::string &resource) const {
            return node.GetResourceRuntime(resource) != nullptr;
        }

        // The value of `expression` in the Lua runtime of the resource, as tostring() spells it.
        std::string Eval(const std::string &resource, const std::string &expression) const {
            Framework::Scripting::LuaRuntime *runtime = lua.GetRuntime(resource);
            return runtime != nullptr ? LuaRuntimeTest::Eval(*runtime, expression) : "<no runtime>";
        }
    };

    // A server scripting module over a resources directory of its own.
    struct Server final {
        Files files;
        Framework::Integrations::Server::Scripting::ServerScriptingModule module;

        // Brings the module up and starts what was written. False when either failed.
        bool Start() {
            module.SetResourcesPath(files.root.string());
            return module.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE && module.StartAllResources();
        }

        std::string Eval(const std::string &resource, const std::string &expression) const {
            Framework::Scripting::LuaEngine *engine   = module.GetLuaEngine();
            Framework::Scripting::LuaRuntime *runtime = engine != nullptr ? engine->GetRuntime(resource) : nullptr;
            return runtime != nullptr ? LuaRuntimeTest::Eval(*runtime, expression) : "<no runtime>";
        }

        // The value of `expression` in the JavaScript runtime of the resource, as a string.
        std::string EvalJavaScript(const std::string &resource, const std::string &expression) const {
            Framework::Scripting::NodeRuntime *runtime = module.GetEngine()->GetResourceRuntime(resource);
            return runtime != nullptr ? NodeTest::Eval(*runtime, expression) : "<no runtime>";
        }

        // Waits as the server does between two ticks, for `ms`. Gives the rounds the wait took: one when nothing
        // cut it short.
        uint64_t Wait(int ms) {
            const uint64_t before = module.GetWaitRounds();
            const auto deadline   = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            module.WaitUntil(deadline);
            return std::chrono::steady_clock::now() >= deadline ? module.GetWaitRounds() - before : 0;
        }

        // Updates the module as the server's ticks do until `expression` is `expected` in the resource, or time
        // runs out.
        bool UpdateUntil(const std::string &resource, const std::string &expression, const std::string &expected, int timeoutMs = 2000) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < deadline) {
                module.Update();
                if (Eval(resource, expression) == expected) {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        }
    };
} // namespace LuaResourcesTest

MODULE(lua_resources, {
    // ==================== Runtimes ====================

    IT("runs a Lua-only resource in a Lua runtime and gives it no JavaScript one", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("lua-only", {{"main.lua", "log('main ran for ' .. resource())"}});

        EQUALS(rig.Start("lua-only"), "");
        EQUALS(LuaResourcesTest::LogText(), "main ran for lua-only");
        EQUALS(rig.manager.IsResourceRunning("lua-only"), true);
        EQUALS(rig.HasLuaRuntime("lua-only"), true);
        EQUALS(rig.HasJavaScriptRuntime("lua-only"), false);
        EQUALS(rig.manager.GetCurrentResourceContext(), "");
        EQUALS(rig.manager.GetLuaEngine() == &rig.lua, true);
    });

    IT("gives a JavaScript-only resource no Lua runtime", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("js-only", {{"main.js", "log('main ran')"}});

        EQUALS(rig.Start("js-only"), "");
        EQUALS(LuaResourcesTest::LogText(), "main ran");
        EQUALS(rig.HasJavaScriptRuntime("js-only"), true);
        EQUALS(rig.HasLuaRuntime("js-only"), false);
    });

    IT("gives a resource with no script for this side no Lua runtime", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Manifest("idle", "\"clientScripts\":[\"client.lua\"]");
        rig.files.Write("idle", "client.lua", "log('client ran')");

        EQUALS(rig.Start("idle"), "");
        EQUALS(LuaResourcesTest::LogText(), "");
        EQUALS(rig.manager.IsResourceRunning("idle"), true);
        EQUALS(rig.HasLuaRuntime("idle"), false);
        // As before Lua: a resource with nothing to run still has its JavaScript runtime.
        EQUALS(rig.HasJavaScriptRuntime("idle"), true);
    });

    IT("runs the scripts of a mixed resource in manifest order across languages", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Write("mixed", "shared.lua", "local name = 'shared' log(name)");
        rig.files.Write("mixed", "a.js", "log('a')");
        rig.files.Write("mixed", "b.lua", "local name = 'b' log(name)");
        rig.files.Write("mixed", "c.js", "log('c')");
        rig.files.Manifest("mixed", R"("sharedScripts":["shared.lua"],"serverScripts":["a.js","b.lua","c.js"])");

        EQUALS(rig.Start("mixed"), "");
        EQUALS(LuaResourcesTest::LogText(), "shared,a,b,c");
        EQUALS(rig.HasJavaScriptRuntime("mixed"), true);
        EQUALS(rig.HasLuaRuntime("mixed"), true);

        EQUALS((bool)rig.manager.StopResource("mixed"), true);
        EQUALS(rig.HasJavaScriptRuntime("mixed"), false);
        EQUALS(rig.HasLuaRuntime("mixed"), false);
    });

    IT("runs a Lua-only resource for a manager that has no JavaScript engine", {
        LuaResourcesTest::Files files;
        files.Resource("lua-only", {{"main.lua", "greeting = 'hello'"}});
        files.Resource("js-only", {{"main.js", "globalThis.greeting = 'hello';"}});
        Framework::Scripting::LuaEngine lua;
        EQUALS(lua.Init(), Framework::Scripting::ScriptingError::SCRIPTING_NONE);
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath = files.root.string();
        Framework::Scripting::ResourceManager manager(nullptr, config);
        manager.SetLuaEngine(&lua);
        EQUALS(manager.DiscoverResources(), 2u);

        EQUALS((bool)manager.StartResource("lua-only"), true);
        EQUALS(manager.IsResourceRunning("lua-only"), true);
        EQUALS(LuaRuntimeTest::Eval(*lua.GetRuntime("lua-only"), "greeting"), "hello");
        EQUALS((bool)manager.StopResource("lua-only"), true);
        EQUALS(lua.GetRuntime("lua-only") == nullptr, true);

        // As it always was for a manager without one.
        EQUALS(manager.StartResource("js-only").GetError(), "JavaScript engine not initialized");
    });

    IT("tells the Lua engine which manager it works for", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        EQUALS(rig.lua.GetResourceManager() == &rig.manager, true);
        rig.manager.SetLuaEngine(nullptr);
        EQUALS(rig.lua.GetResourceManager() == nullptr, true);
        EQUALS(rig.manager.GetLuaEngine() == nullptr, true);
    });

    IT("runs the client scripts on a client", {
        LuaResourcesTest::Rig rig(LuaResourcesTest::Rig::Options {.isClient = true});
        EQUALS(rig.ready, true);
        rig.files.Write("sided", "client.lua", "local name = 'client' log(name)");
        rig.files.Write("sided", "server.js", "log('server')");
        rig.files.Manifest("sided", R"("clientScripts":["client.lua"],"serverScripts":["server.js"])");

        EQUALS(rig.Start("sided"), "");
        EQUALS(LuaResourcesTest::LogText(), "client");
        EQUALS(rig.HasLuaRuntime("sided"), true);
        EQUALS(rig.HasJavaScriptRuntime("sided"), false);
    });

    IT("gives the Lua engine the directory of the resource, for require and for chunk names", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Write("modular", "lib/greeting.lua", "return { text = 'hello from a module' }");
        rig.files.Resource("modular", {{"src/main.lua", "log(require('lib.greeting').text)\nwhere = select(2, pcall(function() error('x') end))"}});

        EQUALS(rig.Start("modular"), "");
        EQUALS(LuaResourcesTest::LogText(), "hello from a module");
        EQUALS(rig.Eval("modular", "where"), "modular/src/main.lua:2: x");
    });

    // ==================== Lifecycle ====================

    IT("destroys the Lua runtime on stop, and its interval fires no more", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("ticker", {{"main.lua", "setInterval(function() log('tick') end, 10)"}});

        EQUALS(rig.Start("ticker"), "");
        rig.Advance(1000);
        EQUALS(LuaResourcesTest::LogText(), "tick");

        EQUALS((bool)rig.manager.StopResource("ticker"), true);
        EQUALS(rig.manager.GetResourceState("ticker") == Framework::Scripting::ResourceState::Stopped, true);
        EQUALS(rig.HasLuaRuntime("ticker"), false);
        rig.Advance(1000);
        rig.Advance(1000);
        EQUALS(LuaResourcesTest::LogText(), "tick");
        EQUALS(rig.stopped.size(), 1u);
    });

    IT("starts a stopped Lua resource again in a new state", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("again", {{"main.lua", "log('counter is ' .. tostring(counter))\ncounter = 1"}});

        EQUALS(rig.Start("again"), "");
        EQUALS((bool)rig.manager.StopResource("again"), true);
        EQUALS(rig.Start("again"), "");
        EQUALS(LuaResourcesTest::LogText(), "counter is nil,counter is nil");
        EQUALS(rig.Eval("again", "counter"), "1");
    });

    IT("reads an edited file again on restart", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("edited", {{"main.lua", "log('one')\nleftover = true"}});
        EQUALS(rig.Start("edited"), "");

        rig.files.Write("edited", "main.lua", "log('two, leftover is ' .. tostring(leftover))");
        const auto result = rig.manager.RestartResource("edited");
        EQUALS((bool)result, true);
        EQUALS(result.GetValue().size(), 1u);
        EQUALS(LuaResourcesTest::LogText(), "one,two, leftover is nil");
        EQUALS(rig.manager.IsResourceRunning("edited"), true);
        EQUALS(rig.HasLuaRuntime("edited"), true);
    });

    IT("restarts both runtimes of a mixed resource", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("both", {{"a.js", "log('js ' + typeof globalThis.leftover); globalThis.leftover = 1;"}, {"b.lua", "log('lua ' .. tostring(leftover))\nleftover = 1"}});
        EQUALS(rig.Start("both"), "");

        EQUALS((bool)rig.manager.RestartResource("both"), true);
        EQUALS(LuaResourcesTest::LogText(), "js undefined,lua nil,js undefined,lua nil");
        EQUALS(rig.HasJavaScriptRuntime("both"), true);
        EQUALS(rig.HasLuaRuntime("both"), true);
    });

    IT("reloads a Lua resource on refresh, with the scripts its manifest names by then", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("ensured", {{"main.lua", "local name = 'main' log(name)"}});
        EQUALS(rig.Start("ensured"), "");

        // What `ensure` does for a resource that is running.
        rig.files.Resource("ensured", {{"main.lua", "local name = 'main again' log(name)"}, {"extra.lua", "local name = 'extra' log(name)"}});
        EQUALS((bool)rig.manager.RefreshResource("ensured"), true);
        EQUALS(LuaResourcesTest::LogText(), "main,main again,extra");
        EQUALS(rig.manager.IsResourceRunning("ensured"), true);
    });

    IT("reloads a Lua resource when the development file watcher sees a .lua file change", {
        LuaResourcesTest::Rig rig(LuaResourcesTest::Rig::Options {.devMode = true});
        EQUALS(rig.ready, true);
        rig.files.Write("watched", "lib/words.lua", "return 'before'");
        rig.files.Resource("watched", {{"main.lua", "log(require('lib.words'))"}});
        EQUALS(rig.Start("watched"), "");

        // The first poll only notes what is there.
        rig.manager.ProcessFileWatch();
        rig.manager.ProcessFileWatch();
        EQUALS(LuaResourcesTest::LogText(), "before");

        // A module, which no manifest names.
        rig.files.Write("watched", "lib/words.lua", "return 'after'");
        rig.manager.ProcessFileWatch();
        EQUALS(LuaResourcesTest::LogText(), "before,after");
        EQUALS(rig.manager.IsResourceRunning("watched"), true);

        rig.manager.ProcessFileWatch();
        EQUALS(LuaResourcesTest::LogText(), "before,after");
    });

    IT("puts off destroying the runtime of a Lua resource that stops itself", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("quitter", {{"main.lua", "setInterval(function() log('tick') end, 10)\nsetTimeout(function() stop('quitter') log('still here') end, 10)"}});
        EQUALS(rig.Start("quitter"), "");

        // The interval is the older timer: it fires, then the timeout stops the resource and runs to its end.
        rig.Advance(1000);
        EQUALS(LuaResourcesTest::LogText(), "tick,still here");
        EQUALS(rig.manager.GetResourceState("quitter") == Framework::Scripting::ResourceState::Stopped, true);
        EQUALS(rig.HasLuaRuntime("quitter"), false);

        rig.Advance(1000);
        EQUALS(LuaResourcesTest::LogText(), "tick,still here");

        // The name was free at once.
        EQUALS(rig.Start("quitter"), "");
        EQUALS(rig.HasLuaRuntime("quitter"), true);
    });

    IT("destroys the Lua runtime of a mixed resource that stops itself from JavaScript", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("quitter", {{"a.js", "setTimeout(() => { stop('quitter'); log('still here'); }, 1);"}, {"b.lua", "setInterval(function() log('tick') end, 10)"}});
        EQUALS(rig.Start("quitter"), "");

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (LuaResourcesTest::Log().empty() && std::chrono::steady_clock::now() < deadline) {
            rig.node.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EQUALS(LuaResourcesTest::LogText(), "still here");
        EQUALS(rig.manager.GetResourceState("quitter") == Framework::Scripting::ResourceState::Stopped, true);
        EQUALS(rig.HasLuaRuntime("quitter"), false);
        EQUALS(rig.HasJavaScriptRuntime("quitter"), false);

        rig.Advance(1000);
        rig.node.Tick();
        EQUALS(LuaResourcesTest::LogText(), "still here");
    });

    // ==================== A start that fails ====================

    IT("fails the start on a syntax error, naming the file and the line, and leaves no runtime", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("broken", {{"main.lua", "log('never')\nlocal = 5"}});

        const std::string error = rig.Start("broken");
        EQUALS(LuaRuntimeTest::Contains(error, "broken/main.lua:2:"), true);
        EQUALS(LuaResourcesTest::LogText(), "");
        EQUALS(rig.manager.GetResourceState("broken") == Framework::Scripting::ResourceState::Error, true);
        EQUALS(rig.HasLuaRuntime("broken"), false);
        EQUALS(rig.HasJavaScriptRuntime("broken"), false);
        EQUALS(rig.errors.size(), 1u);
        EQUALS(rig.stopped.size(), 1u);

        // Mended, it starts.
        rig.files.Write("broken", "main.lua", "log('mended')");
        EQUALS(rig.Start("broken"), "");
        EQUALS(LuaResourcesTest::LogText(), "mended");
    });

    IT("fails the start when a Lua file raises, and destroys both runtimes of a mixed resource", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("raises", {{"a.js", "log('a'); setInterval(() => log('js tick'), 1);"}, {"b.lua", "setInterval(function() log('lua tick') end, 10)\nerror('boom')"}, {"c.js", "log('c')"}});

        const std::string error = rig.Start("raises");
        EQUALS(LuaRuntimeTest::Contains(error, "raises/b.lua:2: boom"), true);
        EQUALS(LuaResourcesTest::LogText(), "a");
        EQUALS(rig.HasLuaRuntime("raises"), false);
        EQUALS(rig.HasJavaScriptRuntime("raises"), false);

        rig.Advance(1000);
        for (int i = 0; i < 10; ++i) {
            rig.node.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        EQUALS(LuaResourcesTest::LogText(), "a");
    });

    IT("fails the start of a resource with a .lua script when there is no Lua engine", {
        LuaResourcesTest::Rig rig(LuaResourcesTest::Rig::Options {.withLua = false});
        EQUALS(rig.ready, true);
        rig.files.Resource("lua-only", {{"main.lua", "log('never')"}});
        rig.files.Resource("mixed", {{"a.js", "log('a')"}, {"b.lua", "log('b')"}});
        EQUALS(rig.manager.GetLuaEngine() == nullptr, true);

        EQUALS(rig.Start("lua-only"), "Lua scripting is not available");
        EQUALS(rig.manager.GetResourceState("lua-only") == Framework::Scripting::ResourceState::Error, true);
        EQUALS(rig.HasJavaScriptRuntime("lua-only"), false);

        // Before any of its scripts ran.
        EQUALS(rig.Start("mixed"), "Lua scripting is not available");
        EQUALS(rig.HasJavaScriptRuntime("mixed"), false);
        EQUALS(LuaResourcesTest::LogText(), "");
        EQUALS(rig.errors.size(), 2u);
    });

    IT("says that Lua scripting is not available whichever engine is missing", {
        LuaResourcesTest::Files files;
        files.Resource("lua-only", {{"main.lua", "greeting = 'hello'"}});
        files.Resource("js-only", {{"main.js", "globalThis.greeting = 'hello';"}});
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath = files.root.string();

        // A Lua engine that was never initialised.
        Framework::Scripting::LuaEngine idle;
        Framework::Scripting::ResourceManager withIdle(nullptr, config);
        withIdle.SetLuaEngine(&idle);
        EQUALS(withIdle.DiscoverResources(), 2u);
        EQUALS(withIdle.StartResource("lua-only").GetError(), "Lua scripting is not available");
        EQUALS(withIdle.GetResourceState("lua-only") == Framework::Scripting::ResourceState::Error, true);
        EQUALS(idle.GetRuntime("lua-only") == nullptr, true);

        // No engine at all: each script still says which one it lacks.
        Framework::Scripting::ResourceManager bare(nullptr, config);
        EQUALS(bare.DiscoverResources(), 2u);
        EQUALS(bare.StartResource("lua-only").GetError(), "Lua scripting is not available");
        EQUALS(bare.StartResource("js-only").GetError(), "JavaScript engine not initialized");
    });

    IT("destroys the JavaScript runtime of a resource whose Lua runtime cannot be created", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.lua.SetRuntimeSetupCallback([](Framework::Scripting::LuaRuntime &) {
            throw std::runtime_error("no bindings today");
        });
        rig.files.Resource("unlucky", {{"a.js", "log('a')"}, {"b.lua", "log('b')"}});

        const std::string error = rig.Start("unlucky");
        EQUALS(LuaRuntimeTest::Contains(error, "no bindings today"), true);
        EQUALS(LuaResourcesTest::LogText(), "");
        EQUALS(rig.manager.GetResourceState("unlucky") == Framework::Scripting::ResourceState::Error, true);
        EQUALS(rig.HasJavaScriptRuntime("unlucky"), false);
        EQUALS(rig.HasLuaRuntime("unlucky"), false);
    });

    // ==================== Errors after the start ====================

    IT("stops a resource whose timer raises, as errorBehavior stop says", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("fragile", {{"main.lua", "setTimeout(function() error('boom') end, 10)\nsetInterval(function() log('tick') end, 2000)"}});
        EQUALS(rig.Start("fragile"), "");

        rig.Advance(1000);
        EQUALS(rig.errors.size(), 1u);
        EQUALS(LuaRuntimeTest::Contains(rig.errors[0], "fragile: fragile/main.lua:1: boom"), true);
        EQUALS(rig.manager.GetResourceState("fragile") == Framework::Scripting::ResourceState::Error, true);
        EQUALS(rig.HasLuaRuntime("fragile"), false);
        EQUALS(rig.stopped.size(), 1u);

        rig.Advance(5000);
        EQUALS(LuaResourcesTest::LogText(), "");
    });

    IT("keeps a resource whose timer raises running, as errorBehavior continue says", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("sturdy", {{"main.lua", "setTimeout(function() error('boom') end, 10)\nsetInterval(function() log('tick') end, 2000)"}}, R"(,"errorBehavior":"continue")");
        EQUALS(rig.Start("sturdy"), "");

        rig.Advance(1000);
        EQUALS(rig.errors.size(), 1u);
        EQUALS(rig.HasLuaRuntime("sturdy"), true);
        EQUALS(rig.stopped.empty(), true);

        rig.Advance(5000);
        EQUALS(LuaResourcesTest::LogText(), "tick");
    });

    IT("restarts a resource whose timer raises, as errorBehavior restart says", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("phoenix", {{"main.lua", "log('started')\nsetTimeout(function() error('boom') end, 10)"}}, R"(,"errorBehavior":"restart")");
        EQUALS(rig.Start("phoenix"), "");

        rig.Advance(1000);
        EQUALS(rig.errors.size(), 1u);
        EQUALS(rig.HasLuaRuntime("phoenix"), false);
        EQUALS(rig.stopped.size(), 1u);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!rig.manager.IsResourceRunning("phoenix") && std::chrono::steady_clock::now() < deadline) {
            rig.manager.ProcessScheduledRestarts();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EQUALS(rig.manager.IsResourceRunning("phoenix"), true);
        EQUALS(LuaResourcesTest::LogText(), "started,started");
        EQUALS(rig.HasLuaRuntime("phoenix"), true);
    });

    // ==================== A resource with no JavaScript runtime ====================

    IT("tells JavaScript resources of the start and the stop of a Lua-only resource", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("listener", {{"main.js", "Events.on('resourceStart', (name) => log('start ' + name)); Events.on('resourceStop', (name) => log('stop ' + name));"}});
        rig.files.Resource("lua-only", {{"main.lua", "local name = 'main' log(name)"}});
        EQUALS(rig.Start("listener"), "");
        LuaResourcesTest::Log().clear();

        EQUALS(rig.Start("lua-only"), "");
        EQUALS((bool)rig.manager.StopResource("lua-only"), true);
        EQUALS(LuaResourcesTest::LogText(), "main,start lua-only,stop lua-only");
    });

    IT("answers JavaScript that a Lua-only resource has no exports", {
        LuaResourcesTest::Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("lua-only", {{"main.lua", "local name = 'main' log(name)"}});
        rig.files.Resource("asker", {{"main.js", "try { Exports.get('lua-only', 'anything'); log('found'); } catch (error) { log(error.message); }"}});
        EQUALS(rig.Start("lua-only"), "");

        EQUALS(rig.Start("asker"), "");
        EQUALS(LuaResourcesTest::LogText(), "main,Exports.get: export 'anything' not found in resource 'lua-only'");
    });

    // ==================== The server module ====================

    IT("gives the Lua resources of a server the value types", {
        LuaResourcesTest::Server server;
        server.files.Resource("vectors", {{"main.lua", "result = Vector3.new(1, 2, 3):add(Vector3.new(4, 5, 6)):toString()"}});

        EQUALS(server.Start(), true);
        EQUALS(server.module.GetLuaEngine() != nullptr, true);
        EQUALS(server.module.GetResourceManager()->GetLuaEngine() == server.module.GetLuaEngine(), true);
        EQUALS(server.module.GetResourceManager()->IsResourceRunning("vectors"), true);
        EQUALS(server.Eval("vectors", "result"), "Vector3(5, 7, 9)");
        EQUALS(server.module.GetEngine()->GetResourceRuntime("vectors") == nullptr, true);

        server.module.Shutdown();
        EQUALS(server.module.GetLuaEngine() == nullptr, true);
    });

    IT("fires the Lua timers of a server from its update", {
        LuaResourcesTest::Server server;
        server.files.Resource("timed", {{"main.lua", "fired = 0\nsetInterval(function() fired = fired + 1 end, 1)"}});

        EQUALS(server.Start(), true);
        EQUALS(server.Eval("timed", "fired"), "0");
        EQUALS(server.UpdateUntil("timed", "fired >= 3", "true"), true);
    });

    IT("hands a server the error of a Lua timer, which stops the resource", {
        LuaResourcesTest::Server server;
        server.files.Resource("fragile", {{"main.lua", "setTimeout(function() error('boom') end, 1)"}});

        EQUALS(server.Start(), true);
        auto *manager = server.module.GetResourceManager();
        EQUALS(manager->IsResourceRunning("fragile"), true);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (manager->GetResourceState("fragile") != Framework::Scripting::ResourceState::Error && std::chrono::steady_clock::now() < deadline) {
            server.module.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EQUALS(manager->GetResourceState("fragile") == Framework::Scripting::ResourceState::Error, true);
        EQUALS(LuaRuntimeTest::Contains(manager->GetResource("fragile")->GetErrorMessage(), "fragile/main.lua:1: boom"), true);
        EQUALS(server.module.GetLuaEngine()->GetRuntime("fragile") == nullptr, true);
    });

    IT("fires a Lua timer that comes due while a server waits for its next tick", {
        LuaResourcesTest::Server server;
        // The second timer is armed when the first fires: both only fire in one wait if Lua is ticked when a timer
        // is due, not once at the end.
        server.files.Resource("waiting", {{"main.lua", "fired = ''\nsetTimeout(function() fired = fired .. 'first' setTimeout(function() fired = fired .. ',second' end, 20) end, 20)\nsetTimeout(function() fired = fired .. ',late' end, 60000)"}});
        EQUALS(server.Start(), true);
        server.module.Update();
        EQUALS(server.Eval("waiting", "fired"), "");

        // A round up to each of the two timers and one to the end of the wait, give or take a sleep that came
        // back early.
        const uint64_t rounds = server.Wait(600);
        EQUALS(server.Eval("waiting", "fired"), "first,second");
        GREATEREQ(rounds, 3u);
        LESSEREQ(rounds, 10u);
    });

    IT("hands a server the error of a Lua timer that comes due while it waits", {
        LuaResourcesTest::Server server;
        server.files.Resource("fragile", {{"main.lua", "setTimeout(function() error('boom') end, 10)"}});
        EQUALS(server.Start(), true);

        server.module.WaitUntil(std::chrono::steady_clock::now() + std::chrono::milliseconds(300));
        EQUALS(server.module.GetResourceManager()->GetResourceState("fragile") == Framework::Scripting::ResourceState::Error, true);
    });

    IT("sleeps through the wait of a server whose Lua timers are far off", {
        LuaResourcesTest::Server server;
        server.files.Resource("patient", {{"main.lua", "setTimeout(function() end, 60000)"}});
        EQUALS(server.Start(), true);
        server.module.Update();

        const uint64_t rounds = server.Wait(200);
        GREATEREQ(rounds, 1u);
        LESSEREQ(rounds, 2u);
    });

    IT("leaves what Lua waits no time for to the next update of a server, once for each", {
        LuaResourcesTest::Server server;
        server.files.Resource("spinner", {{"main.lua", "turns = 0\nspawn(function() while true do turns = turns + 1 sleep(0) end end)\nfires = 0\nlocal function again() fires = fires + 1 setTimeout(again, 0) end\nsetTimeout(again, 0)"}});
        EQUALS(server.Start(), true);
        EQUALS(server.Eval("spinner", "turns .. ' ' .. fires"), "1 0");
        server.module.Update();
        EQUALS(server.Eval("spinner", "turns .. ' ' .. fires"), "2 1");

        // Both are due again at once, and neither goes on before the server ticks, nor cuts the wait short.
        const uint64_t rounds = server.Wait(100);
        EQUALS(server.Eval("spinner", "turns .. ' ' .. fires"), "2 1");
        GREATEREQ(rounds, 1u);
        LESSEREQ(rounds, 2u);

        server.module.Update();
        EQUALS(server.Eval("spinner", "turns .. ' ' .. fires"), "3 2");
        server.module.Update();
        EQUALS(server.Eval("spinner", "turns .. ' ' .. fires"), "4 3");
        server.Wait(20);
        EQUALS(server.Eval("spinner", "turns .. ' ' .. fires"), "4 3");
    });

    IT("ticks Lua every few milliseconds at most while a server waits", {
        LuaResourcesTest::Server server;
        // An interval fires once in a tick however late it is, so it counts the ticks.
        server.files.Resource("eager", {{"main.lua", "ticks = 0\nsetInterval(function() ticks = ticks + 1 end, 1)"}});
        EQUALS(server.Start(), true);
        server.module.Update();
        const int before = std::stoi(server.Eval("eager", "ticks"));

        const uint64_t rounds = server.Wait(400);
        const int ticks       = std::stoi(server.Eval("eager", "ticks")) - before;
        // 400 ms at 4 ms a tick, where a tick for each millisecond would be 400.
        GREATEREQ(ticks, 3);
        LESSEREQ(ticks, 110);
        LESSEREQ(rounds, 130u);
    });

    IT("lets JavaScript run between the ticks of a server while Lua has timers that are always due", {
        LuaResourcesTest::Server server;
        server.files.Resource("busy", {{"main.lua", "turns = 0\nspawn(function() while true do turns = turns + 1 sleep(0) end end)\nticks = 0\nsetInterval(function() ticks = ticks + 1 end, 1)"}});
        server.files.Resource("clock", {{"main.js", "globalThis.fired = 'no'; setTimeout(() => { globalThis.fired = 'yes'; }, 20);"}});
        EQUALS(server.Start(), true);
        server.module.Update();
        EQUALS(server.EvalJavaScript("clock", "globalThis.fired"), "no");
        const std::string turns = server.Eval("busy", "turns");
        const int before        = std::stoi(server.Eval("busy", "ticks"));

        server.Wait(500);
        // The timer of the JavaScript resource fired in the wait, with no tick of the server.
        EQUALS(server.EvalJavaScript("clock", "globalThis.fired"), "yes");
        // So did the interval of the Lua one, and its loop waited for the server.
        GREATEREQ(std::stoi(server.Eval("busy", "ticks")) - before, 3);
        EQUALS(server.Eval("busy", "turns"), turns);
    });
})
