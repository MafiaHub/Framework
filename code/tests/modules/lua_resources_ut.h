/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_runtime_ut.h"
#include "resource_manager_ut.h"

#include "scripting/lua/lua_engine.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace LuaResourcesTest {
    using namespace Framework::Scripting;
    using LuaRuntimeTest::Contains;

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
        NodeEngine node;
        LuaEngine lua;
        // Last, so that it goes first: it stops what still runs, with both engines and the lists above still there.
        ResourceManager manager;

        struct Options final {
            bool withLua  = true;
            bool isClient = false;
            bool devMode  = false;
        };

        Rig(): Rig(Options {}) {}

        explicit Rig(Options options): manager(&node, Config(files, options)) {
            Log().clear();
            ready = node.Init() == ScriptingError::SCRIPTING_NONE && lua.Init() == ScriptingError::SCRIPTING_NONE;
            if (!ready) {
                return;
            }
            TestManagerHelper::RegisterEvents(node, manager, [this](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                InstallJavaScriptLog(isolate, context);
                context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "stop"), v8::Function::New(context, JavaScriptStop, v8::External::New(isolate, this)).ToLocalChecked()).Check();
            });
            lua.SetRuntimeSetupCallback([this](LuaRuntime &runtime) {
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

        static ResourceManagerConfig Config(const Files &files, const Options &options) {
            ResourceManagerConfig config;
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
            LuaRuntime *runtime = lua.GetRuntime(resource);
            return runtime != nullptr ? LuaRuntimeTest::Eval(*runtime, expression) : "<no runtime>";
        }
    };
} // namespace LuaResourcesTest

