/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_events_ut.h"
#include "lua_parity_helpers.h"
#include "scripting_catalog_golden_ut.h"

#include "integrations/server/scripting/module.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

// The acceptance gate of Lua scripting on a server: everything the JavaScript catalog documents is there in the Lua
// state of a resource of a real server scripting module, and so is everything the Lua definition file declares.
namespace LuaParityTest {
    using Framework::Integrations::Server::Scripting::ServerScriptingModule;

    // What a server gives JavaScript and not Lua, each with its reason.
    inline const LuaParity::Exemptions &Exemptions() {
        static const LuaParity::Exemptions exemptions = {
            // The microtask queue orders callbacks between the turns of a Promise. Lua has no promises: a
            // coroutine that waits is resumed by the scheduler, and setTimeout(f, 0) defers a call.
            {"queueMicrotask", "JavaScript's own queue"},
            // In the catalog for its type. No script of either language names it: a bag is what `entity.state`
            // answers. Its members are held to the gate like those of any class.
            {"StateBag", "not a global of JavaScript either"},
            // toJSON is what JSON.stringify asks a JavaScript object for. Lua has no such protocol: the JSON
            // encoder of the framework writes the same object from the properties the class names for it
            // (Class::JsonProperties), so a script has nothing to call.
            {"Vector2.toJSON", "the protocol of JSON.stringify"},
            {"Vector3.toJSON", "the protocol of JSON.stringify"},
            {"Vector4.toJSON", "the protocol of JSON.stringify"},
            {"Quaternion.toJSON", "the protocol of JSON.stringify"},
            {"Color.toJSON", "the protocol of JSON.stringify"},
        };
        return exemptions;
    }
} // namespace LuaParityTest

