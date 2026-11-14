/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_runtime_ut.h"
#include "lua_scheduler_ut.h"

#include "scripting/engine.h"
#include "scripting/lua/lua_engine.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/lua/lua_scheduler.h"
#include "scripting/script_engine.h"
#include "utils/package/package.h"
#include "utils/vfs.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace LuaEngineTest {

    static_assert(std::is_base_of_v<Framework::Scripting::ScriptEngine, Framework::Scripting::Engine>, "the JavaScript engine is a ScriptEngine");
    static_assert(std::is_base_of_v<Framework::Lifecycle, Framework::Scripting::ScriptEngine>, "a ScriptEngine has the lifecycle of a module");

    // The least a JavaScript engine has to be, to read what Engine itself answers.
    struct JavaScriptStub final: Framework::Scripting::Engine {
        Framework::Scripting::ScriptingError Init() override {
            return Framework::Scripting::ScriptingError::SCRIPTING_NONE;
        }
        void Shutdown() override {}
        bool ExecuteFile(std::string_view) override {
            _lastError = "stub";
            return false;
        }
        void Tick() override {}
        v8::Isolate *GetIsolate() const override {
            return nullptr;
        }
        v8::Local<v8::Context> GetContext() const override {
            return {};
        }
    };

    // Resource directories on disk, removed with the object.
    struct Files final {
        std::filesystem::path root = std::filesystem::temp_directory_path() / ("framework_lua_engine_ut_" + std::to_string(std::random_device {}()));

        Files() {
            std::filesystem::remove_all(root);
            std::filesystem::create_directories(root);
        }

        ~Files() {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }

        std::string Root(const std::string &resource) const {
            return (root / resource).string();
        }

        // Writes a file of a resource and gives its path.
        std::string Write(const std::string &resource, const std::string &file, const std::string &content) const {
            const std::filesystem::path path = root / resource / file;
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path, std::ios::binary) << content;
            return path.string();
        }
    };

    // What scripts told the host through record(text). It outlives the runtime that wrote it.
    inline std::vector<std::string> &Recorded() {
        static std::vector<std::string> recorded;
        return recorded;
    }

    inline std::string RecordedText() {
        std::string text;
        for (const std::string &entry : Recorded()) {
            text += (text.empty() ? "" : ",") + entry;
        }
        return text;
    }

    inline int Record(lua_State *L) {
        Recorded().emplace_back(luaL_checkstring(L, 1));
        return 0;
    }

    // shutdown(): shuts down the engine in its upvalue, from inside a script.
    inline int ShutdownEngine(lua_State *L) {
        static_cast<Framework::Scripting::LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)))->Shutdown();
        return 0;
    }

    inline void AddShutdown(Framework::Scripting::LuaRuntime &runtime, Framework::Scripting::LuaEngine &engine) {
        lua_State *L = runtime.State();
        lua_pushlightuserdata(L, &engine);
        lua_pushcclosure(L, ShutdownEngine, 1);
        lua_setglobal(L, "shutdown");
    }

    // An initialised engine over resource directories, on a clock the test moves. Every runtime gets record(text),
    // destroy(resource), which destroys a runtime from inside a script, tick(ms), which ticks the engine from
    // inside one, nextDue(), which is DueMs() from inside one, and shutdown().
    struct Scripted final {
        Files files;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::time_point() + std::chrono::hours(1);
        int setups                                  = 0;
        // What the disposing listener saw: "<resource>:<its global `value`>" for each runtime, in order.
        std::vector<std::string> disposed;
        // Last, so that it goes first: it tells the listener of the runtimes it still has.
        Framework::Scripting::LuaEngine engine;

        explicit Scripted(Framework::Scripting::LuaEngineOptions options = {}): engine(options) {
            Recorded().clear();
            engine.SetRuntimeSetupCallback([this](Framework::Scripting::LuaRuntime &runtime) {
                ++setups;
                lua_State *L = runtime.State();
                lua_pushcfunction(L, Record);
                lua_setglobal(L, "record");
                lua_pushlightuserdata(L, this);
                lua_pushcclosure(L, Destroy, 1);
                lua_setglobal(L, "destroy");
                lua_pushlightuserdata(L, this);
                lua_pushcclosure(L, TickInside, 1);
                lua_setglobal(L, "tick");
                lua_pushlightuserdata(L, this);
                lua_pushcclosure(L, NextDueInside, 1);
                lua_setglobal(L, "nextDue");
                AddShutdown(runtime, engine);
            });
            engine.AddRuntimeDisposingListener([this](Framework::Scripting::LuaRuntime &runtime) {
                lua_State *L = runtime.State();
                lua_pushglobaltable(L);
                lua_pushstring(L, "value");
                lua_rawget(L, -2);
                disposed.push_back(runtime.Options().resourceName + ":" + luaL_tolstring(L, -1, nullptr));
                lua_pop(L, 3);
            });
            (void)engine.Init();
        }

        static int Destroy(lua_State *L) {
            auto *self = static_cast<Scripted *>(lua_touserdata(L, lua_upvalueindex(1)));
            self->engine.DestroyResourceRuntime(luaL_checkstring(L, 1));
            return 0;
        }

        static int TickInside(lua_State *L) {
            auto *self = static_cast<Scripted *>(lua_touserdata(L, lua_upvalueindex(1)));
            self->At(static_cast<int>(luaL_checkinteger(L, 1)));
            return 0;
        }

        static int NextDueInside(lua_State *L) {
            auto *self = static_cast<Scripted *>(lua_touserdata(L, lua_upvalueindex(1)));
            lua_pushinteger(L, self->DueMs());
            return 1;
        }

        // Creates the runtime of `resource`, with its directory as the root.
        bool Create(const std::string &resource) {
            engine.SetResourceRoot(resource, files.Root(resource));
            return engine.CreateResourceRuntime(resource);
        }

        // Ticks at `ms` after the start.
        void At(int ms) {
            engine.Tick(start + std::chrono::milliseconds(ms));
        }

        // When the engine says it next has something to run, in milliseconds after the start. -1 when it has nothing.
        int DueMs() const {
            return DueMs(engine.NextDue(std::chrono::steady_clock::time_point::min()));
        }

        // The same, for what is due later than `ms` after the start.
        int DueMsAfter(int ms) const {
            return DueMs(engine.NextDue(start + std::chrono::milliseconds(ms)));
        }

        int DueMs(std::optional<std::chrono::steady_clock::time_point> due) const {
            return due ? static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(*due - start).count()) : -1;
        }

        // Writes `source` as a file of the resource and runs it. Empty when it ran, the error otherwise.
        std::string Run(const std::string &resource, const std::string &file, const std::string &source) {
            return engine.ExecuteResourceFile(resource, files.Write(resource, file, source)) ? std::string() : engine.GetLastError();
        }

        // The value of `expression` in the runtime of the resource, as tostring() spells it.
        std::string Eval(const std::string &resource, const std::string &expression) {
            Framework::Scripting::LuaRuntime *runtime = engine.GetRuntime(resource);
            return runtime != nullptr ? LuaRuntimeTest::Eval(*runtime, expression) : "<no runtime>";
        }

        std::string DisposedText() const {
            std::string text;
            for (const std::string &entry : disposed) {
                text += (text.empty() ? "" : ",") + entry;
            }
            return text;
        }

        // Has every runtime that is destroyed from now on say so through record, as "closing <resource>".
        void RecordClosing() {
            engine.AddRuntimeDisposingListener([](Framework::Scripting::LuaRuntime &runtime) {
                Recorded().push_back("closing " + runtime.Options().resourceName);
            });
        }
    };

    // Starts the global function `name` of the runtime, straight through its scheduler as an event or an export
    // will, with a `done` that notes what it was told in `told` and destroys the runtime of `resource`.
    inline void StartAndDestroy(Scripted &scripted, Framework::Scripting::LuaRuntime &runtime, const char *name, const std::string &resource, std::string &told) {
        lua_getglobal(runtime.State(), name);
        runtime.Scheduler().Start(0, [&scripted, &told, resource](bool ok, lua_State *thread, const std::string &error) {
            told = ok ? lua_tostring(thread, -1) : "failed: " + LuaRuntimeTest::FirstLine(error);
            scripted.engine.DestroyResourceRuntime(resource);
        });
    }

    // Mounts a resource of the given files in the VFS for as long as it lives.
    struct Mounted final {
        std::string name;
        bool mounted = false;

        Mounted(std::string resourceName, const std::vector<std::pair<std::string, std::string>> &files): name(std::move(resourceName)) {
            auto &vfs = Framework::Utils::Vfs::Get();
            vfs.Init(nullptr);
            Framework::Utils::Package::Writer writer;
            for (const auto &[path, content] : files) {
                writer.Add(path, content);
            }
            std::string blob, zip, error;
            mounted = writer.Build(nullptr, blob) && Framework::Utils::Package::Open(blob, nullptr, zip, error) && vfs.MountMemory(std::move(zip), name + ".zip", Root(), error);
        }

        ~Mounted() {
            Framework::Utils::Vfs::Get().Unmount(name + ".zip");
        }

        std::string Root() const {
            return Framework::Utils::Vfs::ResourcePath(name);
        }
    };

    // Whether the host lets a test create symbolic links: a Windows runner needs a privilege for it.
