/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_runtime_ut.h"

#include "core_modules.h"
#include "networking/network_server.h"
#include "networking/replication/entity_registry.h"
#include "networking/replication/replication_manager.h"
#include "scripting/bind/call.h"
#include "scripting/builtins/builtins.h"
#include "scripting/builtins/console.h"
#include "scripting/builtins/state_bag.h"
#include "scripting/engine_scope.h"
#include "scripting/lua/lua_client_host.h"
#include "scripting/lua/lua_engine.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/resource/package_manifest.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/resource/resource_packager.h"
#include "scripting/scripting_catalog.h"
#include "scripting/v8_engine.h"
#include "utils/package/package.h"
#include "utils/vfs.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Lua on a client: the V8 engine, the Lua host and the resource manager put together as ClientScriptingModule puts
// them, which builds on Windows only. The scripts are what a server sent, so the limits are asserted here.
namespace LuaClientTest {
    using namespace Framework::Scripting;
    using Framework::Utils::Vfs;
    using LuaRuntimeTest::Contains;

    using Sources = std::vector<std::pair<std::string, std::string>>;

    // The directory of a resource on disk, as a server has it. Removed with the object.
    struct Directory final {
        std::filesystem::path root = std::filesystem::temp_directory_path() / ("framework_lua_client_ut_" + std::to_string(std::random_device {}()));

        Directory() {
            std::filesystem::create_directories(root);
        }

        ~Directory() {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }

        // Writes a resource: its files, and a manifest whose "mafiahub" object is `{<mafiahub>}`.
        void Resource(const std::string &name, const std::string &mafiahub, const Sources &files) const {
            const auto write = [&](const std::string &file, const std::string &content) {
                const std::filesystem::path path = root / name / file;
                std::filesystem::create_directories(path.parent_path());
                std::ofstream(path, std::ios::binary) << content;
            };
            write("package.json", "{\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"mafiahub\":{" + mafiahub + "}}");
            for (const auto &[file, content] : files) {
                write(file, content);
            }
        }
    };

    // Packages a resource of `directory` as a server does and mounts it where a client reads its resources.
    // Empty when it is mounted, the error otherwise. Unmount it with `<name>.zip`.
    inline std::string MountPackaged(const Directory &directory, const std::string &name) {
        Vfs::Get().Init(nullptr);
        PackageManifest manifest;
        if (!manifest.Parse((directory.root / name / "package.json").string())) {
            return "manifest";
        }
        PackagedResource packaged;
        std::string zip, error;
        if (!ResourcePackager::Package(name, (directory.root / name).string(), manifest, nullptr, packaged, error)) {
            return error;
        }
        if (!Framework::Utils::Package::Open(packaged.blob, nullptr, zip, error)) {
            return error;
        }
        Vfs::Get().Unmount(name + ".zip");
        return Vfs::Get().MountMemory(std::move(zip), name + ".zip", Vfs::ResourcePath(name), error) ? std::string() : error;
    }

    // Writes one resource, packages it and mounts it.
    inline std::string Mount(const std::string &name, const std::string &mafiahub, const Sources &files) {
        const Directory directory;
        directory.Resource(name, mafiahub, files);
        return MountPackaged(directory, name);
    }

    // A replication manager on a peer that was never started, with one entity for a script to name.
    struct World final {
        Framework::Networking::NetworkServer peer;
        std::string id;

        World() {
            auto *replication = peer.GetReplicationManager();
            replication->Init(&peer, true);
            replication->ConfigureGrid(100, -4096, 4096);
            id = std::to_string(replication->CreateEntity(Framework::Networking::Replication::EntityRegistry::Get().Register<Framework::Networking::Replication::NetworkEntity>("Test::LuaClientWatched"))->GetNetworkID());
            Framework::CoreModules::SetReplication(replication);
        }

        ~World() {
            Framework::CoreModules::SetReplication(nullptr);
            peer.GetReplicationManager()->Clear();
        }
    };

    // The scripting of a client. Init, Update, Reset and Shutdown do what those of ClientScriptingModule do, in
    // its order, with the shared bindings of JavaScript only.
    struct Client final {
        std::string resourcesPath;
        V8Engine v8;
        LuaClientHost lua;
        std::unique_ptr<ResourceManager> manager;
        // "<resource>: <first line>" for each error the manager told its listeners of.
        std::vector<std::string> errors;

