/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "alloc_counter.h"
#include "lua_resources_ut.h"
#include "lua_runtime_ut.h"
#include "node_test_helpers.h"

#include "scripting/bind/call.h"
#include "scripting/builtins/builtins.h"
#include "scripting/builtins/console.h"
#include "scripting/builtins/execution_environment.h"
#include "scripting/lua/lua_engine.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/scripting_catalog.h"
#include "utils/version.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Console and ExecutionEnvironment, declared once for JavaScript and Lua: what a Lua script gets of them, held
// against what a JavaScript one gets.
namespace LuaBuiltinsCoreTest {
    // The resource the scripts of both languages run as.
    constexpr const char *kResource   = "core";
    constexpr const char *kModVersion = "1.2.3-rc.1";

    inline std::string Joined(const std::vector<std::string> &lines) {
        std::string text;
        for (const std::string &line : lines) {
            text += (text.empty() ? "" : "\n") + line;
        }
        return text;
    }

    // What the resource wrote to the log, each line as "<level> <text>", joined with newlines.
    inline std::string Written(LuaRuntimeTest::LogCapture &log, const std::string &resource = kResource) {
        std::vector<std::string> lines;
        const std::string tag = " [" + resource + "] ";
        for (const std::string &line : log.LeveledLines()) {
            if (const size_t at = line.find(tag); at != std::string::npos && line.find(' ') == at) {
                lines.push_back(line.substr(0, at) + " " + line.substr(at + tag.size()));
            }
        }
        return Joined(lines);
    }

    // A Lua engine whose runtimes get the value types, the console and ExecutionEnvironment the way the server
    // module gives them, with one runtime of the resource.
    struct LuaSide final {
        Framework::Scripting::LuaEngine engine;

        // `memoryLimitBytes` is the cap of the state, or 0 for the one the engine gives by default.
        explicit LuaSide(Framework::Scripting::Bind::Side side = Framework::Scripting::Bind::Side::Server, const std::string &modVersion = kModVersion, size_t memoryLimitBytes = 0): engine(Options(side, memoryLimitBytes)) {
            engine.SetRuntimeSetupCallback([side, modVersion](Framework::Scripting::LuaRuntime &runtime) {
                Framework::Scripting::Builtins::RegisterValueTypesLua(runtime.State(), side);
                Framework::Scripting::Builtins::Console::RegisterLua(runtime);
                Framework::Scripting::Builtins::ExecutionEnvironment::RegisterLua(runtime, modVersion);
            });
            ready = engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE && engine.CreateResourceRuntime(kResource);
        }

        static Framework::Scripting::LuaEngineOptions Options(Framework::Scripting::Bind::Side side, size_t memoryLimitBytes = 0) {
            Framework::Scripting::LuaEngineOptions options;
            options.side = side;
            if (memoryLimitBytes != 0) {
                options.memoryLimitBytes = memoryLimitBytes;
            }
            return options;
        }

        Framework::Scripting::LuaRuntime &Runtime() {
            return *engine.GetRuntime(kResource);
        }

        // Empty when the chunk ran, its error otherwise.
        std::string Run(const std::string &source) {
            return LuaRuntimeTest::Run(Runtime(), source);
        }

        std::string Eval(const std::string &expression) {
            return LuaRuntimeTest::Eval(Runtime(), expression);
        }

        // The lines `source` writes, as the print handler gets them.
        std::string Printed(const std::string &source) {
            LuaRuntimeTest::Printed printed(Runtime());
            const std::string error = Run(source);
            Runtime().SetPrintHandler({});
            return error.empty() ? Joined(*printed.lines) : "<error> " + LuaRuntimeTest::FirstLine(error);
        }

        bool ready = false;
    };

    // A JavaScript runtime of an engine of its own, taken down in the order the caches kept by isolate need.
    struct JsRuntime final {
        Framework::Scripting::NodeEngine engine;
        std::unique_ptr<Framework::Scripting::NodeRuntime> runtime;

        JsRuntime() {
            engine.Init();
            std::string error;
            runtime = engine.CreateRuntime(error);
        }

        ~JsRuntime() {
            // The caches and the catalog are keyed by isolate address, which a later isolate may reuse.
            Framework::Scripting::Builtins::UnregisterAll(Isolate());
            Framework::Scripting::ClearScriptingCatalog(Isolate());
            runtime.reset();
            engine.Shutdown();
        }