MODULE(lua_parity, {
    using Framework::Scripting::Bind::Side;
    using LuaEventsTest::Bus;
    using LuaParityTest::ServerScriptingModule;

    IT("gives Lua every member of every global the server catalog documents for JavaScript", {
        const nlohmann::json catalog = ScriptingCatalogGolden::BuildServerCatalog();
        EQUALS(catalog.is_object(), true);

        Bus bus;
        bus.server.files.Resource("lua", {{"main.lua", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("lua"), "");
        Framework::Scripting::LuaRuntime *runtime = bus.server.module.GetLuaEngine()->GetRuntime("lua");
        NEQUALS(runtime, nullptr);
        lua_State *L = runtime->State();

        const auto builtins = ServerScriptingModule::LuaBuiltinDefinitions();
        LuaParity::Gate gate(L, Side::Server, builtins, LuaParityTest::Exemptions());
        const LuaParity::Report report = gate.Check(catalog);

        EQUALS(LuaParity::Join(report.missing), "");
        EQUALS(LuaParity::Join(report.stale), "");
        // The gate looked at the whole catalog: 8 global objects, 9 classes and PendingConnection, which is a data
        // type with a class behind it; EventMap, TextLabelOptions and Timeout have none.
        EQUALS(report.symbols, 18);
        EQUALS(report.dataTypes, 3);
        EQUALS(report.members >= 200, true);
        // A builtin registered for one language alone.
        EQUALS(LuaParity::Join(LuaParity::OneSided(catalog, builtins, Side::Server)), "");
        EQUALS(static_cast<size_t>(report.exempted), LuaParityTest::Exemptions().size());
    });

    IT("has in a Lua state everything the server definition file declares, and declares every global", {
        Bus bus;
        bus.server.files.Resource("lua", {{"main.lua", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("lua"), "");
        Framework::Scripting::LuaRuntime *runtime = bus.server.module.GetLuaEngine()->GetRuntime("lua");
        NEQUALS(runtime, nullptr);
        lua_State *L = runtime->State();

        const auto builtins                = ServerScriptingModule::LuaBuiltinDefinitions();
        const std::string definitions      = LuaParity::Generate(builtins, Side::Server);
        const LuaParity::Declared declared = LuaParity::CheckDefinitions(definitions, L, Side::Server, builtins);

        EQUALS(LuaParity::CompileError(definitions), "");
        EQUALS(LuaParity::Join(declared.missing), "");
        EQUALS(LuaParity::Join(LuaParity::UndeclaredGlobals(L, declared.globals)), "");
        // 16 global tables and 8 global functions; StateBag and PendingConnection are tables no global names.
        EQUALS(declared.globals.size(), 24u);
        EQUALS(declared.tables.size(), 18u);
        EQUALS(declared.members >= 200, true);
        // Five operators for each of the three vectors, with two more in their description; Quaternion and Color.
        EQUALS(declared.operators, 3 * 7 + 2 + 1);
        // The value types and the three handles.
        EQUALS(declared.callable, 8);

        // Every global the JavaScript catalog documents and Lua has is declared.
        const nlohmann::json fixture = ScriptingCatalogGolden::ReadFixture();
        EQUALS(fixture.is_object(), true);
        std::set<std::string> undeclared;
        for (const nlohmann::json &symbol : fixture["symbols"]) {
            const std::string name = symbol.value("name", std::string());
            if (symbol.value("kind", std::string()) != "dataType" && !LuaParityTest::Exemptions().contains(name) && !declared.globals.contains(name)) {
                undeclared.insert(name);
            }
        }
        EQUALS(LuaParity::Join(undeclared), "");
    });

    IT("the server definition file matches the committed one", {
        const std::string generated = LuaParity::Generate(ServerScriptingModule::LuaBuiltinDefinitions(), Side::Server);
        if (LuaParity::UpdateRequested()) {
            EQUALS(LuaParity::WriteDefinitions("server.d.lua", generated), true);
        }
        EQUALS(LuaParity::FirstDifference(LuaParity::ReadDefinitions("server.d.lua"), generated), "");
    });

    IT("writes a definition file from descriptions: sides, bases, operators, what waits and what is no global", {
        using namespace Framework::Scripting::Bind;
        const auto param = [](std::string name, TypeRef type, std::string description, bool optional = false, bool rest = false) {
            return ParamDesc {std::move(name), std::move(type), optional, std::move(description), rest};
        };

        ClassDesc base;
        base.name        = "Shape";
        base.description = "A shape.\nOf any kind.";
        base.global      = false;
        base.properties  = {{"sides", TypeOf<int>(), "How many sides.", true, false, Side::Shared}};

        ClassDesc square;
        square.name        = "Square";
        square.description = "A square.";
        square.bases       = {"Shape"};
        square.constructor = FunctionDesc {"constructor", "Makes a square.", {param("size", TypeOf<double>(), "Side length."), param("end", TypeOf<std::string>(), "A label.", true)}, {}, "", false, Side::Shared};
        square.properties  = {{"size", TypeOf<double>(), "", false, false, Side::Shared}, {"owner", Type::Custom("Player | undefined", "Player?"), "Read-only. who owns it, or undefined.", true, false, Side::Server},
            {"shade", TypeOf<int>(), "Drawn shade.", false, false, Side::Client}, {"count", TypeOf<int>(), "How many squares there are; null is not undefined, nullable stays.", true, true, Side::Shared}};
        square.functions   = {
            {"grow", "Grows it.\nIn place.", {param("by", TypeOf<double>(), "How much.")}, Type::This(), "This square.", false, Side::Shared},
            {"save", "Saves it.", {}, Type::Custom("Promise<boolean>", "boolean"), "Whether it was saved.", false, Side::Server},
            {"draw", "Draws it.", {}, {}, "", false, Side::Client},
            {"unit", "A square of size 1.", {}, Type::Class("Square"), "", true, Side::Shared},
        };
        square.luaOperators = {{"add", {Type::Class("Square"), Type::Class("Square")}, Type::Class("Square")}, {"mul", {TypeOf<double>(), Type::Class("Square")}, Type::Class("Square")}, {"unm", {Type::Class("Square")}, Type::Class("Square")},
            {"lt", {Type::Class("Square"), Type::Class("Square")}, TypeOf<bool>()}};

        ClassDesc tools;
        tools.name        = "Tools";
        tools.isNamespace = true;
        tools.side        = Side::Server;
        tools.properties  = {{"version", TypeOf<std::string>(), "The version.", true, true, Side::Shared}};
        tools.functions   = {{"log", "Logs.", {param("level", TypeOf<int>(), "", true), param("values", Type::Custom("unknown[]", "any"), "What to log.", false, true)}, Type::Custom("Promise<void>", "nil"), "", true, Side::Shared}};

        ClassDesc screen;
        screen.name        = "Screen";
        screen.isNamespace = true;
        screen.side        = Side::Client;

        ApiCatalog catalog;
        for (const ClassDesc *desc : {&base, &square, &tools, &screen}) {
            catalog.Record(desc);
        }
        const std::string server = WriteLuaDefinitions(catalog, Side::Server);

        // After the header, each definition and then the globals of every state.
        const size_t start   = server.find("\n---A shape.");
        const size_t globals = server.find("\n---Calls a function once after a delay");
        EQUALS(server.starts_with("---@meta\n\n-- MafiaHub Framework: the Lua API of a server resource"), true);
        EQUALS(start != std::string::npos && globals != std::string::npos, true);
        EQUALS(server.substr(start, globals - start), R"(
---A shape.
---Of any kind.
---
---Not a global: a script is given the objects of this class and cannot create one.
---@class Shape
---@field sides integer [read-only] How many sides.
local Shape = {}

---A square.
---
---Also has the operators `number * Square`, `Square < Square`.
---@class Square: Shape
---@field size number
---@field owner Player? [read-only] Who owns it, or nil.
---@operator add(Square): Square
---@operator unm: Square
---@overload fun(size: number, end_?: string): Square
Square = {}

---[read-only] How many squares there are; nil is not nil, nullable stays.
---@type integer
Square.count = nil

---Makes a square.
---@param size number Side length.
---@param end_? string A label.
---@return Square
function Square.new(size, end_) end

---Grows it.
---In place.
---@param by number How much.
---@return self # This square.
function Square:grow(by) end

---Saves it.
---@async
---@return boolean # Whether it was saved.
function Square:save() end

---A square of size 1.
---@return Square
function Square.unit() end

---@class Tools
---@field version string [read-only] The version.
Tools = {}

---Logs.
---@async
---@param level? integer
---@param ... any What to log.
function Tools.log(level, ...) end
)");
        for (const FunctionDesc &global : LuaRuntimeGlobals()) {
            EQUALS(server.find("\nfunction " + global.name + "(") != std::string::npos, true);
        }
        EQUALS(LuaParity::CompileError(server), "");

        // A client has the members of its side, and the namespace of its side for that of the server.
        const std::string client = WriteLuaDefinitions(catalog, Side::Client);
        EQUALS(client.find("---@field shade integer Drawn shade.\n") != std::string::npos, true);
        EQUALS(client.find("function Square:draw() end\n") != std::string::npos, true);
        EQUALS(client.find("\nScreen = {}\n") != std::string::npos, true);
        EQUALS(client.find("owner") == std::string::npos && client.find("Square:save") == std::string::npos && client.find("Tools") == std::string::npos, true);
        EQUALS(LuaParity::CompileError(client), "");
    });

    IT("writes the definition file where the environment says, and nothing when it says nothing", {
        using namespace Framework::Scripting::Bind;
        const auto builtins              = ServerScriptingModule::LuaBuiltinDefinitions();
        const std::filesystem::path path = std::filesystem::temp_directory_path() / ("framework_lua_definitions_ut_" + std::to_string(std::random_device {}()) + ".d.lua");
        const char *const variable       = "FW_TEST_LUA_DEFINITIONS_PATH";
        const auto set                   = [variable](const std::string &value) {
#ifdef _WIN32
            _putenv_s(variable, value.c_str());
#else
            setenv(variable, value.c_str(), 1);
#endif
        };

        set("");
        EQUALS(ExportLuaDefinitionsFromEnvironment(builtins, Side::Server, variable), true);
        EQUALS(std::filesystem::exists(path), false);

        set(path.string());
        EQUALS(ExportLuaDefinitionsFromEnvironment(builtins, Side::Server, variable), true);
        std::stringstream written;
        written << std::ifstream(path, std::ios::binary).rdbuf();
        EQUALS(written.str() == LuaParity::Generate(builtins, Side::Server), true);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);

        // A file that cannot be written is reported.
        set((path / "below" / "a-file").string());
        EQUALS(ExportLuaDefinitionsFromEnvironment(builtins, Side::Server, variable), false);
        set("");
    });

    IT("fails the gate for a member Lua lacks and for an exemption that excuses nothing", {
        Bus bus;
        bus.server.files.Resource("lua", {{"main.lua", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("lua"), "");
        Framework::Scripting::LuaRuntime *runtime = bus.server.module.GetLuaEngine()->GetRuntime("lua");
        NEQUALS(runtime, nullptr);
        lua_State *L        = runtime->State();
        const auto builtins = ServerScriptingModule::LuaBuiltinDefinitions();

        const nlohmann::json catalog           = nlohmann::json::parse(R"({"symbols":[
            {"name":"Chat","kind":"globalObject","functions":[{"name":"sendToAll","static":false},{"name":"whisper","static":false}],"properties":[]},
            {"name":"Vector3","kind":"constructor","constructor":{},"functions":[{"name":"add","static":false},{"name":"zero","static":true},{"name":"zero","static":false},{"name":"spin","static":true}],
             "properties":[{"name":"x","readonly":true,"static":false},{"name":"length","readonly":false,"static":false},{"name":"mass","readonly":true,"static":false}]},
            {"name":"Entity","kind":"constructor","constructor":null,"functions":[],"properties":[]},
            {"name":"Ghost","kind":"constructor","functions":[],"properties":[]},
            {"name":"Shape","kind":"dataType","functions":[],"properties":[{"name":"sides","readonly":false,"static":false}]}
        ],"functions":[{"name":"setTimeout"},{"name":"setImmediate"}]})");
        const LuaParity::Exemptions exemptions = {{"Vector3.add", "excuses what Lua has"}, {"Vector3.spin", "excuses what Lua lacks"}, {"Nothing.at", "names nothing"}};
        LuaParity::Gate gate(L, Side::Server, builtins, exemptions);
        const LuaParity::Report report = gate.Check(catalog);

        EQUALS(LuaParity::Join(report.missing), "Chat.whisper (function), Entity.new (Lua constructs what JavaScript cannot), Ghost (global), Ghost (no class of this name is defined for Lua), Vector3.length (property with a setter), Vector3.mass (read-only property), "
                                                "Vector3.x (read-only property), Vector3.zero (method), setImmediate (global function)");
        EQUALS(LuaParity::Join(report.stale), "Nothing.at (names nothing the catalog documents), Vector3.add (Lua has it)");
        EQUALS(report.exempted, 1);
        EQUALS(report.dataTypes, 1);
        EQUALS(report.symbols, 4);

        // And the definition file: what it declares and the state lacks is named.
        const LuaParity::Declared declared = LuaParity::CheckDefinitions("---@class Chat\n---@field motd string [read-only]\nChat = {}\nfunction Chat.whisper(text) end\n"
                                                                         "---Also has the operators `number * Vector3`, `Vector3 < Vector3`, `#Vector3`.\n---@class Vector3\n---@field x number [read-only] X.\n---@field length number [read-only]\n"
                                                                         "---@operator add(Vector3): Vector3\n---@operator concat(string): string\n---@overload fun(): Vector3\nVector3 = {}\nVector3.count = nil\n"
                                                                         "function Vector3:spin() end\nfunction Vector3.zero() end\n---@class StateBag\n---@overload fun(): StateBag\nStateBag = {}\n---@class Entity\nlocal Entity = {}\nfunction setImmediate(callback) end\n",
            L, Side::Server, builtins);
        EQUALS(LuaParity::Join(declared.missing), "Chat.motd (constant), Chat.whisper (function), Entity (declared as no global, and is one), StateBag (callable class), StateBag (global), Vector3 __concat (operator), Vector3 __len (operator named in the description), "
                                                  "Vector3 __lt (operator named in the description), Vector3.count (static property), Vector3.x (read-only property), Vector3:spin (function), setImmediate (global function)");
        EQUALS(declared.operators, 5);
        EQUALS(declared.callable, 2);

        // And a builtin one language has alone.
        EQUALS(LuaParity::Join(LuaParity::OneSided(catalog, {&Framework::Scripting::Builtins::Vector3::Definition().Describe(), &Framework::Scripting::Builtins::Color::Definition().Describe(), &Framework::Scripting::Builtins::StateBag::Definition().Describe()}, Side::Server)),
            "Chat (JavaScript only), Color (Lua only), Entity (JavaScript only), Ghost (JavaScript only)");
        EQUALS(LuaParity::WithoutDefinition("---@meta\n\n---A.\n---@class A\nA = {}\n\n---f\nfunction A.f() end\n\n---@class B\nB = {}\n\nfunction B:g() end\n\nfunction after() end\n", "B"),
            "---@meta\n\n---A.\n---@class A\nA = {}\n\n---f\nfunction A.f() end\n\nfunction after() end\n");
        EQUALS(LuaParity::Join(LuaParity::UndeclaredGlobals(L, declared.globals)).find("Events") != std::string::npos, true);
    });
})