        explicit Client(std::string path = Vfs::kResourceMountRoot, LuaEngineOptions options = LuaEngineOptions::Client()): resourcesPath(std::move(path)), lua(options) {
            Vfs::Get().Init(nullptr);
        }

        ~Client() {
            Shutdown();
        }

        bool Init() {
            v8.SetModuleRootPath(resourcesPath);
            if (v8.Init() != ScriptingError::SCRIPTING_NONE) {
                return false;
            }
            ResourceManagerConfig config;
            config.resourcesPath         = resourcesPath;
            config.isClient              = true;
            config.cascadeStopDependents = true;
            if (manager) {
                manager->SetConfig(config);
            }
            else {
                manager = std::make_unique<ResourceManager>(&v8, config);
                manager->AddOnResourceError([this](const std::string &name, const std::string &error) {
                    errors.push_back(name + ": " + LuaRuntimeTest::FirstLine(error));
                });
            }
            if (lua.Init(*manager, "1.2.3") != ScriptingError::SCRIPTING_NONE) {
                return false;
            }

            EngineScope scope(&v8);
            SetScriptingEnvironment(scope.Isolate(), /*isClient*/ true);
            Builtins::RegisterValueTypes(scope.Isolate(), scope.Context()->Global());
            manager->GetEvents().Register(scope.Isolate(), scope.Context(), scope.Context()->Global(), manager.get(), /*isClient*/ true);
            Builtins::Console::Register(scope.Isolate(), scope.Context(), manager.get());
            return true;
        }

        void Update() {
            manager->ProcessHandlerFailures();
            manager->ProcessScheduledRestarts();
            lua.Update();
            v8.Tick();
        }

        // Lets `ms` pass and updates.
        void Wait(int ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            Update();
        }

        void Reset() {
            manager->Reset();
            lua.Shutdown();
            v8.ResetContext();
        }

        void Shutdown() {
            if (manager) {
                manager->StopAll();
                manager.reset();
            }
            lua.Shutdown();
            v8.Shutdown();
        }

        // Discovers what is there and starts one resource. Empty when it started, the error otherwise.
        std::string Start(const std::string &resource) {
            manager->DiscoverResources();
            const auto result = manager->StartResource(resource);
            return result ? std::string() : result.GetError();
        }

        bool HasRuntime(const std::string &resource) const {
            return lua.GetEngine().GetRuntime(resource) != nullptr;
        }

        // The value of `expression` in the Lua state of the resource, as tostring() spells it.
        std::string Eval(const std::string &resource, const std::string &expression) const {
            LuaRuntime *runtime = lua.GetEngine().GetRuntime(resource);
            return runtime != nullptr ? LuaRuntimeTest::Eval(*runtime, expression) : "<no runtime>";
        }

        // Whether `condition` holds in the one isolate of the client.
        bool JavaScript(const std::string &condition) {
            return v8.Execute("if (!(" + condition + ")) throw Error('no');");
        }
    };
} // namespace LuaClientTest