        v8::Isolate *Isolate() const {
            return runtime->GetIsolate();
        }

        // Runs `action(isolate, context)` inside the runtime.
        template <typename Action>
        void Enter(Action action) {
            v8::Isolate *isolate = Isolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = runtime->GetContext();
            v8::Context::Scope contextScope(context);
            action(isolate, context);
        }

        // What `source` gives, as String() spells it, or "<threw> " and the exception.
        std::string Eval(const std::string &source) {
            return NodeTest::Eval(*runtime, source);
        }
    };

    // A JavaScript runtime with the value types, the console and ExecutionEnvironment registered through their
    // Register, under a manager that says which resource is running.
    struct JsSide final {
        JsRuntime js;
        Framework::Scripting::ResourceManager manager;

        explicit JsSide(bool isClient = false, const std::string &modVersion = kModVersion): manager(&js.engine) {
            static int count = 0;
            Framework::Scripting::SetScriptingCatalog(js.Isolate(), "lua_builtins_core/" + std::to_string(++count));
            Framework::Scripting::SetScriptingEnvironment(js.Isolate(), isClient);
            js.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                Framework::Scripting::Builtins::RegisterValueTypes(isolate, context->Global());
                Framework::Scripting::Builtins::Console::Register(isolate, context, &manager);
                Framework::Scripting::Builtins::ExecutionEnvironment::Register(isolate, context, context->Global(), isClient, modVersion);
            });
            manager.SetCurrentResourceContext(kResource);
        }

        std::string Eval(const std::string &source) {
            return js.Eval(source);
        }
    };

    // The names of the functions and of the properties a definition declares, in the order it declares them.
    inline std::string Members(const Framework::Scripting::Bind::Namespace &definition) {
        std::string names;
        for (const Framework::Scripting::Bind::FunctionDesc &function : definition.Describe().functions) {
            names += (names.empty() ? "" : ",") + function.name + "()";
        }
        for (const Framework::Scripting::Bind::PropertyDesc &property : definition.Describe().properties) {
            names += (names.empty() ? "" : ",") + property.name;
        }
        return names;
    }
} // namespace LuaBuiltinsCoreTest

