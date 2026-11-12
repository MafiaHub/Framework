/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// What a script pays for a bound call in JavaScript and in Lua: a Vector3 built, a method and a property of
// it, and an event emitted to one handler, in the same resource and in a resource of either language.
//
// The resources are real ones, started by a server scripting module from a directory it writes. Each loop
// runs inside the script, so a number is the binding and the interpreter's loop around it. Build it in
// RelWithDebInfo or Release; a Debug number says little.

#include "integrations/server/scripting/module.h"
#include "logging/logger.h"
#include "scripting/lua/lua_engine.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/node_engine.h"
#include "scripting/node_runtime.h"
#include "scripting/resource/resource_manager.h"

#include <lua.h>

#include <lauxlib.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {
    using Clock = std::chrono::steady_clock;
    using namespace Framework::Scripting;
    using Framework::Integrations::Server::Scripting::ServerScriptingModule;

    // Every resource counts what it hears on its own event and runs a named loop `n` times.
    const char *const kJavaScript = R"(
        globalThis.heard = 0;
        Events.on('bench:' + NAME, () => { globalThis.heard++; });
        const v = new Vector3(1, 2, 3), w = new Vector3(4, 5, 6);
        globalThis.bench = {
            construct(n) { let r; for (let i = 0; i < n; i++) r = new Vector3(i, 2, 3); return r.x; },
            method(n) { let r; for (let i = 0; i < n; i++) r = v.add(w); return r.x; },
            property(n) { let s = 0; for (let i = 0; i < n; i++) s += v.x; return s; },
            emit(n, to) { const name = 'bench:' + to; for (let i = 0; i < n; i++) Events.emit(name, i); return n; },
        };
    )";

    const char *const kLua = R"(
        heard = 0
        Events.on('bench:' .. NAME, function() heard = heard + 1 end)
        local v, w = Vector3.new(1, 2, 3), Vector3.new(4, 5, 6)
        bench = {
            construct = function(n) local r for i = 1, n do r = Vector3.new(i, 2, 3) end return r.x end,
            method = function(n) local r for i = 1, n do r = v:add(w) end return r.x end,
            property = function(n) local s = 0 for i = 1, n do s = s + v.x end return s end,
            emit = function(n, to) local name = 'bench:' .. to for i = 1, n do Events.emit(name, i) end return n end,
        }
    )";

    struct Server final {
        std::filesystem::path root = std::filesystem::temp_directory_path() / ("framework_binding_bench_" + std::to_string(std::random_device {}()));
        ServerScriptingModule module;

        ~Server() {
            module.PreShutdown();
            module.Shutdown();
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }

        void Resource(const std::string &name, bool lua) const {
            const std::string file = lua ? "main.lua" : "main.js";
            std::filesystem::create_directories(root / name);
            std::ofstream(root / name / "package.json") << "{\"name\":\"" << name << "\",\"version\":\"1.0.0\",\"mafiahub\":{\"serverScripts\":[\"" << file << "\"]}}";
            std::ofstream(root / name / file) << (lua ? "local NAME = '" : "const NAME = '") << name << "'\n" << (lua ? kLua : kJavaScript);
        }

        bool Start() {
            Resource("js-a", false);
            Resource("js-b", false);
            Resource("lua-a", true);
            Resource("lua-b", true);
            module.SetResourcesPath(root.string());
            return module.Init() == ScriptingError::SCRIPTING_NONE && module.StartAllResources();
        }

        static bool IsLua(const std::string &resource) {
            return resource.rfind("lua", 0) == 0;
        }

        // The number `expression` gives in the resource, or -1 when it failed.
        double Eval(const std::string &resource, const std::string &expression) {
            if (IsLua(resource)) {
                LuaRuntime *runtime     = module.GetLuaEngine()->GetRuntime(resource);
                lua_State *L            = runtime->State();
                const std::string chunk = "return " + expression;
                std::string error;
                if (luaL_loadbufferx(L, chunk.data(), chunk.size(), "=bench", "t") != LUA_OK) {
                    std::printf("%s: %s\n", resource.c_str(), lua_tostring(L, -1));
                    lua_pop(L, 1);
                    return -1;
                }
                if (!runtime->Call(0, 1, error)) {
                    std::printf("%s: %s\n", resource.c_str(), error.c_str());
                    return -1;
                }
                const double result = lua_tonumber(L, -1);
                lua_pop(L, 1);
                return result;
            }

            NodeRuntime *runtime = module.GetEngine()->GetResourceRuntime(resource);
            v8::Isolate *isolate = runtime->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = runtime->GetContext();
            v8::Context::Scope contextScope(context);
            v8::TryCatch tryCatch(isolate);
            v8::Local<v8::Script> script;
            v8::Local<v8::Value> result;
            if (!v8::Script::Compile(context, v8::String::NewFromUtf8(isolate, expression.c_str()).ToLocalChecked()).ToLocal(&script) || !script->Run(context).ToLocal(&result)) {
                v8::String::Utf8Value text(isolate, tryCatch.Exception());
                std::printf("%s: %s\n", resource.c_str(), *text ? *text : "threw");
                return -1;
            }
            return result->NumberValue(context).FromMaybe(-1);
        }

        void Report(const char *what, const std::string &resource, const std::string &call, int iterations) {
            Eval(resource, "bench." + call + "(" + std::to_string(iterations / 10) + ")");
            const auto start    = Clock::now();
            const double result = Eval(resource, "bench." + call + "(" + std::to_string(iterations) + ")");
            const double ns     = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
            std::printf("%-22s %-11s %10.0f ns/op%s\n", what, IsLua(resource) ? "Lua" : "JavaScript", ns / iterations, result < 0 ? "  (failed)" : "");
        }

        // An emission is timed up to the moment its handler has run: the module is updated until all were heard.
        void ReportEmit(const std::string &from, const std::string &to, int iterations) {
            const double before = Eval(to, "heard");
            const auto start    = Clock::now();
            Eval(from, "bench.emit(" + std::to_string(iterations) + ", '" + to + "')");
            const auto deadline = start + std::chrono::seconds(30);
            double heard        = Eval(to, "heard") - before;
            while (heard < iterations && Clock::now() < deadline) {
                module.Update();
                heard = Eval(to, "heard") - before;
            }
            const double ns        = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
            const std::string what = from == to ? "same resource" : "another resource";
            const std::string pair = std::string(IsLua(from) ? "Lua" : "JS") + " -> " + (IsLua(to) ? "Lua" : "JS");
            std::printf("Events.emit, %-16s %-11s %10.0f ns/op%s\n", what.c_str(), pair.c_str(), ns / iterations, heard < iterations ? "  (not all heard)" : "");
        }
    };
} // namespace

int main() {
    Framework::Logging::GetInstance()->PauseLogging(true);

    Server server;
    if (!server.Start()) {
        std::printf("the resources did not start\n");
        return 1;
    }

    for (const char *resource : {"js-a", "lua-a"}) {
        server.Report("new Vector3", resource, "construct", 200000);
        server.Report("vector.add(other)", resource, "method", 200000);
        server.Report("vector.x", resource, "property", 1000000);
    }
    server.ReportEmit("js-a", "js-a", 20000);
    server.ReportEmit("lua-a", "lua-a", 20000);
    server.ReportEmit("js-a", "js-b", 20000);
    server.ReportEmit("lua-a", "lua-b", 20000);
    server.ReportEmit("js-a", "lua-a", 20000);
    server.ReportEmit("lua-a", "js-a", 20000);
    return 0;
}