#ifdef _WIN32
    inline constexpr bool kCanLink = false;
#else
    inline constexpr bool kCanLink = true;
#endif
} // namespace LuaEngineTest

MODULE(lua_engine, {
    // --- The engines ---

    IT("is the Lua engine and handles the files that end in .lua", {
        Framework::Scripting::LuaEngine engine;
        Framework::Scripting::ScriptEngine &neutral = engine;
        EQUALS(neutral.GetLanguage() == Framework::Scripting::Language::Lua, true);
        EQUALS(neutral.HandlesFile("main.lua"), true);
        EQUALS(neutral.HandlesFile("/resources/a/sub.dir/main.lua"), true);
        EQUALS(neutral.HandlesFile("main.js"), false);
        EQUALS(neutral.HandlesFile("main.LUA"), false);
        EQUALS(neutral.HandlesFile("main.lua.js"), false);
        EQUALS(neutral.HandlesFile("main.luac"), false);
        EQUALS(neutral.HandlesFile("mainlua"), false);
        EQUALS(neutral.HandlesFile(""), false);
    });

    IT("leaves every other file to the JavaScript engine", {
        LuaEngineTest::JavaScriptStub engine;
        Framework::Scripting::ScriptEngine &neutral = engine;
        EQUALS(neutral.GetLanguage() == Framework::Scripting::Language::JavaScript, true);
        for (const char *path : {"main.js", "main.mjs", "main.cjs", "main.ts", "main", "main.LUA", "main.lua.js", ""}) {
            EQUALS(neutral.HandlesFile(path), true);
        }
        EQUALS(neutral.HandlesFile("main.lua"), false);
        EQUALS(neutral.HandlesFile("dir/sub/main.lua"), false);
    });

    IT("keeps the last error and the resource manager where both engines find them", {
        LuaEngineTest::JavaScriptStub engine;
        Framework::Scripting::ScriptEngine &neutral = engine;
        EQUALS(neutral.GetLastError(), "");
        EQUALS(neutral.ExecuteResourceFile("any", "main.js"), false);
        EQUALS(neutral.GetLastError(), "stub");
        EQUALS(engine.GetLastError(), "stub");
        EQUALS(neutral.CreateResourceRuntime("any"), true);
        EQUALS(neutral.GetResourceManager() == nullptr, true);
        auto *manager = reinterpret_cast<Framework::Scripting::ResourceManager *>(&engine);
        neutral.SetResourceManager(manager);
        EQUALS(engine.GetResourceManager() == manager, true);
    });

    IT("creates no runtime before it is initialised", {
        Framework::Scripting::LuaEngine engine;
        EQUALS(engine.IsInitialized(), false);
        EQUALS(engine.CreateResourceRuntime("a"), false);
        EQUALS(engine.GetLastError(), "Engine not initialized");
        EQUALS(engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE, true);
        EQUALS(engine.IsInitialized(), true);
        EQUALS(engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE, true);
        EQUALS(engine.CreateResourceRuntime("a"), true);
    });

    // --- Runtimes and files ---

    IT("runs a file in the runtime of its resource", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.engine.GetRuntime("a") == nullptr, true);
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.engine.GetRuntime("a") != nullptr, true);
        EQUALS(scripted.Run("a", "main.lua", "value = 40 + 2"), "");
        EQUALS(scripted.Eval("a", "value"), "42");
        EQUALS(lua_gettop(scripted.engine.GetRuntime("a")->State()), 0);
    });

    IT("creates a runtime with the options of the engine", {
        Framework::Scripting::LuaEngineOptions options;
        options.side             = Framework::Scripting::Bind::Side::Client;
        options.memoryLimitBytes = 64u << 20;
        options.entryDeadline    = std::chrono::milliseconds(250);
        LuaEngineTest::Scripted scripted(options);
        EQUALS(scripted.Create("a"), true);
        const Framework::Scripting::LuaRuntimeOptions &created = scripted.engine.GetRuntime("a")->Options();
        EQUALS(created.resourceName, "a");
        EQUALS(created.side == Framework::Scripting::Bind::Side::Client, true);
        UEQUALS(created.memoryLimitBytes, static_cast<size_t>(64u << 20));
        EQUALS(created.entryDeadline.count(), 250);
        EQUALS(created.rootPath, scripted.files.Root("a"));

        UEQUALS(Framework::Scripting::LuaEngineOptions().memoryLimitBytes, static_cast<size_t>(512u << 20));
        EQUALS(Framework::Scripting::LuaEngineOptions().entryDeadline.count(), 0);
        EQUALS(Framework::Scripting::LuaEngineOptions().side == Framework::Scripting::Bind::Side::Server, true);
    });

    IT("gives each resource globals of its own", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        EQUALS(scripted.Run("a", "main.lua", "value = 'from a' only_a = true"), "");
        EQUALS(scripted.Run("b", "main.lua", "value = 'from b'"), "");
        EQUALS(scripted.Eval("a", "value"), "from a");
        EQUALS(scripted.Eval("b", "value"), "from b");
        EQUALS(scripted.Eval("b", "only_a"), "nil");
        EQUALS(scripted.engine.GetRuntime("a") != scripted.engine.GetRuntime("b"), true);
    });

    IT("runs the files of a resource in one state", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Run("a", "first.lua", "shared = 1"), "");
        EQUALS(scripted.Run("a", "second.lua", "shared = shared + 1"), "");
        EQUALS(scripted.Eval("a", "shared"), "2");
    });

    IT("refuses a second runtime for a resource", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        Framework::Scripting::LuaRuntime *first = scripted.engine.GetRuntime("a");
        EQUALS(scripted.Create("a"), false);
        EQUALS(scripted.engine.GetLastError(), "resource 'a' already has a Lua runtime");
        EQUALS(scripted.engine.GetRuntime("a") == first, true);
        EQUALS(scripted.setups, 1);
    });

    IT("fails a file that raises, naming the file and the line", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        const std::string error = scripted.Run("a", "main.lua", "local x = 1\nerror('boom')\nvalue = 1");
        EQUALS(LuaRuntimeTest::FirstLine(error), "a/main.lua:2: boom");
        EQUALS(LuaRuntimeTest::Contains(error, "stack traceback:"), true);
        EQUALS(scripted.Eval("a", "value"), "nil");
        // The state goes on.
        EQUALS(scripted.Run("a", "next.lua", "value = 2"), "");
        EQUALS(scripted.Eval("a", "value"), "2");
        EQUALS(scripted.engine.DrainPendingErrors().size(), 0u);
    });

    IT("fails a file that does not compile, naming the file and the line", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(LuaRuntimeTest::FirstLine(scripted.Run("a", "main.lua", "value = 1\nvalue = = 2")), "a/main.lua:2: unexpected symbol near '='");
        EQUALS(scripted.Eval("a", "value"), "nil");
    });

    IT("names a chunk by its path below the resource root", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(LuaRuntimeTest::FirstLine(scripted.Run("a", "sub/dir/file.lua", "error('deep')")), "a/sub/dir/file.lua:1: deep");
        // Reached another way round, it is the same file.
        scripted.files.Write("a", "sub/again.lua", "error('again')");
        EQUALS(scripted.engine.ExecuteResourceFile("a", scripted.files.Root("a") + "/sub/dir/../again.lua"), false);
        EQUALS(LuaRuntimeTest::FirstLine(scripted.engine.GetLastError()), "a/sub/again.lua:1: again");
        // A file from elsewhere keeps its name alone.
        EQUALS(scripted.engine.ExecuteResourceFile("a", scripted.files.Write("b", "other/outside.lua", "error('outside')")), false);
        EQUALS(LuaRuntimeTest::FirstLine(scripted.engine.GetLastError()), "a/outside.lua:1: outside");
    });

    IT("reads the root of a resource when it needs it, not when the runtime is created", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.engine.CreateResourceRuntime("a"), true);
        scripted.files.Write("a", "lib/util.lua", "return 'util'");
        const std::string path = scripted.files.Write("a", "sub/main.lua", "value = require('lib.util')\nerror('named')");
        // With no root there is no module, and a file has its name alone.
        EQUALS(scripted.Eval("a", "require('lib.util')"), "<error> eval:1: module 'lib.util' not found in resource 'a'");
        EQUALS(scripted.engine.ExecuteResourceFile("a", path), false);
        EQUALS(LuaRuntimeTest::FirstLine(scripted.engine.GetLastError()), "a/main.lua:1: module 'lib.util' not found in resource 'a'");

        scripted.engine.SetResourceRoot("a", scripted.files.Root("a"));
        EQUALS(scripted.engine.ExecuteResourceFile("a", path), false);
        EQUALS(LuaRuntimeTest::FirstLine(scripted.engine.GetLastError()), "a/sub/main.lua:2: named");
        EQUALS(scripted.Eval("a", "value"), "util");

        // And follows it when it changes.
        scripted.files.Write("moved", "lib/other.lua", "return 'other'");
        scripted.engine.SetResourceRoot("a", scripted.files.Root("moved"));
        EQUALS(scripted.Eval("a", "require('lib.other')"), "other");
    });

    IT("skips a byte order mark at the start of a file and of a module", {
        const std::string mark = "\xEF\xBB\xBF";
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.files.Write("a", "lib/marked.lua", mark + "return 'marked'");
        EQUALS(LuaRuntimeTest::FirstLine(scripted.Run("a", "main.lua", mark + "value = require('lib.marked')\nerror('second line')")), "a/main.lua:2: second line");
        EQUALS(scripted.Eval("a", "value"), "marked");
        EQUALS(scripted.Run("a", "only.lua", mark), "");
        // Anywhere else it is not Lua.
        EQUALS(LuaRuntimeTest::Contains(scripted.Run("a", "late.lua", "value = 1\n" + mark + "value = 2"), "a/late.lua:2: unexpected symbol"), true);
        EQUALS(scripted.Eval("a", "value"), "marked");
    });

    IT("fails a file of a resource that has no runtime, and one it cannot read", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Run("ghost", "main.lua", "value = 1"), "no Lua runtime for resource 'ghost'");
        EQUALS(scripted.Create("a"), true);
        const std::string missing = scripted.files.Root("a") + "/missing.lua";
        EQUALS(scripted.engine.ExecuteResourceFile("a", missing), false);
        EQUALS(scripted.engine.GetLastError(), "cannot read '" + missing + "'");
        EQUALS(scripted.engine.ExecuteResourceFile("a", scripted.files.Root("a")), false);
        EQUALS(scripted.engine.GetLastError(), "cannot read '" + scripted.files.Root("a") + "'");
    });

    IT("refuses bytecode for a file", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(LuaRuntimeTest::Contains(scripted.Run("a", "main.lua", LuaRuntimeTest::Bytecode("value = 1")), "attempt to load a binary chunk"), true);
        EQUALS(scripted.Eval("a", "value"), "nil");
    });

    IT("reads a file with a virtual path through the VFS", {
        const std::vector<std::pair<std::string, std::string>> files = {
            {"main.lua", "value = require('lib.util').name .. ' ' .. require('lib.pack')"},
            {"lib/util.lua", "return { name = ... }"},
            {"lib/pack/init.lua", "return 'packed'"},
            {"sub/bad.lua", "\nerror('virtual')"},
        };
        const LuaEngineTest::Mounted mounted("lua_engine_ut", files);
        EQUALS(mounted.mounted, true);
        LuaEngineTest::Scripted scripted;
        scripted.engine.SetResourceRoot("lua_engine_ut", mounted.Root());
        EQUALS(scripted.engine.CreateResourceRuntime("lua_engine_ut"), true);
        EQUALS(scripted.engine.ExecuteResourceFile("lua_engine_ut", mounted.Root() + "/main.lua"), true);
        EQUALS(scripted.Eval("lua_engine_ut", "value"), "lib.util packed");
        EQUALS(scripted.engine.ExecuteResourceFile("lua_engine_ut", mounted.Root() + "/sub/bad.lua"), false);
        EQUALS(LuaRuntimeTest::FirstLine(scripted.engine.GetLastError()), "lua_engine_ut/sub/bad.lua:2: virtual");
        EQUALS(scripted.engine.ExecuteResourceFile("lua_engine_ut", mounted.Root() + "/nope.lua"), false);
        EQUALS(scripted.engine.GetLastError(), "cannot read '" + mounted.Root() + "/nope.lua'");
        EQUALS(scripted.Eval("lua_engine_ut", "select(2, pcall(require, 'lib.nope'))"), "module 'lib.nope' not found in resource 'lua_engine_ut'");
    });

    // --- require ---

    IT("loads a module of the resource once and gives what it returned", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.files.Write("a", "lib/util.lua", "loads = (loads or 0) + 1\nreturn { name = ..., arguments = select('#', ...) }");
        EQUALS(scripted.Run("a", "main.lua", "local first = require('lib.util')\nlocal second = require('lib.util')\nsame = first == second\nname = first.name\narguments = first.arguments"), "");
        EQUALS(scripted.Eval("a", "loads"), "1");
        EQUALS(scripted.Eval("a", "same"), "true");
        EQUALS(scripted.Eval("a", "name"), "lib.util");
        EQUALS(scripted.Eval("a", "arguments"), "1");
        // From another file of the resource as well.
        EQUALS(scripted.Run("a", "other.lua", "again = require('lib.util').name"), "");
        EQUALS(scripted.Eval("a", "loads"), "1");
        EQUALS(scripted.Eval("a", "select('#', require('lib.util'))"), "1");
    });

    IT("finds a module in a file, or else in the init.lua of a directory", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.files.Write("a", "top.lua", "return 'top'");
        scripted.files.Write("a", "pack/init.lua", "return 'pack init'");
        scripted.files.Write("a", "both.lua", "return 'both file'");
        scripted.files.Write("a", "both/init.lua", "return 'both init'");
        scripted.files.Write("a", "my-lib/part_2.lua", "return 'dashed'");
        EQUALS(scripted.Eval("a", "require('top')"), "top");
        EQUALS(scripted.Eval("a", "require('pack')"), "pack init");
        EQUALS(scripted.Eval("a", "require('both')"), "both file");
        EQUALS(scripted.Eval("a", "require('my-lib.part_2')"), "dashed");
    });

    IT("gives true for a module that returns nothing, and keeps false", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.files.Write("a", "nothing.lua", "runs = (runs or 0) + 1");
        scripted.files.Write("a", "no.lua", "runs = (runs or 0) + 1 return false");
        EQUALS(scripted.Eval("a", "require('nothing')"), "true");
        EQUALS(scripted.Eval("a", "require('nothing')"), "true");
        EQUALS(scripted.Eval("a", "require('no')"), "false");
        EQUALS(scripted.Eval("a", "require('no')"), "false");
        EQUALS(scripted.Eval("a", "runs"), "2");
    });

    IT("keeps the modules of each resource apart", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.files.Write("a", "lib/util.lua", "return 'util of a'");
        scripted.files.Write("b", "lib/util.lua", "return 'util of b'");
        scripted.files.Write("a", "only.lua", "return 'only in a'");
        EQUALS(scripted.Eval("a", "require('lib.util')"), "util of a");
        EQUALS(scripted.Eval("b", "require('lib.util')"), "util of b");
        EQUALS(scripted.Eval("b", "require('only')"), "<error> eval:1: module 'only' not found in resource 'b'");
    });

    IT("refuses a module name that could leave the resource", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.files.Write("a", "x.lua", "return 'x'");
        scripted.files.Write("a", "sub/y.lua", "return 'y'");
        scripted.files.Write("b", "x.lua", "return 'x of b'");
        const std::vector<std::string> refused = {"../x", "../b/x", "sub/y", "sub\\y", "/x", scripted.files.Root("a") + "/x", "C:x", "..b.x", "sub..y", ".x", "x.", ".", "..", "", "x.lua/", "x y", std::string("x\0y", 3)};
        for (const std::string &name : refused) {
            LuaRuntimeTest::SetGlobalString(*scripted.engine.GetRuntime("a"), "name", name);
            EQUALS(scripted.Eval("a", "(select(2, pcall(require, name)):gsub('^(require: invalid module name).*', '%1'))"), "require: invalid module name");
        }
        EQUALS(scripted.Eval("a", "require('../x')"), "<error> eval:1: require: invalid module name '../x'");
        // A name has its length, so that a script cannot have the host build paths without end.
        LuaRuntimeTest::SetGlobalString(*scripted.engine.GetRuntime("a"), "name", std::string(257, 'a'));
        EQUALS(scripted.Eval("a", "select(2, pcall(require, name))"), "require: module name is longer than 256 bytes");
        LuaRuntimeTest::SetGlobalString(*scripted.engine.GetRuntime("a"), "name", std::string(256, 'a'));
        EQUALS(scripted.Eval("a", "(select(2, pcall(require, name)):gsub('a+', 'a'))"), "module 'a' not found in resource 'a'");
        EQUALS(scripted.Eval("a", "require(42)"), "<error> eval:1: require: module name must be a string");
        EQUALS(scripted.Eval("a", "require()"), "<error> eval:1: require: module name must be a string");
        EQUALS(scripted.Eval("a", "require('sub.y')"), "y");
    });

    IT("does not follow a link out of the resource", {
        if (!LuaEngineTest::kCanLink) {
            SKIP();
        }
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        const std::string outside = scripted.files.Write("b", "secret.lua", "return 'secret'");
        scripted.files.Write("a", "inside.lua", "return 'inside'");
        std::filesystem::create_symlink(outside, std::filesystem::path(scripted.files.Root("a")) / "out.lua");
        std::filesystem::create_symlink(std::filesystem::path(scripted.files.Root("a")) / "inside.lua", std::filesystem::path(scripted.files.Root("a")) / "alias.lua");
        EQUALS(scripted.Eval("a", "require('out')"), "<error> eval:1: module 'out' not found in resource 'a'");
        EQUALS(scripted.Eval("a", "require('alias')"), "inside");
    });

    IT("says which module was not found, and in which resource", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(LuaRuntimeTest::FirstLine(scripted.Run("a", "main.lua", "\nrequire('lib.nope')")), "a/main.lua:2: module 'lib.nope' not found in resource 'a'");
        // A directory is not a module.
        scripted.files.Write("a", "dir.lua/keep.txt", "");
        EQUALS(scripted.Eval("a", "require('dir')"), "<error> eval:1: module 'dir' not found in resource 'a'");
        // It is looked for again once it is there.
        scripted.files.Write("a", "lib/nope.lua", "return 'now there'");
        EQUALS(scripted.Eval("a", "require('lib.nope')"), "now there");
    });

    IT("raises the error of a module that fails, and runs it again the next time", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.files.Write("a", "lib/flaky.lua", "attempts = (attempts or 0) + 1\nif attempts == 1 then error('not yet') end\nreturn 'loaded'");
        const std::string error = scripted.Run("a", "main.lua", "flaky = require('lib.flaky')");
        EQUALS(LuaRuntimeTest::FirstLine(error), "a/lib/flaky.lua:2: not yet");
        // Where it was required from is in the traceback.
        EQUALS(LuaRuntimeTest::Contains(error, "a/main.lua:1: in main chunk"), true);
        EQUALS(scripted.Eval("a", "require('lib.flaky')"), "loaded");
        EQUALS(scripted.Eval("a", "attempts"), "2");

        scripted.files.Write("a", "lib/broken.lua", "return 1 +");
        EQUALS(LuaRuntimeTest::Contains(scripted.Eval("a", "require('lib.broken')"), "a/lib/broken.lua:1: unexpected symbol near <eof>"), true);
        scripted.files.Write("a", "lib/broken.lua", "return 'mended'");
        EQUALS(scripted.Eval("a", "require('lib.broken')"), "mended");

        scripted.files.Write("a", "lib/binary.lua", LuaRuntimeTest::Bytecode("return 1"));
        EQUALS(LuaRuntimeTest::Contains(scripted.Eval("a", "require('lib.binary')"), "attempt to load a binary chunk"), true);
    });

    IT("refuses a module that requires itself through another", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.files.Write("a", "one.lua", "return require('two')");
        scripted.files.Write("a", "two.lua", "return require('one')");
        scripted.files.Write("a", "self.lua", "return require('self')");
        EQUALS(scripted.Eval("a", "require('one')"), "<error> a/two.lua:1: require: cyclic dependency on 'one'");
        EQUALS(scripted.Eval("a", "require('self')"), "<error> a/self.lua:1: require: cyclic dependency on 'self'");
        // Neither is left half loaded.
        scripted.files.Write("a", "two.lua", "return 'two'");
        EQUALS(scripted.Eval("a", "require('one')"), "two");
        EQUALS(scripted.Eval("a", "require('two')"), "two");
        // Caught, the cycle leaves the module that was loading to finish.
        scripted.files.Write("a", "outer.lua", "local ok, message = pcall(require, 'outer')\nreturn tostring(ok) .. ' ' .. message");
        EQUALS(scripted.Eval("a", "require('outer')"), "false require: cyclic dependency on 'outer'");
        EQUALS(scripted.Eval("a", "require('outer')"), "false require: cyclic dependency on 'outer'");
    });

    IT("does not let a module wait while it loads", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.At(0);
        scripted.files.Write("a", "waits.lua", "sleep(10)\nreturn 'never'");
        scripted.files.Write("a", "later.lua", "return { nap = function() sleep(10) record('napped') end }");
        EQUALS(LuaRuntimeTest::FirstLine(scripted.Run("a", "main.lua", "require('waits')")), "a/waits.lua:1: sleep: cannot wait here");
        // What it defines can wait when it is called.
        EQUALS(scripted.Run("a", "second.lua", "require('later').nap() record('done')"), "");
        EQUALS(LuaEngineTest::RecordedText(), "");
        scripted.At(10);
        EQUALS(LuaEngineTest::RecordedText(), "napped,done");
        EQUALS(scripted.engine.DrainPendingErrors().size(), 0u);
    });

    IT("has no modules without a loader", {
        Framework::Scripting::LuaRuntime runtime(LuaRuntimeTest::Options());
        EQUALS(LuaRuntimeTest::Eval(runtime, "require('anything')"), "<error> eval:1: require: no module loader");
        runtime.SetModuleLoader([](const std::string &relativePath, std::string &source) {
            source = "return '" + relativePath + "'";
            return relativePath == "a/b-c/init.lua";
        });
        EQUALS(LuaRuntimeTest::Eval(runtime, "require('a.b-c')"), "a/b-c/init.lua");
        EQUALS(LuaRuntimeTest::Eval(runtime, "require('missing')"), "<error> eval:1: module 'missing' not found in resource 'test'");
        EQUALS(lua_gettop(runtime.State()), 0);
    });

    // --- Time ---

    IT("fires the timers of every runtime from Tick", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "setTimeout(function() record('a timeout') end, 50)\nsetInterval(function() record('a interval') end, 40)"), "");
        EQUALS(scripted.Run("b", "main.lua", "record('b before') sleep(60) record('b after')"), "");
        scripted.At(39);
        EQUALS(LuaEngineTest::RecordedText(), "b before");
        scripted.At(40);
        EQUALS(LuaEngineTest::RecordedText(), "b before,a interval");
        scripted.At(60);
        EQUALS(LuaEngineTest::RecordedText(), "b before,a interval,a timeout,b after");
        scripted.At(80);
        EQUALS(LuaEngineTest::RecordedText(), "b before,a interval,a timeout,b after,a interval");
        EQUALS(scripted.engine.DrainPendingErrors().size(), 0u);
    });

    IT("ticks on the steady clock when it is given no time", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.engine.Tick();
        EQUALS(scripted.Run("a", "main.lua", "setTimeout(function() record('fired') end, 0) setTimeout(function() record('far') end, 3600000)"), "");
        EQUALS(LuaEngineTest::RecordedText(), "");
        scripted.engine.Tick();
        EQUALS(LuaEngineTest::RecordedText(), "fired");
    });

    IT("tells when the first timer or sleep of any runtime is due", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.DueMs(), -1);
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.DueMs(), -1);
        EQUALS(scripted.Run("a", "main.lua", "timeout = setTimeout(function() end, 50)"), "");
        EQUALS(scripted.DueMs(), 50);
        EQUALS(scripted.Run("b", "main.lua", "interval = setInterval(function() end, 20)\nsleep(5)"), "");
        EQUALS(scripted.DueMs(), 5);
        scripted.At(5);
        EQUALS(scripted.DueMs(), 20);
        // An interval is due again a period after it was.
        scripted.At(25);
        EQUALS(scripted.DueMs(), 40);
        EQUALS(scripted.Run("b", "next.lua", "clearInterval(interval)"), "");
        EQUALS(scripted.DueMs(), 50);
        scripted.At(50);
        EQUALS(scripted.DueMs(), -1);
    });

    IT("tells what is due after a given time, past what is due by then", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "setTimeout(function() end, 10)\nsetTimeout(function() end, 30)"), "");
        EQUALS(scripted.Run("b", "main.lua", "setTimeout(function() end, 20)"), "");
        EQUALS(scripted.DueMsAfter(0), 10);
        EQUALS(scripted.DueMsAfter(9), 10);
        // Strictly after.
        EQUALS(scripted.DueMsAfter(10), 20);
        EQUALS(scripted.DueMsAfter(20), 30);
        EQUALS(scripted.DueMsAfter(30), -1);

        // What a tick left due at its own time, as a wait for no time is, does not hide a later timer.
        scripted.At(15);
        EQUALS(scripted.Run("b", "next.lua", "setTimeout(function() end, 0)\nspawn(function() sleep(0) end)"), "");
        EQUALS(scripted.DueMs(), 15);
        EQUALS(scripted.DueMsAfter(15), 20);
    });

    IT("fires only what came due after a given time in a tick between two ticks", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "spawn(function() while true do record('a turn') sleep(0) end end)"), "");
        EQUALS(scripted.Run("b", "main.lua", "setTimeout(function() record('b timer') end, 10)"), "");
        EQUALS(LuaEngineTest::RecordedText(), "a turn");

        scripted.engine.Tick(scripted.start + std::chrono::milliseconds(5), scripted.start);
        EQUALS(LuaEngineTest::RecordedText(), "a turn");
        scripted.engine.Tick(scripted.start + std::chrono::milliseconds(10), scripted.start + std::chrono::milliseconds(5));
        EQUALS(LuaEngineTest::RecordedText(), "a turn,b timer");
        EQUALS(scripted.DueMsAfter(10), -1);

        // The loop goes round once for each tick that is not between two.
        scripted.At(16);
        EQUALS(LuaEngineTest::RecordedText(), "a turn,b timer,a turn");
        scripted.engine.Tick(scripted.start + std::chrono::milliseconds(20), scripted.start + std::chrono::milliseconds(16));
        scripted.At(32);
        EQUALS(LuaEngineTest::RecordedText(), "a turn,b timer,a turn,a turn");
    });

    IT("leaves the timers of a runtime that is executing or going out of what is due next", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "setTimeout(function() end, 10)\nrecord('from a: ' .. nextDue())"), "");
        EQUALS(scripted.Run("b", "main.lua", "setTimeout(function() end, 30)\nrecord('from b: ' .. nextDue())"), "");
        EQUALS(LuaEngineTest::RecordedText(), "from a: -1,from b: 10");
        EQUALS(scripted.DueMs(), 10);

        scripted.engine.DestroyResourceRuntime("a");
        EQUALS(scripted.DueMs(), 30);
        // Destroyed from inside its own file, it is still there, and fires nothing any more.
        EQUALS(scripted.Run("b", "next.lua", "destroy('b')"), "");
        EQUALS(scripted.DueMs(), -1);
    });

    IT("queues an error raised from a timer under its resource", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "setTimeout(function()\nerror('late in a')\nend, 10)"), "");
        EQUALS(scripted.Run("b", "main.lua", "sleep(20)\nerror('late in b')"), "");
        EQUALS(scripted.engine.DrainPendingErrors().size(), 0u);
        scripted.At(30);
        const auto errors = scripted.engine.DrainPendingErrors();
        EQUALS(errors.size(), 2u);
        EQUALS(errors[0].resourceName, "a");
        EQUALS(LuaRuntimeTest::FirstLine(errors[0].errorMessage), "a/main.lua:2: late in a");
        EQUALS(LuaRuntimeTest::Contains(errors[0].errorMessage, "stack traceback:"), true);
        EQUALS(errors[1].resourceName, "b");
        EQUALS(LuaRuntimeTest::FirstLine(errors[1].errorMessage), "b/main.lua:2: late in b");
        // Drained, they are gone.
        EQUALS(scripted.engine.DrainPendingErrors().size(), 0u);
        // An error of the file itself is the caller's, not queued.
        EQUALS(LuaRuntimeTest::FirstLine(scripted.Run("a", "next.lua", "error('now')")), "a/next.lua:1: now");
        EQUALS(scripted.engine.DrainPendingErrors().size(), 0u);
    });

    IT("leaves a runtime that is executing out of a tick from inside it", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.Run("b", "main.lua", "setTimeout(function() record('b timer') end, 10)"), "");
        EQUALS(scripted.Run("a", "main.lua", "setTimeout(function() record('a timer') end, 10)\ntick(20)\nrecord('a after tick')"), "");
        EQUALS(LuaEngineTest::RecordedText(), "b timer,a after tick");
        scripted.At(20);
        EQUALS(LuaEngineTest::RecordedText(), "b timer,a after tick,a timer");
    });

    // --- Destruction ---

    IT("destroys a runtime with its timers and what waited in it", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua",
                   "value = 'kept to the end'\n"
                   "setTimeout(function() record('a timeout') end, 10)\n"
                   "setInterval(function() record('a interval') end, 10)\n"
                   "spawn(function() sleep(10) record('a spawned') end)\n"
                   "record('a before') sleep(10) record('a after')"),
            "");
        EQUALS(scripted.Run("b", "main.lua", "setTimeout(function() record('b timeout') end, 10)"), "");
        scripted.engine.DestroyResourceRuntime("a");
        EQUALS(scripted.engine.GetRuntime("a") == nullptr, true);
        // The listener was told while the state could still be read.
        EQUALS(scripted.DisposedText(), "a:kept to the end");
        scripted.At(10);
        scripted.At(100);
        EQUALS(LuaEngineTest::RecordedText(), "a before,b timeout");
        EQUALS(scripted.Run("a", "next.lua", "value = 1"), "no Lua runtime for resource 'a'");
        // Destroying what is not there does nothing.
        scripted.engine.DestroyResourceRuntime("a");
        scripted.engine.DestroyResourceRuntime("ghost");
        EQUALS(scripted.DisposedText(), "a:kept to the end");
        EQUALS(scripted.engine.GetRuntime("b") != nullptr, true);
    });

    IT("puts off destroying a runtime from inside its own file", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua",
                   "value = 'alive'\n"
                   "setTimeout(function() record('armed before') end, 10)\n"
                   "destroy('a')\n"
                   "record('still running')\n"
                   "value = 'went on'\n"
                   "setTimeout(function() record('armed after') end, 10)\n"
                   "spawn(function() record('spawned') sleep(10) record('woke') end)"),
            "");
        EQUALS(LuaEngineTest::RecordedText(), "still running,spawned");
        // Gone for everyone at once, destroyed at the next tick.
        EQUALS(scripted.engine.GetRuntime("a") == nullptr, true);
        EQUALS(scripted.Run("a", "next.lua", "value = 1"), "no Lua runtime for resource 'a'");
        EQUALS(scripted.DisposedText(), "");
        scripted.At(10);
        EQUALS(scripted.DisposedText(), "a:went on");
        scripted.At(100);
        EQUALS(LuaEngineTest::RecordedText(), "still running,spawned");
        EQUALS(scripted.DisposedText(), "a:went on");
    });

    IT("puts off destroying a runtime from inside one of its timers", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua",
                   "setTimeout(function() record('first') destroy('a') record('first went on') end, 10)\n"
                   "setTimeout(function() record('due in the same tick') end, 10)\n"
                   "setInterval(function() record('interval') end, 10)\n"
                   "spawn(function() sleep(10) record('woke') end)"),
            "");
        EQUALS(scripted.Run("b", "main.lua", "setTimeout(function() record('b timer') end, 10)"), "");
        scripted.At(10);
        EQUALS(LuaEngineTest::RecordedText(), "first,first went on,b timer");
        EQUALS(scripted.engine.GetRuntime("a") == nullptr, true);
        EQUALS(scripted.DisposedText(), "");
        scripted.At(20);
        EQUALS(scripted.DisposedText(), "a:nil");
        scripted.At(100);
        EQUALS(LuaEngineTest::RecordedText(), "first,first went on,b timer");
    });

    IT("puts off destroying a runtime from what its scheduler tells the host in a tick", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "value = 'told' function work() sleep(10) return 'worked' end"), "");
        Framework::Scripting::LuaRuntime *runtime = scripted.engine.GetRuntime("a");
        lua_getglobal(runtime->State(), "work");
        std::string told;
        runtime->Scheduler().Start(0, [&scripted, &told](bool ok, lua_State *thread, const std::string &) {
            told = ok ? lua_tostring(thread, -1) : "failed";
            scripted.engine.DestroyResourceRuntime("a");
        });
        scripted.At(10);
        EQUALS(told, "worked");
        // No script was running in it any more, but its scheduler was still at work.
        EQUALS(scripted.engine.GetRuntime("a") == nullptr, true);
        EQUALS(scripted.DisposedText(), "");
        scripted.At(20);
        EQUALS(scripted.DisposedText(), "a:told");
    });

    IT("puts off destroying a runtime from the done of a function the host started in it", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "value = 'started' function work() return 'worked' end"), "");
        Framework::Scripting::LuaRuntime *runtime = scripted.engine.GetRuntime("a");
        std::string told;
        LuaEngineTest::StartAndDestroy(scripted, *runtime, "work", "a", told);
        EQUALS(told, "worked");
        // The scheduler had the coroutine still to let go of when it told.
        EQUALS(scripted.engine.GetRuntime("a") == nullptr, true);
        EQUALS(scripted.DisposedText(), "");
        EQUALS(runtime->IsExecuting(), false);
        scripted.At(10);
        EQUALS(scripted.DisposedText(), "a:started");

        // The same when it tells of a failure.
        EQUALS(scripted.Create("a"), true);
        runtime = scripted.engine.GetRuntime("a");
        EQUALS(LuaRuntimeTest::Run(*runtime, "value = 'again' function fail() error('broke') end"), "");
        LuaEngineTest::StartAndDestroy(scripted, *runtime, "fail", "a", told);
        EQUALS(told, "failed: test:1: broke");
        EQUALS(scripted.DisposedText(), "a:started");
        scripted.At(20);
        EQUALS(scripted.DisposedText(), "a:started,a:again");
    });

    IT("puts off destroying a runtime from the done of a coroutine the host resumed", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        Framework::Scripting::LuaRuntime *runtime = scripted.engine.GetRuntime("a");
        LuaRuntimeTest::SetGlobalFunction(*runtime, "wait", LuaSchedulerTest::Wait);
        EQUALS(scripted.Run("a", "main.lua", "value = 'waited' function work() return wait() end"), "");
        std::string told;
        LuaEngineTest::StartAndDestroy(scripted, *runtime, "work", "a", told);
        EQUALS(told, "");
        runtime->Scheduler().Resume(LuaSchedulerTest::g_token, [](lua_State *thread) {
            lua_pushstring(thread, "resumed");
            return 1;
        });
        EQUALS(told, "resumed");
        EQUALS(scripted.engine.GetRuntime("a") == nullptr, true);
        EQUALS(scripted.DisposedText(), "");
        scripted.At(10);
        EQUALS(scripted.DisposedText(), "a:waited");

        // And when the done of one runtime, still at work, is what a script of another stops it from.
        runtime = scripted.engine.GetRuntime("b");
        LuaRuntimeTest::SetGlobalFunction(*runtime, "wait", LuaSchedulerTest::Wait);
        EQUALS(scripted.Run("b", "main.lua", "value = 'stopped by a' function work() return wait() end"), "");
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Run("a", "main.lua", "function stop() destroy('b') return 'stopped' end"), "");
        lua_getglobal(runtime->State(), "work");
        runtime->Scheduler().Start(0, [&scripted, &told](bool, lua_State *, const std::string &) {
            LuaEngineTest::StartAndDestroy(scripted, *scripted.engine.GetRuntime("a"), "stop", "none", told);
        });
        runtime->Scheduler().Resume(LuaSchedulerTest::g_token, {});
        EQUALS(told, "stopped");
        EQUALS(scripted.engine.GetRuntime("b") == nullptr, true);
        EQUALS(scripted.DisposedText(), "a:waited");
        scripted.At(20);
        EQUALS(scripted.DisposedText(), "a:waited,b:stopped by a");
    });

    IT("destroys another runtime from inside a script at once", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.Run("b", "main.lua", "value = 'b' setTimeout(function() record('b timer') end, 10)"), "");
        EQUALS(scripted.Run("a", "main.lua", "destroy('b') record('a went on')"), "");
        EQUALS(scripted.DisposedText(), "b:b");
        EQUALS(scripted.engine.GetRuntime("b") == nullptr, true);
        EQUALS(scripted.engine.GetRuntime("a") != nullptr, true);
        scripted.At(10);
        EQUALS(LuaEngineTest::RecordedText(), "a went on");
    });

    IT("does not resume from the host what waited in a runtime that is going", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        Framework::Scripting::LuaRuntime *runtime = scripted.engine.GetRuntime("a");
        LuaRuntimeTest::SetGlobalFunction(*runtime, "wait", LuaSchedulerTest::Wait);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "spawn(function() wait() record('resumed') end)\ndestroy('a')\nwait()\nrecord('resumed too')"), "");
        // Still there until the next tick, and its coroutines stay where they are.
        runtime->Scheduler().Resume(LuaSchedulerTest::g_token, {});
        EQUALS(LuaEngineTest::RecordedText(), "");
        EQUALS(runtime->Scheduler().SuspendedCoroutines(), 2u);
        scripted.At(10);
        EQUALS(scripted.DisposedText(), "a:nil");
    });

    IT("frees the name of a runtime that is being destroyed for the next one", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "value = 'old' destroy('a') setTimeout(function() record('old timer') end, 10)"), "");
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.setups, 2);
        EQUALS(scripted.Eval("a", "value"), "nil");
        EQUALS(scripted.DisposedText(), "");
        scripted.At(0);
        EQUALS(scripted.DisposedText(), "a:old");
        EQUALS(scripted.Run("a", "main.lua", "value = 'new' setTimeout(function() record('new timer') end, 10)"), "");
        scripted.At(10);
        EQUALS(scripted.DisposedText(), "a:old");
        EQUALS(LuaEngineTest::RecordedText(), "new timer");
        EQUALS(scripted.Eval("a", "value"), "new");
    });

    IT("keeps a runtime that is still executing through a tick from inside it", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "value = 'busy' destroy('a') tick(10) record('after the tick')"), "");
        EQUALS(LuaEngineTest::RecordedText(), "after the tick");
        EQUALS(scripted.DisposedText(), "");
        scripted.At(20);
        EQUALS(scripted.DisposedText(), "a:busy");
    });

    IT("destroys every runtime on shutdown", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        EQUALS(scripted.Create("c"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "value = 1 setTimeout(function() record('a timer') end, 10)"), "");
        EQUALS(scripted.Run("b", "main.lua", "value = 2"), "");
        EQUALS(scripted.Run("c", "main.lua", "value = 3 destroy('c')"), "");
        scripted.engine.Shutdown();
        EQUALS(scripted.engine.IsInitialized(), false);
        EQUALS(scripted.DisposedText(), "a:1,b:2,c:3");
        EQUALS(scripted.engine.GetRuntime("a") == nullptr, true);
        scripted.At(10);
        EQUALS(LuaEngineTest::RecordedText(), "");
        EQUALS(scripted.Create("a"), false);
        EQUALS(scripted.engine.GetLastError(), "Engine not initialized");
        // And starts over.
        scripted.engine.Shutdown();
        EQUALS(scripted.engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE, true);
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Eval("a", "value"), "nil");
    });

    IT("puts off a shutdown from inside a file until the file has returned", {
        LuaEngineTest::Scripted scripted;
        scripted.RecordClosing();
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.Run("b", "main.lua", "value = 'b' setTimeout(function() record('b timer') end, 10)"), "");
        EQUALS(scripted.Run("a", "main.lua",
                   "value = 'a'\n"
                   "setTimeout(function() record('a timer') end, 10)\n"
                   "shutdown()\n"
                   "record('went on')\n"
                   "value = 'after'\n"
                   "tick(10)\n"
                   "record('ticked')"),
            "");
        // Every state was still open while the script that shut the engine down ran, and closed as it returned.
        EQUALS(LuaEngineTest::RecordedText(), "went on,ticked,closing a,closing b");
        EQUALS(scripted.DisposedText(), "a:after,b:b");
        EQUALS(scripted.engine.IsInitialized(), false);
        EQUALS(scripted.engine.GetRuntime("a") == nullptr, true);
        EQUALS(scripted.engine.GetRuntime("b") == nullptr, true);
        scripted.At(20);
        EQUALS(LuaEngineTest::RecordedText(), "went on,ticked,closing a,closing b");
        EQUALS(scripted.Create("a"), false);
        EQUALS(scripted.engine.GetLastError(), "Engine not initialized");
    });

    IT("takes every runtime away at once on a shutdown from inside a script", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.Run("b", "main.lua", "value = 'b'"), "");
        EQUALS(scripted.Run("a", "main.lua", "shutdown()\nrecord(tostring(pcall(destroy, 'b')))\nshutdown()\nsetTimeout(function() record('armed after') end, 0)"), "");
        EQUALS(LuaEngineTest::RecordedText(), "true");
        EQUALS(scripted.DisposedText(), "a:nil,b:b");
    });

    IT("puts off a shutdown from inside a timer until the tick has returned", {
        LuaEngineTest::Scripted scripted;
        scripted.RecordClosing();
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Create("b"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "setTimeout(function() shutdown() record('timer went on') end, 10)\nsetTimeout(function() record('same tick') end, 10)"), "");
        EQUALS(scripted.Run("b", "main.lua", "setTimeout(function() record('b timer') end, 10)"), "");
        scripted.At(10);
        EQUALS(LuaEngineTest::RecordedText(), "timer went on,closing a,closing b");
        EQUALS(scripted.engine.IsInitialized(), false);
    });

    IT("ends a shutdown from a coroutine the host started at the next tick", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        scripted.At(0);
        EQUALS(scripted.Run("a", "main.lua", "value = 'old' function stop() shutdown() record('went on') end"), "");
        Framework::Scripting::LuaRuntime *runtime = scripted.engine.GetRuntime("a");
        lua_getglobal(runtime->State(), "stop");
        runtime->Scheduler().Start(0);
        EQUALS(LuaEngineTest::RecordedText(), "went on");
        EQUALS(scripted.engine.IsInitialized(), false);
        EQUALS(scripted.engine.GetRuntime("a") == nullptr, true);
        // The engine was not on the way out of that call: it finds out at its next tick.
        EQUALS(scripted.DisposedText(), "");
        scripted.At(10);
        EQUALS(scripted.DisposedText(), "a:old");
        EQUALS(scripted.engine.IsInitialized(), false);
    });

    IT("starts over after a shutdown from inside a script", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.Run("a", "main.lua", "value = 'old' function stop() shutdown() end"), "");
        Framework::Scripting::LuaRuntime *runtime = scripted.engine.GetRuntime("a");
        lua_getglobal(runtime->State(), "stop");
        runtime->Scheduler().Start(0);
        EQUALS(scripted.DisposedText(), "");
        // The name is free for the next runtime, and the old one goes as soon as the engine is called again.
        EQUALS(scripted.engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE, true);
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.DisposedText(), "a:old");
        EQUALS(scripted.Run("a", "main.lua", "value = 'new'"), "");
        scripted.At(10);
        EQUALS(scripted.DisposedText(), "a:old");
        EQUALS(scripted.Eval("a", "value"), "new");
    });

    IT("ends a shutdown that is still pending when it goes", {
        std::vector<std::string> disposed;
        {
            Framework::Scripting::LuaEngine engine;
            engine.SetRuntimeSetupCallback([&engine](Framework::Scripting::LuaRuntime &runtime) {
                LuaEngineTest::AddShutdown(runtime, engine);
            });
            engine.AddRuntimeDisposingListener([&disposed](Framework::Scripting::LuaRuntime &runtime) {
                disposed.push_back(runtime.Options().resourceName);
            });
            EQUALS(engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE, true);
            EQUALS(engine.CreateResourceRuntime("a"), true);
            Framework::Scripting::LuaRuntime *runtime = engine.GetRuntime("a");
            lua_getglobal(runtime->State(), "shutdown");
            runtime->Scheduler().Start(0);
            EQUALS(engine.IsInitialized(), false);
            EQUALS(disposed.size(), 0u);
        }
        EQUALS(disposed.size(), 1u);
    });

    IT("destroys its runtimes when it goes", {
        std::vector<std::string> disposed;
        {
            Framework::Scripting::LuaEngine engine;
            engine.AddRuntimeDisposingListener([&disposed](Framework::Scripting::LuaRuntime &runtime) {
                disposed.push_back("first " + runtime.Options().resourceName);
            });
            engine.AddRuntimeDisposingListener([&disposed](Framework::Scripting::LuaRuntime &runtime) {
                disposed.push_back("second " + runtime.Options().resourceName);
            });
            EQUALS(engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE, true);
            EQUALS(engine.CreateResourceRuntime("a"), true);
            EQUALS(disposed.size(), 0u);
        }
        EQUALS(disposed.size(), 2u);
        EQUALS(disposed[0], "first a");
        EQUALS(disposed[1], "second a");
    });

    // --- Setup ---

    IT("runs the setup callback once in every runtime, before any script", {
        LuaEngineTest::Scripted scripted;
        EQUALS(scripted.setups, 0);
        EQUALS(scripted.Create("a"), true);
        EQUALS(scripted.setups, 1);
        EQUALS(scripted.Create("b"), true);
        EQUALS(scripted.setups, 2);
        EQUALS(scripted.Run("a", "main.lua", "kind = type(record)"), "");
        EQUALS(scripted.Run("a", "second.lua", "kind = kind .. type(destroy)"), "");
        EQUALS(scripted.Eval("a", "kind"), "functionfunction");
        EQUALS(scripted.setups, 2);
        EQUALS(lua_gettop(scripted.engine.GetRuntime("a")->State()), 0);
    });

    IT("creates a runtime with no setup callback", {
        Framework::Scripting::LuaEngine engine;
        EQUALS(engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE, true);
        EQUALS(engine.CreateResourceRuntime("a"), true);
        EQUALS(LuaRuntimeTest::Eval(*engine.GetRuntime("a"), "type(sleep) .. type(require)"), "functionfunction");
    });

    IT("fails the creation of a runtime whose setup fails", {
        std::vector<std::string> disposed;
        std::string failure = "lua";
        Framework::Scripting::LuaEngine engine;
        engine.SetRuntimeSetupCallback([&failure](Framework::Scripting::LuaRuntime &runtime) {
            if (failure == "lua") {
                luaL_error(runtime.State(), "Thing.register: cannot bind");
            }
            if (failure == "exception") {
                throw std::runtime_error("Bind: declared twice");
            }
        });
        engine.AddRuntimeDisposingListener([&disposed](Framework::Scripting::LuaRuntime &runtime) {
            disposed.push_back(runtime.Options().resourceName);
        });
        EQUALS(engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE, true);

        EQUALS(engine.CreateResourceRuntime("a"), false);
        EQUALS(LuaRuntimeTest::FirstLine(engine.GetLastError()), "Thing.register: cannot bind");
        EQUALS(engine.GetRuntime("a") == nullptr, true);
        EQUALS(disposed.size(), 1u);

        failure = "exception";
        EQUALS(engine.CreateResourceRuntime("a"), false);
        EQUALS(LuaRuntimeTest::FirstLine(engine.GetLastError()), "Bind: declared twice");
        EQUALS(engine.GetRuntime("a") == nullptr, true);
        EQUALS(disposed.size(), 2u);

        failure.clear();
        EQUALS(engine.CreateResourceRuntime("a"), true);
        EQUALS(engine.GetRuntime("a") != nullptr, true);
        EQUALS(disposed.size(), 2u);
    });

    IT("fails the creation of a runtime that its setup destroys", {
        std::vector<std::string> disposed;
        std::string doing = "destroy";
        Framework::Scripting::LuaEngine engine;
        engine.SetRuntimeSetupCallback([&engine, &doing](Framework::Scripting::LuaRuntime &runtime) {
            if (doing == "destroy" || doing == "destroy and fail") {
                engine.DestroyResourceRuntime(runtime.Options().resourceName);
            }
            if (doing == "shutdown") {
                engine.Shutdown();
            }
            if (doing == "destroy and fail") {
                luaL_error(runtime.State(), "failed as well");
            }
        });
        engine.AddRuntimeDisposingListener([&disposed](Framework::Scripting::LuaRuntime &runtime) {
            disposed.push_back(runtime.Options().resourceName);
        });
        EQUALS(engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE, true);

        EQUALS(engine.CreateResourceRuntime("a"), false);
        EQUALS(engine.GetLastError(), "resource runtime was destroyed during setup");
        EQUALS(engine.GetRuntime("a") == nullptr, true);
        // It was running its setup: it goes at the next tick.
        EQUALS(disposed.size(), 0u);
        engine.Tick();
        EQUALS(disposed.size(), 1u);

        doing = "destroy and fail";
        EQUALS(engine.CreateResourceRuntime("a"), false);
        EQUALS(LuaRuntimeTest::FirstLine(engine.GetLastError()), "failed as well");
        EQUALS(engine.GetRuntime("a") == nullptr, true);
        engine.Tick();
        EQUALS(disposed.size(), 2u);

        doing = "shutdown";
        EQUALS(engine.CreateResourceRuntime("a"), false);
        EQUALS(engine.GetLastError(), "resource runtime was destroyed during setup");
        EQUALS(engine.IsInitialized(), false);
        EQUALS(disposed.size(), 3u);

        doing.clear();
        EQUALS(engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE, true);
        EQUALS(engine.CreateResourceRuntime("a"), true);
        EQUALS(engine.GetRuntime("a") != nullptr, true);
        EQUALS(disposed.size(), 3u);
    });

    IT("fails the creation of a runtime that does not fit its memory limit", {
        Framework::Scripting::LuaEngineOptions options;
        options.memoryLimitBytes = 1024;
        Framework::Scripting::LuaEngine engine(options);
        EQUALS(engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE, true);
        EQUALS(engine.CreateResourceRuntime("a"), false);
        EQUALS(LuaRuntimeTest::Contains(engine.GetLastError(), "cannot create the Lua state of 'a'"), true);
        EQUALS(engine.GetRuntime("a") == nullptr, true);
    });

    IT("stops a file at the deadline of the engine", {
        Framework::Scripting::LuaEngineOptions options;
        options.entryDeadline = std::chrono::milliseconds(50);
        LuaEngineTest::Scripted scripted(options);
        EQUALS(scripted.Create("a"), true);
        LuaRuntimeTest::AddSpin(*scripted.engine.GetRuntime("a"));
        EQUALS(LuaRuntimeTest::FirstLine(scripted.Run("a", "main.lua", "\nspin()")), "spin:1: script ran too long");
        EQUALS(scripted.Eval("a", "busy()"), "true");
        // A module stopped while it loads is not left loading.
        scripted.files.Write("a", "slow.lua", "spin()");
        EQUALS(LuaRuntimeTest::FirstLine(scripted.Run("a", "second.lua", "require('slow')")), "spin:1: script ran too long");
        EQUALS(scripted.Eval("a", "busy()"), "true");
        EQUALS(LuaRuntimeTest::FirstLine(scripted.Run("a", "third.lua", "require('slow')")), "spin:1: script ran too long");
    });
});