MODULE(lua_builtins_core, {
    // ==================== console ====================

    IT("gives a Lua state the console of JavaScript, under the same names", {
        EQUALS(LuaBuiltinsCoreTest::Members(Framework::Scripting::Builtins::Console::Definition()), "log(),info(),warn(),error(),debug()");

        LuaBuiltinsCoreTest::LuaSide lua;
        EQUALS(lua.ready, true);
        LuaBuiltinsCoreTest::JsSide js;
        std::string types;
        for (const Framework::Scripting::Bind::FunctionDesc &function : Framework::Scripting::Builtins::Console::Definition().Describe().functions) {
            types += lua.Eval("type(console." + function.name + ")") + "/" + js.Eval("typeof console." + function.name) + " ";
        }
        EQUALS(types, "function/function function/function function/function function/function function/function ");

        // In JavaScript info is log itself under a second name, as it always was.
        EQUALS(js.Eval("console.log === console.info"), "true");
        EQUALS(js.Eval("console.log === console.warn || console.warn === console.error || console.error === console.debug || console.debug === console.log"), "false");

        // A script reads the console and cannot replace a function of it.
        EQUALS(lua.Eval("type(console.trace)"), "nil");
        EQUALS(lua.Eval("(function() console.log = print end)()"), "<error> eval:1: attempt to set 'log' on console");
        EQUALS(lua.Eval("select('#', console.log('nothing comes back'))"), "0");
        EQUALS(js.Eval("console.log('nothing comes back')"), "undefined");
    });

    IT("logs each line under the name of the resource, at the level of its function", {
        // One source for both languages.
        const std::string script   = "console.log('to log'); console.info('to info'); console.warn('to warn'); console.error('to error'); console.debug('to debug')";
        const std::string expected = "info to log\ninfo to info\nwarning to warn\nerror to error\ndebug to debug";

        LuaRuntimeTest::LogCapture log(spdlog::level::debug);
        LuaBuiltinsCoreTest::LuaSide lua;
        EQUALS(lua.Run(script), "");
        EQUALS(LuaBuiltinsCoreTest::Written(log), expected);

        // JavaScript logs the same lines for the same calls.
        LuaRuntimeTest::LogCapture again(spdlog::level::debug);
        LuaBuiltinsCoreTest::JsSide js;
        EQUALS(js.Eval(script + "; 0"), "0");
        EQUALS(LuaBuiltinsCoreTest::Written(again), expected);
    });

    IT("formats what it is given as the console of JavaScript does", {
        struct Case final {
            const char *lua;
            const char *js;
            const char *expected;
        };
        const Case cases[] = {
            {"'text'", "'text'", "text"},
            {"''", "''", ""},
            {"42", "42", "42"},
            {"-7", "-7", "-7"},
            {"2.0", "2.0", "2"},
            {"1.5", "1.5", "1.5"},
            {"1 / 3", "1 / 3", "0.333333"},
            {"1e300", "1e300", "1e+300"},
            {"true", "true", "true"},
            {"false", "false", "false"},
            {"{ 1, 2, 3 }", "[1, 2, 3]", "[Array(3)]"},
            {"{ x = 1 }", "{ x: 1 }", "{\"x\":1}"},
            {"{ list = { 1, 'two' }, nested = { flag = true } }", "{ list: [1, 'two'], nested: { flag: true } }", "{\"list\":[1,\"two\"],\"nested\":{\"flag\":true}}"},
            {"print", "Math.max", "[Function]"},
            {"function() end", "() => {}", "[Function]"},
            {"Vector3.new(1, 2, 3)", "new Vector3(1, 2, 3)", "{\"x\":1,\"y\":2,\"z\":3}"},
            {"(function() local t = {} t.self = t return t end)()", "(() => { const o = {}; o.self = o; return o; })()", "[Object]"},
            {"'a', 1, true, { 1 }", "'a', 1, true, [1]", "a 1 true [Array(1)]"},
            {"", "", ""},
        };

        LuaRuntimeTest::LogCapture log(spdlog::level::debug);
        LuaBuiltinsCoreTest::LuaSide lua;
        LuaBuiltinsCoreTest::JsSide js;
        std::string mismatches;
        std::string expected;
        for (const Case &entry : cases) {
            if (const std::string got = lua.Printed(std::string("console.log(") + entry.lua + ")"); got != entry.expected) {
                mismatches += "\n\tlua `" + std::string(entry.lua) + "` gave \"" + got + "\", expected \"" + entry.expected + "\"";
            }
            js.Eval(std::string("console.log(") + entry.js + "); 0");
            expected += std::string(expected.empty() ? "" : "\n") + "info " + entry.expected;
        }
        EQUALS(mismatches, "");
        EQUALS(LuaBuiltinsCoreTest::Written(log), expected);

        // Each language spells what it has for nothing, and what the other has no word for.
        EQUALS(lua.Printed("console.log('a', nil, 'b', nil)"), "a nil b nil");
        EQUALS(lua.Printed("console.log(math.maxinteger, math.mininteger)"), "9223372036854775807 -9223372036854775808");
        EQUALS(lua.Printed("console.log(coroutine.create(print))").starts_with("thread: "), true);
        // Nothing tells an empty table from an empty list, and JSON writes it as a list. A table that holds a
        // function has no JSON in Lua, where JavaScript leaves the function out.
        EQUALS(lua.Printed("console.log({})"), "[]");
        EQUALS(lua.Printed("console.log({ callback = print })"), "[Object]");
        // A function of a namespace is called with a dot. Called with a colon it is given the console as its
        // first value, which is an empty table to it.
        EQUALS(lua.Printed("console:log('x')"), "[] x");

        LuaRuntimeTest::LogCapture rest(spdlog::level::debug);
        js.Eval("console.log(null, undefined, [], {}, { callback: () => {} }, 10n, Symbol('s')); 0");
        EQUALS(LuaBuiltinsCoreTest::Written(rest), "info null undefined [Array(0)] {} {} 10 <unknown>");
    });

    IT("writes where print writes, within its bounds", {
        LuaBuiltinsCoreTest::LuaSide lua;
        EQUALS(lua.Printed("print('printed') console.log('logged') console.info('told') console.warn('warned') console.error('failed') console.debug('traced')"), "printed\nlogged\ntold\nwarned\nfailed\ntraced");

        // A line is cut where a printed one is, and gets no line of its own into the log.
        const std::string cut = lua.Printed("console.log(('x'):rep(3000), ('y'):rep(3000))");
        EQUALS(cut.size(), Framework::Scripting::LuaRuntime::kMaxLineBytes + LuaRuntimeTest::kTruncated.size());
        EQUALS(cut.starts_with(std::string(3000, 'x') + " " + std::string(1095, 'y') + std::string(LuaRuntimeTest::kTruncated)), true);
        EQUALS(lua.Printed("console.warn('first\\nsecond \\27[31mred\\ttab')"), "first second  [31mred\ttab");

        // Without a handler both go to the log, and share what a resource may write in a second.
        LuaRuntimeTest::LogCapture log(spdlog::level::debug);
        EQUALS(lua.Run("print('printed') console.log('logged') for i = 1, 150 do print('more') end for i = 1, 150 do console.error('less') end"), "");
        const std::vector<std::string> lines = log.LeveledLines();
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "info [core] printed"), 1u);
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "info [core] logged"), 1u);
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "info [core] more"), 150u);
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "error [core] less"), static_cast<size_t>(Framework::Scripting::LuaRuntime::kMaxLinesPerSecond) - 152u);
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "warning [core] output suppressed"), 1u);
    });

    IT("formats no more of its arguments than a line takes", {
        // A state of two megabytes, and lines whose arguments come to far more than that when they are all spelled:
        // two thousand times a string of 64 KB, and as many times a table whose JSON is 20 KB.
        LuaBuiltinsCoreTest::LuaSide lua(Framework::Scripting::Bind::Side::Server, LuaBuiltinsCoreTest::kModVersion, 2u << 20);
        EQUALS(lua.ready, true);
        EQUALS(lua.Run("big = ('x'):rep(65536)\nstrings = setmetatable({}, { __index = function() return big end })\nheavy = {}\nfor i = 1, 200 do heavy['key' .. i] = ('x'):rep(100) end\ntables = setmetatable({}, { __index = function() return heavy end })"), "");

        LuaRuntimeTest::Printed printed(lua.Runtime());
        // What the host allocates while the chunk runs. The state has its own allocator, which is not counted.
        const auto allocations = [&lua](const std::string &source) {
            FrameworkTests::AllocationScope scope;
            const std::string error = lua.Run(source);
            return error.empty() ? scope.Count() : UINT64_MAX;
        };
        const uint64_t oneString   = allocations("console.log(strings[1])");
        const uint64_t manyStrings = allocations("console.log(table.unpack(strings, 1, 2000))");
        const uint64_t oneTable    = allocations("console.log(tables[1])");
        const uint64_t manyTables  = allocations("console.log(table.unpack(tables, 1, 2000))");
        NEQUALS(oneString, UINT64_MAX);
        NEQUALS(oneTable, UINT64_MAX);
        LESSER(manyStrings, oneString + 50);
        LESSER(manyTables, oneTable + 50);

        // Each is the line a print of the same values is cut to.
        const std::string cut = std::string(Framework::Scripting::LuaRuntime::kMaxLineBytes, 'x') + std::string(LuaRuntimeTest::kTruncated);
        EQUALS(printed.lines->size(), 4u);
        EQUALS((*printed.lines)[0], cut);
        EQUALS((*printed.lines)[1], cut);
        EQUALS((*printed.lines)[2].starts_with("{\"key1\":\"xxxx"), true);
        EQUALS((*printed.lines)[3], (*printed.lines)[2]);

        // A line that is full to the byte is whole, and one more value makes it a cut one.
        EQUALS(lua.Run("console.log(('y'):rep(" + std::to_string(Framework::Scripting::LuaRuntime::kMaxLineBytes) + "))\nconsole.log(('y'):rep(" + std::to_string(Framework::Scripting::LuaRuntime::kMaxLineBytes) + "), '')\nconsole.log(('y'):rep("
                       + std::to_string(Framework::Scripting::LuaRuntime::kMaxLineBytes - 1) + "), 'z', 'z')"),
            "");
        EQUALS((*printed.lines)[4], std::string(Framework::Scripting::LuaRuntime::kMaxLineBytes, 'y'));
        EQUALS((*printed.lines)[5], std::string(Framework::Scripting::LuaRuntime::kMaxLineBytes, 'y') + std::string(LuaRuntimeTest::kTruncated));
        EQUALS((*printed.lines)[6], std::string(Framework::Scripting::LuaRuntime::kMaxLineBytes - 1, 'y') + " " + std::string(LuaRuntimeTest::kTruncated));

        // JavaScript has no bound, and gets every value.
        LuaRuntimeTest::LogCapture log(spdlog::level::debug);
        LuaBuiltinsCoreTest::JsSide js;
        js.Eval("console.log('j'.repeat(3000), 'k'.repeat(3000), 'end'); 0");
        EQUALS(LuaBuiltinsCoreTest::Written(log), "info " + std::string(3000, 'j') + " " + std::string(3000, 'k') + " end");
    });

    IT("keeps one function to format a value of JavaScript with, in a context", {
        LuaBuiltinsCoreTest::JsRuntime js;
        std::string number;
        std::string text;
        bool made = false;
        bool kept = false;
        js.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            // Where Console::FormatValue keeps it: on the global object, under a name no script can read.
            const v8::Local<v8::Private> key = v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.console.format"));
            number                           = Framework::Scripting::Builtins::Console::FormatValue(isolate, v8::Number::New(isolate, 42).As<v8::Value>());
            v8::Local<v8::Value> first;
            made = context->Global()->GetPrivate(context, key).ToLocal(&first) && first->IsFunction();
            text = Framework::Scripting::Builtins::Console::FormatValue(isolate, v8::String::NewFromUtf8Literal(isolate, "text").As<v8::Value>());
            v8::Local<v8::Value> second;
            kept = made && context->Global()->GetPrivate(context, key).ToLocal(&second) && second->StrictEquals(first);
        });
        EQUALS(number, "42");
        EQUALS(text, "text");
        EQUALS(made, true);
        EQUALS(kept, true);
        EQUALS(js.Eval("Object.getOwnPropertyNames(globalThis).concat(Object.getOwnPropertySymbols(globalThis).map(String)).filter((name) => name.includes('format')).length"), "0");
    });

    IT("names the resource its manager tells, and none without one", {
        LuaRuntimeTest::LogCapture log(spdlog::level::debug);
        LuaBuiltinsCoreTest::JsRuntime js;
        const auto registerConsole = [&js](Framework::Scripting::ResourceManager *manager) {
            js.Enter([manager](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                Framework::Scripting::Builtins::Console::Register(isolate, context, manager);
            });
        };

        {
            Framework::Scripting::ResourceManager manager(&js.engine);
            registerConsole(&manager);
            js.Eval("console.log('before any resource'); 0");
            manager.SetCurrentResourceContext("first");
            js.Eval("console.log('in a resource'); 0");

            // Registered again without a manager, the console keeps the one the isolate has.
            registerConsole(nullptr);
            js.Eval("console.warn('still in it'); 0");
        }
        // The manager is gone, and the console it was registered with goes on without a name.
        js.Eval("console.error('after the manager'); 0");

        const std::vector<std::string> lines = log.LeveledLines();
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "info before any resource"), 1u);
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "info [first] in a resource"), 1u);
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "warning [first] still in it"), 1u);
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "error after the manager"), 1u);
    });

    // ==================== ExecutionEnvironment ====================

    IT("gives a Lua state the ExecutionEnvironment of JavaScript", {
        EQUALS(LuaBuiltinsCoreTest::Members(Framework::Scripting::Builtins::ExecutionEnvironment::Definition()), "isClient,isServer,frameworkVersion,modVersion");

        const std::string version = Framework::Utils::Version::rel;
        {
            LuaBuiltinsCoreTest::LuaSide lua;
            LuaBuiltinsCoreTest::JsSide js;
            EQUALS(lua.Eval("ExecutionEnvironment.isClient"), "false");
            EQUALS(lua.Eval("ExecutionEnvironment.isServer"), "true");
            EQUALS(lua.Eval("ExecutionEnvironment.frameworkVersion"), version);
            EQUALS(lua.Eval("ExecutionEnvironment.modVersion"), LuaBuiltinsCoreTest::kModVersion);
            EQUALS(lua.Eval("type(ExecutionEnvironment.isClient) .. ' ' .. type(ExecutionEnvironment.isServer) .. ' ' .. type(ExecutionEnvironment.frameworkVersion) .. ' ' .. type(ExecutionEnvironment.modVersion)"), "boolean boolean string string");
            EQUALS(js.Eval("[ExecutionEnvironment.isClient, ExecutionEnvironment.isServer, ExecutionEnvironment.frameworkVersion, ExecutionEnvironment.modVersion].join(' ')"), "false true " + version + " " + LuaBuiltinsCoreTest::kModVersion);
            EQUALS(js.Eval("[typeof ExecutionEnvironment.isClient, typeof ExecutionEnvironment.isServer, typeof ExecutionEnvironment.frameworkVersion, typeof ExecutionEnvironment.modVersion].join(' ')"), "boolean boolean string string");
        }

        // A client runtime is told it is one, in either language.
        LuaBuiltinsCoreTest::LuaSide lua(Framework::Scripting::Bind::Side::Client);
        LuaBuiltinsCoreTest::JsSide js(/*isClient*/ true);
        EQUALS(lua.Eval("tostring(ExecutionEnvironment.isClient) .. ' ' .. tostring(ExecutionEnvironment.isServer)"), "true false");
        EQUALS(js.Eval("ExecutionEnvironment.isClient + ' ' + ExecutionEnvironment.isServer"), "true false");
    });

    IT("leaves the mod version empty for a mod that has none", {
        LuaBuiltinsCoreTest::LuaSide lua(Framework::Scripting::Bind::Side::Server, "");
        LuaBuiltinsCoreTest::JsSide js(/*isClient*/ false, "");
        EQUALS(lua.Eval("ExecutionEnvironment.modVersion == ''"), "true");
        EQUALS(js.Eval("ExecutionEnvironment.modVersion === ''"), "true");

        // What RegisterLua gives a state when it is told none.
        Framework::Scripting::LuaRuntime runtime(LuaRuntimeTest::Options());
        Framework::Scripting::Builtins::ExecutionEnvironment::RegisterLua(runtime);
        EQUALS(LuaRuntimeTest::Eval(runtime, "ExecutionEnvironment.modVersion == '' and ExecutionEnvironment.isServer"), "true");
    });

    IT("lets no script change ExecutionEnvironment", {
        LuaBuiltinsCoreTest::LuaSide lua;
        EQUALS(lua.Eval("(function() ExecutionEnvironment.modVersion = 'x' end)()"), "<error> eval:1: attempt to set 'modVersion' on ExecutionEnvironment");
        EQUALS(lua.Eval("(function() ExecutionEnvironment.isClient = true end)()"), "<error> eval:1: attempt to set 'isClient' on ExecutionEnvironment");
        EQUALS(lua.Eval("(function() ExecutionEnvironment.extra = 1 end)()"), "<error> eval:1: attempt to set 'extra' on ExecutionEnvironment");
        EQUALS(lua.Eval("ExecutionEnvironment.modVersion"), LuaBuiltinsCoreTest::kModVersion);
        EQUALS(lua.Eval("type(ExecutionEnvironment.IsClient)"), "nil");

        LuaBuiltinsCoreTest::JsSide js;
        EQUALS(js.Eval("!Reflect.set(ExecutionEnvironment, 'modVersion', 'x') && !Reflect.deleteProperty(ExecutionEnvironment, 'modVersion') && !Reflect.set(ExecutionEnvironment, 'isClient', true)"), "true");
        EQUALS(js.Eval("ExecutionEnvironment.modVersion"), LuaBuiltinsCoreTest::kModVersion);
    });

    // ==================== The server module ====================

    IT("has both in the Lua state of a resource the server module starts", {
        LuaRuntimeTest::LogCapture log(spdlog::level::debug);
        LuaResourcesTest::Server server;
        server.module.SetModVersion("4.5.6");
        server.files.Resource("lua-side", {{"main.lua", "console.log('started', ExecutionEnvironment.isServer, ExecutionEnvironment.isClient, ExecutionEnvironment.modVersion)\nconsole.warn('careful')"}});
        server.files.Resource("js-side", {{"main.js", "console.log('started', ExecutionEnvironment.isServer, ExecutionEnvironment.isClient, ExecutionEnvironment.modVersion);\nconsole.warn('careful');"}});
        EQUALS(server.Start(), true);

        // The same lines from both, each under the name of its resource.
        const std::vector<std::string> lines = log.LeveledLines();
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "info [lua-side] started true false 4.5.6"), 1u);
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "warning [lua-side] careful"), 1u);
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "info [js-side] started true false 4.5.6"), 1u);
        EQUALS(LuaRuntimeTest::CountStartingWith(lines, "warning [js-side] careful"), 1u);
        EQUALS(server.Eval("lua-side", "ExecutionEnvironment.frameworkVersion"), Framework::Utils::Version::rel);
    });
})