MODULE(lua_client, {
    using namespace Framework::Scripting;
    using Framework::Utils::Vfs;
    using LuaClientTest::Client;
    using LuaClientTest::Contains;
    using LuaClientTest::Directory;
    using LuaClientTest::Mount;
    using LuaClientTest::MountPackaged;

    // ==================== Engine ====================

    IT("runs client states as the client side, under 128 MB and a deadline of five seconds", {
        const LuaEngineOptions options = LuaEngineOptions::Client();
        EQUALS(options.side == Bind::Side::Client, true);
        EQUALS(options.memoryLimitBytes, size_t {128} * 1024 * 1024);
        EQUALS(options.entryDeadline.count(), 5000);

        EQUALS(Mount("limits", R"("clientScripts":["client.lua"])", {{"client.lua", "loaded = true"}}), "");
        Client client;
        EQUALS(client.Init(), true);
        EQUALS(client.Start("limits"), "");
        const LuaRuntimeOptions &state = client.lua.GetEngine().GetRuntime("limits")->Options();
        EQUALS(state.side == Bind::Side::Client, true);
        EQUALS(state.memoryLimitBytes, options.memoryLimitBytes);
        EQUALS(state.entryDeadline.count(), 5000);
        Vfs::Get().Unmount("limits.zip");
    });

    // ==================== Resources ====================

    IT("starts a Lua client resource from a directory, with its shared scripts and without its server ones", {
        const Directory directory;
        directory.Resource("plain", R"("clientScripts":["client.lua"],"sharedScripts":["shared.lua"],"serverScripts":["server.lua"])",
            {{"shared.lua", "order = 'shared'"}, {"client.lua", "order = order .. ',client,' .. require('lib.util').name"}, {"server.lua", "order = 'server'"}, {"lib/util.lua", "return { name = 'util' }"}});
        Client client(directory.root.string());
        EQUALS(client.Init(), true);
        EQUALS(client.Start("plain"), "");
        EQUALS(client.HasRuntime("plain"), true);
        EQUALS(client.Eval("plain", "order"), "shared,client,util");
    });

    IT("starts a Lua client resource from a mounted package, and requires its modules through the VFS", {
        const Directory directory;
        directory.Resource("packed", R"("clientScripts":["src/client.lua"],"sharedScripts":["shared.lua"],"serverScripts":["server.lua"],"files":["lib/**"])",
            {{"shared.lua", "order = 'shared'"}, {"src/client.lua", "order = order .. ',client,' .. require('lib.util').name .. ',' .. require('lib.deep').name\nwhere = select(2, pcall(error, 'x', 1))\nfailed = select(2, pcall(function() error('here') end))"},
                {"server.lua", "order = 'server'"}, {"lib/util.lua", "return { name = 'util' }"}, {"lib/deep/init.lua", "return { name = 'deep' }"}});
        EQUALS(MountPackaged(directory, "packed"), "");
        EQUALS(Vfs::Get().Contains("/resources/packed/server.lua"), false);

        Client client;
        EQUALS(client.Init(), true);
        EQUALS(client.Start("packed"), "");
        EQUALS(client.Eval("packed", "order"), "shared,client,util,deep");
        // A chunk is named below its resource, whatever the mount.
        EQUALS(client.Eval("packed", "failed"), "packed/src/client.lua:3: here");

        // The server script was never shipped, so nothing of the resource can ask for it.
        EQUALS(Contains(client.Eval("packed", "select(2, pcall(require, 'server'))"), "module 'server' not found in resource 'packed'"), true);
        Vfs::Get().Unmount("packed.zip");
    });

    IT("fails the start of a client resource whose Lua script is not in its package", {
        Framework::Utils::Package::Writer writer;
        writer.Add("package.json", R"({"name":"hollow","version":"1.0.0","mafiahub":{"clientScripts":["client.lua"]}})");
        std::string blob, zip, error;
        EQUALS(writer.Build(nullptr, blob), true);
        EQUALS(Framework::Utils::Package::Open(blob, nullptr, zip, error), true);
        Client client;
        EQUALS(Vfs::Get().MountMemory(std::move(zip), "hollow.zip", Vfs::ResourcePath("hollow"), error), true);
        EQUALS(client.Init(), true);
        EQUALS(client.Start("hollow").empty(), false);
        EQUALS(client.HasRuntime("hollow"), false);
        Vfs::Get().Unmount("hollow.zip");
    });

    // ==================== Bindings ====================

    IT("gives a client state the shared builtins, with the members of the client side only", {
        const LuaClientTest::World world;
        const std::string &id = world.id;

        EQUALS(Mount("builtins", R"("clientScripts":["client.lua"])", {{"client.lua", R"(
            heard, fired = 'nothing', 0
            Events.on('ping', function(vector, text) heard = text .. ':' .. vector.length end)
            Events.emit('ping', Vector3.new(3, 4, 0), 'pong')
            setTimeout(function() fired = fired + 1 end, 1)
            console.log('hello from a client state')
            player = Player.new()" + id + R"()
        )"}}),
            "");
        Client client;
        EQUALS(client.Init(), true);
        LuaRuntimeTest::LogCapture log;
        EQUALS(client.Start("builtins"), "");

        EQUALS(client.Eval("builtins", "heard"), "pong:5.0");
        EQUALS(client.Eval("builtins", "fired"), "0");
        client.Wait(5);
        EQUALS(client.Eval("builtins", "fired"), "1");
        const std::vector<std::string> lines = log.Lines();
        EQUALS(std::count(lines.begin(), lines.end(), "[builtins] hello from a client state"), 1);

        const std::string globals = "(function() local types = {} for _, name in ipairs({'Vector2', 'Vector3', 'Vector4', 'Quaternion', 'Color', 'Entity', 'Player', 'TextLabel', 'Events', 'Messages', 'Exports', 'Imports', 'console', "
                                    "'ExecutionEnvironment', 'setTimeout', 'setInterval', 'clearTimeout', 'clearInterval', 'sleep', 'spawn'}) do types[#types + 1] = type(_G[name]) == 'nil' and name or nil end return table.concat(types, ',') end)()";
        EQUALS(client.Eval("builtins", globals), "");
        EQUALS(client.Eval("builtins", "tostring(ExecutionEnvironment.isClient) .. tostring(ExecutionEnvironment.isServer) .. ExecutionEnvironment.modVersion"), "truefalse1.2.3");

        // Up to the server, and nothing the server alone does.
        EQUALS(client.Eval("builtins", "type(Events.emitServer) .. type(Events.emitAllClients) .. type(Events.onClient)"), "functionnilnil");
        EQUALS(client.Eval("builtins", "type(player.kick) .. type(player.emit) .. type(player.getIP) .. type(player.id)"), "nilnilnilnumber");

        Vfs::Get().Unmount("builtins.zip");
    });

    IT("exchanges an event between the JavaScript and the Lua script of one client resource", {
        EQUALS(Mount("mixed", R"("clientScripts":["first.js","second.lua"])",
                   {{"first.js", "globalThis.fromLua = 'nothing'; Events.on('toJs', (payload) => { fromLua = payload.from + payload.list.length + (payload.at instanceof Vector3); Events.emit('toLua', { from: 'js', at: new Vector3(1, 2, 3) }); });"},
                       {"second.lua", "fromJs = 'nothing'\nEvents.on('toLua', function(payload) fromJs = payload.from .. payload.at.z end)\nEvents.emit('toJs', { from = 'lua', list = { 1, 2 }, at = Vector3.new(0, 0, 0) })"}}),
            "");
        Client client;
        EQUALS(client.Init(), true);
        EQUALS(client.Start("mixed"), "");
        EQUALS(client.JavaScript("fromLua === 'lua2true'"), true);
        EQUALS(client.Eval("mixed", "fromJs"), "js3.0");

        // The handlers of both go with the resource.
        EQUALS(static_cast<bool>(client.manager->StopResource("mixed")), true);
        EQUALS(client.HasRuntime("mixed"), false);
        EQUALS(client.v8.Execute("fromLua = 'stopped'; Events.emit('toJs', { from: 'js', list: [] });"), true);
        EQUALS(client.JavaScript("fromLua === 'stopped'"), true);
        Vfs::Get().Unmount("mixed.zip");
    });

    // ==================== Sandbox ====================

    IT("gives a client state no way to the machine", {
        EQUALS(Mount("caged", R"("clientScripts":["client.lua"])", {{"client.lua", "loaded = true"}}), "");
        Client client;
        EQUALS(client.Init(), true);
        EQUALS(client.Start("caged"), "");
        const std::string gone = "(function() local names = {} for _, name in ipairs({'io', 'debug', 'package', 'dofile', 'loadfile', 'loadstring', 'module'}) do names[#names + 1] = type(_G[name]) end return table.concat(names, ',') end)()";
        EQUALS(client.Eval("caged", gone), "nil,nil,nil,nil,nil,nil,nil");
        EQUALS(client.Eval("caged", "type(os.execute) .. type(os.remove) .. type(os.rename) .. type(os.getenv) .. type(os.exit) .. type(os.tmpname) .. type(string.dump)"), "nilnilnilnilnilnilnil");
        EQUALS(client.Eval("caged", "type(os.time) .. type(os.clock)"), "functionfunction");
        EQUALS(client.Eval("caged", "select(2, pcall(setmetatable, {}, { __gc = function() end })) ~= nil"), "true");
        Vfs::Get().Unmount("caged.zip");
    });

    IT("loads no bytecode in a client state, from a string or from a file of the package", {
        const std::string bytecode = LuaRuntimeTest::Bytecode("escaped = true");
        EQUALS(bytecode.empty(), false);
        EQUALS(Mount("binary", R"("clientScripts":["client.lua"],"files":["blob.lua"])", {{"client.lua", "loaded = true"}, {"blob.lua", bytecode}}), "");
        EQUALS(Mount("binary-entry", R"("clientScripts":["client.lua"])", {{"client.lua", bytecode}}), "");
        Client client;
        EQUALS(client.Init(), true);
        EQUALS(client.Start("binary"), "");

        LuaRuntimeTest::SetGlobalString(*client.lua.GetEngine().GetRuntime("binary"), "bytecode", bytecode);
        EQUALS(client.Eval("binary", "type(load(bytecode))"), "nil");
        EQUALS(client.Eval("binary", "pcall(load, function() local chunk = bytecode bytecode = nil return chunk end)"), "false");
        EQUALS(client.Eval("binary", "pcall(require, 'blob')"), "false");
        EQUALS(client.Eval("binary", "escaped"), "nil");

        EQUALS(client.Start("binary-entry").empty(), false);
        EQUALS(client.HasRuntime("binary-entry"), false);
        Vfs::Get().Unmount("binary.zip");
        Vfs::Get().Unmount("binary-entry.zip");
    });

    IT("requires nothing from outside the resource on a client", {
        EQUALS(Mount("neighbour", R"("clientScripts":["client.lua"],"files":["secret.lua"])", {{"client.lua", "loaded = true"}, {"secret.lua", "return 'the secret'"}}), "");
        EQUALS(Mount("nosy", R"("clientScripts":["client.lua"],"files":["own.lua"])", {{"client.lua", "loaded = true"}, {"own.lua", "return 'own'"}}), "");
        EQUALS(Vfs::Get().Contains("/resources/neighbour/secret.lua"), true);
        Client client;
        EQUALS(client.Init(), true);
        EQUALS(client.Start("nosy"), "");
        EQUALS(client.Eval("nosy", "require('own')"), "own");
        for (const char *name : {"neighbour.secret", "..neighbour.secret", "../neighbour/secret", "/resources/neighbour/secret", "own/../../neighbour/secret", ".neighbour.secret", "neighbour/secret"}) {
            EQUALS(client.Eval("nosy", "pcall(require, '" + std::string(name) + "')"), "false");
        }
        EQUALS(Contains(client.Eval("nosy", "select(2, pcall(require, 'neighbour.secret'))"), "module 'neighbour.secret' not found in resource 'nosy'"), true);
        Vfs::Get().Unmount("neighbour.zip");
        Vfs::Get().Unmount("nosy.zip");
    });

    // The loops here end by themselves after twenty seconds, so that a deadline that does not hold fails its test
    // instead of hanging the suite.
    IT("fails the start of a client resource whose script runs past the deadline", {
        EQUALS(Mount("endless", R"("clientScripts":["client.lua"])", {{"client.lua", "local stop = os.clock() + 20\nwhile os.clock() < stop do end\nsurvived = true"}}), "");
        Client client;
        EQUALS(client.Init(), true);
        const auto start        = std::chrono::steady_clock::now();
        const std::string error = client.Start("endless");
        const auto elapsed      = LuaRuntimeTest::ElapsedMs(start);
        EQUALS(Contains(error, "script ran too long"), true);
        EQUALS(elapsed >= 5000 && elapsed < 10000, true);
        EQUALS(client.HasRuntime("endless"), false);
        EQUALS(client.manager->GetResourceState("endless") == ResourceState::Running, false);
        Vfs::Get().Unmount("endless.zip");
    });

    IT("cuts a client timer that runs past the deadline short, and its resource does what its errorBehavior says", {
        const std::string spin = "local stop = os.clock() + 20 while os.clock() < stop do end";
        EQUALS(Mount("slow-timer", R"("clientScripts":["client.lua"])", {{"client.lua", "setTimeout(function() " + spin + " end, 1)"}}), "");
        EQUALS(Mount("slow-kept", R"("clientScripts":["client.lua"],"errorBehavior":"continue")", {{"client.lua", "ticks = 0\nsetInterval(function() ticks = ticks + 1 end, 1)\nsetTimeout(function() " + spin + " end, 1)"}}), "");
        LuaEngineOptions options = LuaEngineOptions::Client();
        options.entryDeadline    = std::chrono::milliseconds(100);
        Client client(Vfs::kResourceMountRoot, options);
        EQUALS(client.Init(), true);
        EQUALS(client.Start("slow-timer"), "");
        EQUALS(client.Start("slow-kept"), "");

        const auto start = std::chrono::steady_clock::now();
        client.Wait(5);
        EQUALS(LuaRuntimeTest::ElapsedMs(start) < 5000, true);

        EQUALS(client.errors.size(), 2u);
        EQUALS(std::count(client.errors.begin(), client.errors.end(), "slow-timer: slow-timer/client.lua:1: script ran too long"), 1);
        EQUALS(std::count(client.errors.begin(), client.errors.end(), "slow-kept: slow-kept/client.lua:3: script ran too long"), 1);
        EQUALS(client.manager->GetResourceState("slow-timer") == ResourceState::Error, true);
        EQUALS(client.HasRuntime("slow-timer"), false);

        // The one that said to go on does, with the timers it had.
        EQUALS(client.HasRuntime("slow-kept"), true);
        const std::string before = client.Eval("slow-kept", "ticks");
        client.Wait(5);
        EQUALS(client.Eval("slow-kept", "ticks") != before, true);
        Vfs::Get().Unmount("slow-timer.zip");
        Vfs::Get().Unmount("slow-kept.zip");
    });

    IT("cuts a client event handler that runs past the deadline short, and tells the emitter", {
        const std::string spin = "local stop = os.clock() + 20 while os.clock() < stop do end";
        EQUALS(Mount("slow-handler", R"("clientScripts":["client.js","client.lua"])",
                   {{"client.js", "globalThis.spinError = 'none'; globalThis.spin = () => Events.emit('spin').catch((error) => { spinError = error.message + '|' + (error.errors || []).map((inner) => inner.message).join(); });"},
                       {"client.lua", "after = 0\nEvents.on('spin', function() " + spin + " after = after + 1 end)"}}),
            "");
        LuaEngineOptions options = LuaEngineOptions::Client();
        options.entryDeadline    = std::chrono::milliseconds(100);
        Client client(Vfs::kResourceMountRoot, options);
        EQUALS(client.Init(), true);
        EQUALS(client.Start("slow-handler"), "");

        const auto start = std::chrono::steady_clock::now();
        EQUALS(client.v8.Execute("spin();", "/resources/slow-handler/client.js"), true);
        client.Wait(5);
        EQUALS(LuaRuntimeTest::ElapsedMs(start) < 5000, true);
        EQUALS(client.JavaScript("spinError.startsWith('One or more event handlers failed') && spinError.includes('script ran too long')"), true);
        // The failure is the emitter's to handle, and the state is whole for the next entry.
        EQUALS(client.Eval("slow-handler", "after"), "0");
        EQUALS(client.errors.size(), 0u);
        Vfs::Get().Unmount("slow-handler.zip");
    });

    IT("bounds the memory of a client state", {
        // A megabyte at a time, and far more of them than the cap leaves room for.
        const std::string hoard = "hoard = {}\nfor i = 1, 4096 do hoard[i] = string.rep('x', 1048576) .. i end";
        EQUALS(Mount("greedy", R"("clientScripts":["client.lua"])", {{"client.lua", hoard}}), "");
        EQUALS(Mount("greedy-caught", R"("clientScripts":["client.lua"])", {{"client.lua", "caught = select(2, pcall(load([[" + hoard + "]])))\nheld = #hoard\nhoard = nil"}}), "");
        Client client;
        EQUALS(client.Init(), true);
        EQUALS(Contains(client.Start("greedy"), "not enough memory"), true);
        EQUALS(client.HasRuntime("greedy"), false);

        // A script that catches the refusal goes on, having got no further than the cap.
        EQUALS(client.Start("greedy-caught"), "");
        EQUALS(Contains(client.Eval("greedy-caught", "caught"), "not enough memory"), true);
        EQUALS(client.Eval("greedy-caught", "held < 128"), "true");
        EQUALS(client.lua.GetEngine().GetRuntime("greedy-caught")->MemoryInUse() <= size_t {128} * 1024 * 1024, true);
        Vfs::Get().Unmount("greedy.zip");
        Vfs::Get().Unmount("greedy-caught.zip");
    });

    // ==================== Reconnection ====================

    IT("drops every Lua state on a reset, and starts clean after it", {
        const auto mount = [](int version) {
            return Mount("session", R"("clientScripts":["client.js","client.lua"])",
                {{"client.js", "globalThis.pings = 0; Events.on('ping', () => ++pings);"},
                    {"client.lua", "loads = (loads or 0) + 1\nversion = " + std::to_string(version) + "\nticks, heard = 0, 0\nsetInterval(function() ticks = ticks + 1 end, 1)\nEvents.on('ping', function() heard = heard + 1 end)\nsetTimeout(function() error('late') end, 50)"}});
        };
        EQUALS(mount(1), "");
        Client client;
        EQUALS(client.Init(), true);
        EQUALS(client.Start("session"), "");
        client.Wait(5);
        EQUALS(client.Eval("session", "ticks") != "0", true);
        LuaEngine &engine = client.lua.GetEngine();
        EQUALS(engine.IsInitialized(), true);

        client.Reset();
        EQUALS(client.HasRuntime("session"), false);
        EQUALS(engine.IsInitialized(), false);
        EQUALS(client.manager->HasResource("session"), false);
        // Nothing of the session that ended is left to fire or to report.
        client.Wait(60);
        EQUALS(client.errors.size(), 0u);
        // Before the next Init a Lua resource has nothing to run in.
        EQUALS(Contains(client.Start("session"), "Lua scripting is not available"), true);

        EQUALS(mount(2), "");
        EQUALS(client.Init(), true);
        EQUALS(client.Start("session"), "");
        EQUALS(client.Eval("session", "loads .. ':' .. version .. ':' .. heard"), "1:2:0");
        EQUALS(client.v8.Execute("Events.emit('ping');"), true);
        EQUALS(client.Eval("session", "heard"), "1");
        EQUALS(client.JavaScript("pings === 1"), true);
        client.Wait(5);
        EQUALS(client.Eval("session", "ticks") != "0", true);

        client.Shutdown();
        EQUALS(engine.IsInitialized(), false);
        EQUALS(engine.GetRuntime("session") == nullptr, true);
        Vfs::Get().Unmount("session.zip");
    });

    IT("lets go of what the builtins kept for a Lua state that a stop or a reset took", {
        using Builtins::StateBag;
        const LuaClientTest::World world;
        const std::string script = "Entity.new(" + world.id + ").state:onChange(nil, function() end)";
        EQUALS(Mount("watcher", R"("clientScripts":["client.lua"])", {{"client.lua", script}}), "");
        EQUALS(Mount("watcher-too", R"("clientScripts":["client.lua"])", {{"client.lua", script}}), "");
        Client client;
        EQUALS(client.Init(), true);
        EQUALS(client.Start("watcher"), "");
        EQUALS(client.Start("watcher-too"), "");
        EQUALS(StateBag::SubscriptionCount(), 2u);
        // Kept by the address of the state, which the next state may get.
        const void *stopped = client.lua.GetEngine().GetRuntime("watcher");
        const void *reset   = client.lua.GetEngine().GetRuntime("watcher-too");
        EQUALS(Bind::Detail::CallResourceManager(stopped) == client.manager.get(), true);
        EQUALS(Bind::Detail::CallResourceManager(reset) == client.manager.get(), true);

        EQUALS(static_cast<bool>(client.manager->StopResource("watcher")), true);
        EQUALS(StateBag::SubscriptionCount(), 1u);
        EQUALS(Bind::Detail::CallResourceManager(stopped) == nullptr, true);
        client.Reset();
        EQUALS(StateBag::SubscriptionCount(), 0u);
        EQUALS(Bind::Detail::CallResourceManager(reset) == nullptr, true);
        Vfs::Get().Unmount("watcher.zip");
        Vfs::Get().Unmount("watcher-too.zip");
    });
});