MODULE(lua_resources, {
    using namespace Framework::Scripting;
    using LuaResourcesTest::Contains;
    using LuaResourcesTest::Log;
    using LuaResourcesTest::LogText;
    using LuaResourcesTest::Rig;

    // ==================== Runtimes ====================

    IT("runs a Lua-only resource in a Lua runtime and gives it no JavaScript one", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("lua-only", {{"main.lua", "log('main ran for ' .. resource())"}});

        EQUALS(rig.Start("lua-only"), "");
        EQUALS(LogText(), "main ran for lua-only");
        EQUALS(rig.manager.IsResourceRunning("lua-only"), true);
        EQUALS(rig.HasLuaRuntime("lua-only"), true);
        EQUALS(rig.HasJavaScriptRuntime("lua-only"), false);
        EQUALS(rig.manager.GetCurrentResourceContext(), "");
        EQUALS(rig.manager.GetLuaEngine() == &rig.lua, true);
    });

    IT("gives a JavaScript-only resource no Lua runtime", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("js-only", {{"main.js", "log('main ran')"}});

        EQUALS(rig.Start("js-only"), "");
        EQUALS(LogText(), "main ran");
        EQUALS(rig.HasJavaScriptRuntime("js-only"), true);
        EQUALS(rig.HasLuaRuntime("js-only"), false);
    });

    IT("gives a resource with no script for this side no Lua runtime", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Manifest("idle", "\"clientScripts\":[\"client.lua\"]");
        rig.files.Write("idle", "client.lua", "log('client ran')");

        EQUALS(rig.Start("idle"), "");
        EQUALS(LogText(), "");
        EQUALS(rig.manager.IsResourceRunning("idle"), true);
        EQUALS(rig.HasLuaRuntime("idle"), false);
        // As before Lua: a resource with nothing to run still has its JavaScript runtime.
        EQUALS(rig.HasJavaScriptRuntime("idle"), true);
    });

    IT("runs the scripts of a mixed resource in manifest order across languages", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Write("mixed", "shared.lua", "local name = 'shared' log(name)");
        rig.files.Write("mixed", "a.js", "log('a')");
        rig.files.Write("mixed", "b.lua", "local name = 'b' log(name)");
        rig.files.Write("mixed", "c.js", "log('c')");
        rig.files.Manifest("mixed", R"("sharedScripts":["shared.lua"],"serverScripts":["a.js","b.lua","c.js"])");

        EQUALS(rig.Start("mixed"), "");
        EQUALS(LogText(), "shared,a,b,c");
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
        LuaEngine lua;
        EQUALS(lua.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = files.root.string();
        ResourceManager manager(nullptr, config);
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
        Rig rig;
        EQUALS(rig.ready, true);
        EQUALS(rig.lua.GetResourceManager() == &rig.manager, true);
        rig.manager.SetLuaEngine(nullptr);
        EQUALS(rig.lua.GetResourceManager() == nullptr, true);
        EQUALS(rig.manager.GetLuaEngine() == nullptr, true);
    });

    IT("runs the client scripts on a client", {
        Rig rig(Rig::Options {.isClient = true});
        EQUALS(rig.ready, true);
        rig.files.Write("sided", "client.lua", "local name = 'client' log(name)");
        rig.files.Write("sided", "server.js", "log('server')");
        rig.files.Manifest("sided", R"("clientScripts":["client.lua"],"serverScripts":["server.js"])");

        EQUALS(rig.Start("sided"), "");
        EQUALS(LogText(), "client");
        EQUALS(rig.HasLuaRuntime("sided"), true);
        EQUALS(rig.HasJavaScriptRuntime("sided"), false);
    });

    IT("gives the Lua engine the directory of the resource, for require and for chunk names", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Write("modular", "lib/greeting.lua", "return { text = 'hello from a module' }");
        rig.files.Resource("modular", {{"src/main.lua", "log(require('lib.greeting').text)\nwhere = select(2, pcall(function() error('x') end))"}});

        EQUALS(rig.Start("modular"), "");
        EQUALS(LogText(), "hello from a module");
        EQUALS(rig.Eval("modular", "where"), "modular/src/main.lua:2: x");
    });

    // ==================== Lifecycle ====================

    IT("destroys the Lua runtime on stop, and its interval fires no more", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("ticker", {{"main.lua", "setInterval(function() log('tick') end, 10)"}});

        EQUALS(rig.Start("ticker"), "");
        rig.Advance(1000);
        EQUALS(LogText(), "tick");

        EQUALS((bool)rig.manager.StopResource("ticker"), true);
        EQUALS(rig.manager.GetResourceState("ticker") == ResourceState::Stopped, true);
        EQUALS(rig.HasLuaRuntime("ticker"), false);
        rig.Advance(1000);
        rig.Advance(1000);
        EQUALS(LogText(), "tick");
        EQUALS(rig.stopped.size(), 1u);
    });

    IT("starts a stopped Lua resource again in a new state", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("again", {{"main.lua", "log('counter is ' .. tostring(counter))\ncounter = 1"}});

        EQUALS(rig.Start("again"), "");
        EQUALS((bool)rig.manager.StopResource("again"), true);
        EQUALS(rig.Start("again"), "");
        EQUALS(LogText(), "counter is nil,counter is nil");
        EQUALS(rig.Eval("again", "counter"), "1");
    });

    IT("reads an edited file again on restart", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("edited", {{"main.lua", "log('one')\nleftover = true"}});
        EQUALS(rig.Start("edited"), "");

        rig.files.Write("edited", "main.lua", "log('two, leftover is ' .. tostring(leftover))");
        const auto result = rig.manager.RestartResource("edited");
        EQUALS((bool)result, true);
        EQUALS(result.GetValue().size(), 1u);
        EQUALS(LogText(), "one,two, leftover is nil");
        EQUALS(rig.manager.IsResourceRunning("edited"), true);
        EQUALS(rig.HasLuaRuntime("edited"), true);
    });

    IT("restarts both runtimes of a mixed resource", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("both", {{"a.js", "log('js ' + typeof globalThis.leftover); globalThis.leftover = 1;"}, {"b.lua", "log('lua ' .. tostring(leftover))\nleftover = 1"}});
        EQUALS(rig.Start("both"), "");

        EQUALS((bool)rig.manager.RestartResource("both"), true);
        EQUALS(LogText(), "js undefined,lua nil,js undefined,lua nil");
        EQUALS(rig.HasJavaScriptRuntime("both"), true);
        EQUALS(rig.HasLuaRuntime("both"), true);
    });

    IT("reloads a Lua resource on refresh, with the scripts its manifest names by then", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("ensured", {{"main.lua", "local name = 'main' log(name)"}});
        EQUALS(rig.Start("ensured"), "");

        // What `ensure` does for a resource that is running.
        rig.files.Resource("ensured", {{"main.lua", "local name = 'main again' log(name)"}, {"extra.lua", "local name = 'extra' log(name)"}});
        EQUALS((bool)rig.manager.RefreshResource("ensured"), true);
        EQUALS(LogText(), "main,main again,extra");
        EQUALS(rig.manager.IsResourceRunning("ensured"), true);
    });

    IT("reloads a Lua resource when the development file watcher sees a .lua file change", {
        Rig rig(Rig::Options {.devMode = true});
        EQUALS(rig.ready, true);
        rig.files.Write("watched", "lib/words.lua", "return 'before'");
        rig.files.Resource("watched", {{"main.lua", "log(require('lib.words'))"}});
        EQUALS(rig.Start("watched"), "");

        // The first poll only notes what is there.
        rig.manager.ProcessFileWatch();
        rig.manager.ProcessFileWatch();
        EQUALS(LogText(), "before");

        // A module, which no manifest names.
        rig.files.Write("watched", "lib/words.lua", "return 'after'");
        rig.manager.ProcessFileWatch();
        EQUALS(LogText(), "before,after");
        EQUALS(rig.manager.IsResourceRunning("watched"), true);

        rig.manager.ProcessFileWatch();
        EQUALS(LogText(), "before,after");
    });

    IT("puts off destroying the runtime of a Lua resource that stops itself", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("quitter", {{"main.lua", "setInterval(function() log('tick') end, 10)\nsetTimeout(function() stop('quitter') log('still here') end, 10)"}});
        EQUALS(rig.Start("quitter"), "");

        // The interval is the older timer: it fires, then the timeout stops the resource and runs to its end.
        rig.Advance(1000);
        EQUALS(LogText(), "tick,still here");
        EQUALS(rig.manager.GetResourceState("quitter") == ResourceState::Stopped, true);
        EQUALS(rig.HasLuaRuntime("quitter"), false);

        rig.Advance(1000);
        EQUALS(LogText(), "tick,still here");

        // The name was free at once.
        EQUALS(rig.Start("quitter"), "");
        EQUALS(rig.HasLuaRuntime("quitter"), true);
    });

    IT("destroys the Lua runtime of a mixed resource that stops itself from JavaScript", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("quitter", {{"a.js", "setTimeout(() => { stop('quitter'); log('still here'); }, 1);"}, {"b.lua", "setInterval(function() log('tick') end, 10)"}});
        EQUALS(rig.Start("quitter"), "");

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (Log().empty() && std::chrono::steady_clock::now() < deadline) {
            rig.node.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EQUALS(LogText(), "still here");
        EQUALS(rig.manager.GetResourceState("quitter") == ResourceState::Stopped, true);
        EQUALS(rig.HasLuaRuntime("quitter"), false);
        EQUALS(rig.HasJavaScriptRuntime("quitter"), false);

        rig.Advance(1000);
        rig.node.Tick();
        EQUALS(LogText(), "still here");
    });

    // ==================== A start that fails ====================

    IT("fails the start on a syntax error, naming the file and the line, and leaves no runtime", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("broken", {{"main.lua", "log('never')\nlocal = 5"}});

        const std::string error = rig.Start("broken");
        EQUALS(Contains(error, "broken/main.lua:2:"), true);
        EQUALS(LogText(), "");
        EQUALS(rig.manager.GetResourceState("broken") == ResourceState::Error, true);
        EQUALS(rig.HasLuaRuntime("broken"), false);
        EQUALS(rig.HasJavaScriptRuntime("broken"), false);
        EQUALS(rig.errors.size(), 1u);
        EQUALS(rig.stopped.size(), 1u);

        // Mended, it starts.
        rig.files.Write("broken", "main.lua", "log('mended')");
        EQUALS(rig.Start("broken"), "");
        EQUALS(LogText(), "mended");
    });

    IT("fails the start when a Lua file raises, and destroys both runtimes of a mixed resource", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("raises", {{"a.js", "log('a'); setInterval(() => log('js tick'), 1);"}, {"b.lua", "setInterval(function() log('lua tick') end, 10)\nerror('boom')"}, {"c.js", "log('c')"}});

        const std::string error = rig.Start("raises");
        EQUALS(Contains(error, "raises/b.lua:2: boom"), true);
        EQUALS(LogText(), "a");
        EQUALS(rig.HasLuaRuntime("raises"), false);
        EQUALS(rig.HasJavaScriptRuntime("raises"), false);

        rig.Advance(1000);
        for (int i = 0; i < 10; ++i) {
            rig.node.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        EQUALS(LogText(), "a");
    });

    IT("fails the start of a resource with a .lua script when there is no Lua engine", {
        Rig rig(Rig::Options {.withLua = false});
        EQUALS(rig.ready, true);
        rig.files.Resource("lua-only", {{"main.lua", "log('never')"}});
        rig.files.Resource("mixed", {{"a.js", "log('a')"}, {"b.lua", "log('b')"}});
        EQUALS(rig.manager.GetLuaEngine() == nullptr, true);

        EQUALS(rig.Start("lua-only"), "Lua scripting is not available");
        EQUALS(rig.manager.GetResourceState("lua-only") == ResourceState::Error, true);
        EQUALS(rig.HasJavaScriptRuntime("lua-only"), false);

        // Before any of its scripts ran.
        EQUALS(rig.Start("mixed"), "Lua scripting is not available");
        EQUALS(rig.HasJavaScriptRuntime("mixed"), false);
        EQUALS(LogText(), "");
        EQUALS(rig.errors.size(), 2u);
    });

    IT("destroys the JavaScript runtime of a resource whose Lua runtime cannot be created", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.lua.SetRuntimeSetupCallback([](LuaRuntime &) {
            throw std::runtime_error("no bindings today");
        });
        rig.files.Resource("unlucky", {{"a.js", "log('a')"}, {"b.lua", "log('b')"}});

        const std::string error = rig.Start("unlucky");
        EQUALS(Contains(error, "no bindings today"), true);
        EQUALS(LogText(), "");
        EQUALS(rig.manager.GetResourceState("unlucky") == ResourceState::Error, true);
        EQUALS(rig.HasJavaScriptRuntime("unlucky"), false);
        EQUALS(rig.HasLuaRuntime("unlucky"), false);
    });

    // ==================== Errors after the start ====================

    IT("stops a resource whose timer raises, as errorBehavior stop says", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("fragile", {{"main.lua", "setTimeout(function() error('boom') end, 10)\nsetInterval(function() log('tick') end, 2000)"}});
        EQUALS(rig.Start("fragile"), "");

        rig.Advance(1000);
        EQUALS(rig.errors.size(), 1u);
        EQUALS(Contains(rig.errors[0], "fragile: fragile/main.lua:1: boom"), true);
        EQUALS(rig.manager.GetResourceState("fragile") == ResourceState::Error, true);
        EQUALS(rig.HasLuaRuntime("fragile"), false);
        EQUALS(rig.stopped.size(), 1u);

        rig.Advance(5000);
        EQUALS(LogText(), "");
    });

    IT("keeps a resource whose timer raises running, as errorBehavior continue says", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("sturdy", {{"main.lua", "setTimeout(function() error('boom') end, 10)\nsetInterval(function() log('tick') end, 2000)"}}, R"(,"errorBehavior":"continue")");
        EQUALS(rig.Start("sturdy"), "");

        rig.Advance(1000);
        EQUALS(rig.errors.size(), 1u);
        EQUALS(rig.HasLuaRuntime("sturdy"), true);
        EQUALS(rig.stopped.empty(), true);

        rig.Advance(5000);
        EQUALS(LogText(), "tick");
    });

    IT("restarts a resource whose timer raises, as errorBehavior restart says", {
        Rig rig;
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
        EQUALS(LogText(), "started,started");
        EQUALS(rig.HasLuaRuntime("phoenix"), true);
    });

    // ==================== A resource with no JavaScript runtime ====================

    IT("tells JavaScript resources of the start and the stop of a Lua-only resource", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("listener", {{"main.js", "Events.on('resourceStart', (name) => log('start ' + name)); Events.on('resourceStop', (name) => log('stop ' + name));"}});
        rig.files.Resource("lua-only", {{"main.lua", "local name = 'main' log(name)"}});
        EQUALS(rig.Start("listener"), "");
        Log().clear();

        EQUALS(rig.Start("lua-only"), "");
        EQUALS((bool)rig.manager.StopResource("lua-only"), true);
        EQUALS(LogText(), "main,start lua-only,stop lua-only");
    });

    IT("answers JavaScript that a Lua-only resource has no exports", {
        Rig rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("lua-only", {{"main.lua", "local name = 'main' log(name)"}});
        rig.files.Resource("asker", {{"main.js", "try { Exports.get('lua-only', 'anything'); log('found'); } catch (error) { log(error.message); }"}});
        EQUALS(rig.Start("lua-only"), "");

        EQUALS(rig.Start("asker"), "");
        EQUALS(LogText(), "main,Exports.get: export 'anything' not found in resource 'lua-only'");
    });
})
