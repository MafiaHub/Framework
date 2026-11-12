/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_builtins_core_ut.h"
#include "lua_resources_ut.h"
#include "lua_runtime_ut.h"
#include "nametag_ut.h"
#include "node_test_helpers.h"
#include "resource_manager_ut.h"
#include "text_label_ut.h"

#include "networking/replication/nametag_state.h"
#include "scripting/bind/call.h"
#include "scripting/bind/callable.h"
#include "scripting/builtins/builtins.h"
#include "scripting/builtins/entity.h"
#include "scripting/builtins/player.h"
#include "scripting/builtins/state_bag.h"
#include "scripting/builtins/text_label.h"
#include "scripting/lua/lua_engine.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/lua/lua_transfer.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/scripting_catalog.h"
#include "scripting/value_transfer.h"

#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Player and TextLabel, declared once for JavaScript and Lua: what a Lua script gets of them, held against what a
// JavaScript one gets, over one replication manager both languages write to.
namespace LuaBuiltinsPlayerTest {
    using LabelEntity = Framework::Networking::Replication::Entities::TextLabelEntity;

    constexpr const char *kLuaResource = "lua_side";
    constexpr const char *kJsResource  = "js_side";

    // A Lua engine whose runtimes get the value types, Entity, Player and TextLabel the way the server module
    // gives them, with one runtime of the resource. A client has no TextLabel.
    struct LuaSide final {
        Framework::Scripting::LuaEngine engine;

        explicit LuaSide(Framework::Scripting::Bind::Side side = Framework::Scripting::Bind::Side::Server, Framework::Scripting::ResourceManager *manager = nullptr): engine(LuaBuiltinsCoreTest::LuaSide::Options(side)) {
            Framework::Scripting::Bind::WatchRuntimes(engine);
            engine.AddRuntimeDisposingListener([](Framework::Scripting::LuaRuntime &runtime) {
                Framework::Scripting::Builtins::StateBag::DropDeadSubscriptions();
                Framework::Scripting::Bind::SetCallResourceManager(&runtime, nullptr);
            });
            engine.SetRuntimeSetupCallback([side, manager](Framework::Scripting::LuaRuntime &runtime) {
                Framework::Scripting::Bind::SetCallResourceManager(&runtime, manager);
                Framework::Scripting::Builtins::RegisterValueTypesLua(runtime.State(), side);
                Framework::Scripting::Builtins::Entity::RegisterLua(runtime);
                Framework::Scripting::Builtins::Player::RegisterLua(runtime);
                if (side == Framework::Scripting::Bind::Side::Server) {
                    Framework::Scripting::Builtins::TextLabel::RegisterLua(runtime);
                }
            });
            ready = engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE && engine.CreateResourceRuntime(kLuaResource);
        }

        Framework::Scripting::LuaRuntime &Runtime() {
            return *engine.GetRuntime(kLuaResource);
        }

        // Empty when the chunk ran, the first line of its error otherwise.
        std::string Run(const std::string &source) {
            return LuaRuntimeTest::FirstLine(LuaRuntimeTest::Run(Runtime(), source));
        }

        std::string Eval(const std::string &expression) {
            return LuaRuntimeTest::Eval(Runtime(), expression);
        }

        // What a Lua error says after the position of the script that raised it, or "no error".
        std::string Raised(const std::string &source) {
            const std::string error = Run(source);
            const size_t at         = error.find(": ");
            return error.empty() ? "no error" : error.substr(at == std::string::npos ? 0 : at + 2);
        }

        bool ready = false;
    };

    // A JavaScript runtime with the value types, Entity, Player and TextLabel registered through their Register.
    struct JsSide final {
        LuaBuiltinsCoreTest::JsRuntime js;
        Framework::Scripting::ResourceManager manager;

        explicit JsSide(bool isClient = false): manager(&js.engine) {
            static int count = 0;
            Framework::Scripting::SetScriptingCatalog(js.Isolate(), "lua_builtins_player/" + std::to_string(++count));
            Framework::Scripting::SetScriptingEnvironment(js.Isolate(), isClient);
            js.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                Framework::Scripting::Builtins::RegisterValueTypes(isolate, context->Global());
                Framework::Scripting::Builtins::Entity::Register(isolate, context->Global());
                Framework::Scripting::Builtins::Player::Register(isolate, context->Global());
                if (!isClient) {
                    Framework::Scripting::Builtins::TextLabel::Register(isolate, context->Global());
                }
                Framework::Scripting::Bind::SetCallResourceManager(isolate, &manager);
            });
            manager.SetCurrentResourceContext(kJsResource);
        }

        std::string Eval(const std::string &source) {
            return js.Eval(source);
        }

        // "<name>: <message>" of what `source` throws, or "no error".
        std::string Thrown(const std::string &source) {
            return js.Eval("(() => { try { " + source + "; } catch (e) { return e.name + ': ' + e.message; } return 'no error'; })()");
        }
    };

    // The world both languages write to: an entity nobody owns, as `e`, and two players, each the avatar of a
    // connection, the first of them as `p`.
    struct Stage final {
        TextLabelWorld world;
        Framework::Networking::Replication::NetworkEntity *entity;
        Framework::Networking::Replication::NetworkEntity *players[2];
        std::string e;
        std::string a;
        std::string b;
        JsSide js;
        LuaSide lua;

        Stage(): entity(world.Manager()->CreateEntity(world.targetType)), lua(Framework::Scripting::Bind::Side::Server, &js.manager) {
            for (int i = 0; i < 2; ++i) {
                players[i]            = world.Manager()->CreateEntity(world.targetType);
                players[i]->ownerGUID = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(71 + i));
                world.Manager()->SetViewer(players[i]->ownerGUID, players[i]);
            }
            e = std::to_string(entity->GetNetworkID());
            a = std::to_string(players[0]->GetNetworkID());
            b = std::to_string(players[1]->GetNetworkID());
            lua.Run("e = Entity.new(" + e + ") p = Player.new(" + a + ")");
            js.Eval("globalThis.e = new Entity(" + e + "); globalThis.p = new Player(" + a + "); 0");
        }

        // The label the Lua expression `handle` names.
        LabelEntity *Label(const std::string &handle) {
            return dynamic_cast<LabelEntity *>(world.Manager()->GetEntityByNetworkID(std::stoull(lua.Eval("math.tointeger(" + handle + ".id)"))));
        }

        size_t LabelCount() {
            size_t count = 0;
            world.Manager()->ForEach<LabelEntity>([&count](LabelEntity *) {
                ++count;
            });
            return count;
        }
    };

    // A Lua table as text, its keys sorted, so that two languages can be held against each other:
    // {a=1,b={2,3}}. A float with an integral value is spelled as the integer.
    constexpr const char *kLuaShow = R"(
        function show(value)
            if type(value) == 'number' then
                return math.tointeger(value) and tostring(math.tointeger(value)) or string.format('%.4g', value)
            end
            if type(value) ~= 'table' then
                return tostring(value)
            end
            local parts = {}
            if #value > 0 or next(value) == nil then
                for _, item in ipairs(value) do parts[#parts + 1] = show(item) end
                return '[' .. table.concat(parts, ',') .. ']'
            end
            local keys = {}
            for key in pairs(value) do keys[#keys + 1] = key end
            table.sort(keys)
            for _, key in ipairs(keys) do parts[#parts + 1] = key .. '=' .. show(value[key]) end
            return '{' .. table.concat(parts, ',') .. '}'
        end
    )";

    // The same spelling of a JavaScript value. null and undefined are left out of an object, as Lua has neither.
    constexpr const char *kJsShow = R"(
        globalThis.show = (value) => {
            if (typeof value === 'number') return Number.isInteger(value) ? String(value) : value.toPrecision(4).replace(/\.?0+$/, '');
            if (Array.isArray(value)) return '[' + value.map(show).join(',') + ']';
            if (value === null || typeof value !== 'object') return String(value);
            return '{' + Object.keys(value).sort().filter((key) => value[key] != null).map((key) => key + '=' + show(value[key])).join(',') + '}';
        }; 0
    )";

    // A handle a project wrote by hand with v8pp: it has an id and does not derive from Entity.
    struct ForeignHandle final {
        explicit ForeignHandle(double id): id(id) {}

        double GetId() const {
            return id;
        }
        double GetX() const {
            return 1.0;
        }
        double GetY() const {
            return 2.0;
        }
        double GetZ() const {
            return 3.0;
        }

        double id;

        // Publishes the class as the global ForeignHandle. v8pp keeps it for the isolate until Builtins::UnregisterAll.
        static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context) {
            v8pp::class_<ForeignHandle> cls(isolate);
            cls.ctor<double>().property("id", &ForeignHandle::GetId).property("x", &ForeignHandle::GetX).property("y", &ForeignHandle::GetY).property("z", &ForeignHandle::GetZ);
            context->Global()->Set(context, v8pp::to_v8(isolate, "ForeignHandle"), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
        }
    };

    // Resources of both languages under one manager, each runtime with TextLabel, over a world whose replication
    // manager tells the resource manager of its entities, as the server instance wires it.
    struct Owners final {
        TextLabelWorld world;
        LuaResourcesTest::Rig rig;

        Owners() {
            TestManagerHelper::RegisterEvents(rig.node, rig.manager, [this](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                Framework::Scripting::Builtins::RegisterValueTypes(isolate, context->Global());
                Framework::Scripting::Builtins::TextLabel::Register(isolate, context->Global());
                Framework::Scripting::Bind::SetCallResourceManager(isolate, &rig.manager);
            });
            rig.lua.SetRuntimeSetupCallback([this](Framework::Scripting::LuaRuntime &runtime) {
                Framework::Scripting::Bind::SetCallResourceManager(&runtime, &rig.manager);
                Framework::Scripting::Builtins::RegisterValueTypesLua(runtime.State(), Framework::Scripting::Bind::Side::Server);
                Framework::Scripting::Builtins::TextLabel::RegisterLua(runtime);
            });
            rig.lua.AddRuntimeDisposingListener([](Framework::Scripting::LuaRuntime &runtime) {
                Framework::Scripting::Bind::SetCallResourceManager(&runtime, nullptr);
            });
            world.Manager()->SetOnEntityCreated([this](uint64_t networkId) {
                rig.manager.OnEntityCreated(networkId);
            });
            world.Manager()->SetOnEntityDestroyed([this](uint64_t networkId) {
                rig.manager.OnEntityDestroyed(networkId);
            });
        }

        ~Owners() {
            world.Manager()->SetOnEntityCreated({});
            world.Manager()->SetOnEntityDestroyed({});
        }

        // What `expression` gives in the JavaScript runtime of the resource.
        std::string Js(const std::string &resource, const std::string &expression) {
            Framework::Scripting::NodeRuntime *runtime = rig.node.GetResourceRuntime(resource);
            return runtime != nullptr ? NodeTest::Eval(*runtime, expression) : "<no runtime>";
        }

        // Ticks both engines until the JavaScript `expression` is "true" in the resource, or time runs out.
        bool Until(const std::string &resource, const std::string &expression) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < deadline) {
                rig.node.Tick();
                rig.Advance(5);
                if (Js(resource, expression) == "true") {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        }

        bool Lives(const std::string &id) {
            return world.Manager()->GetEntityByNetworkID(std::stoull(id)) != nullptr;
        }
    };
} // namespace LuaBuiltinsPlayerTest

MODULE(lua_builtins_player, {
    // ==================== Player ====================

    IT("gives Lua a Player that is an Entity, as JavaScript has it", {
        LuaBuiltinsPlayerTest::Stage stage;
        EQUALS(stage.lua.ready, true);

        EQUALS(stage.lua.Eval("type(Player) .. ' ' .. p:toString() .. ' ' .. tostring(p) .. ' ' .. p.id"), "table Player{ id: " + stage.a + " } Player{ id: " + stage.a + " } " + stage.a);
        EQUALS(stage.js.Eval("[p instanceof Entity, p.toString(), String(p.id)].join(' ')"), "true Player{ id: " + stage.a + " } " + stage.a);

        // What an Entity has, a Player has.
        EQUALS(stage.lua.Run("p.position = Vector3.new(7, 8, 9) p:setVirtualWorld(2)"), "");
        EQUALS(stage.players[0]->position.z == 9.0f, true);
        EQUALS(stage.lua.Eval("p.position:toString() .. ' ' .. p.virtualWorld"), "Vector3(7, 8, 9) 2");
        EQUALS(stage.lua.Eval("p.state:set('job', 'smith')"), "true");
        EQUALS(stage.js.Eval("p.state.get('job')"), "smith");

        // And passes where an Entity is expected.
        EQUALS(stage.lua.Run("e:setVisibleTo(p)"), "");
        EQUALS(stage.entity->streaming.targetGUID == stage.players[0]->ownerGUID, true);
        EQUALS(stage.lua.Run("e:setVisibleTo(nil)"), "");

        // A handle is of an entity that is there.
        EQUALS(stage.lua.Raised("Player.new(99999)"), "Entity handle '99999' is not valid!");
        EQUALS(stage.js.Thrown("new Player(99999)"), "Error: Entity handle '99999' is not valid!");
        EQUALS(stage.lua.Raised("Player(99999)"), "Entity handle '99999' is not valid!");
    });

    IT("reads a connection that is not there as JavaScript does", {
        LuaBuiltinsPlayerTest::Stage stage;
        // No network peer: the identity is empty and the calls do nothing.
        EQUALS(stage.lua.Eval("show == nil and table.concat({ p.steamId, p.epicId, p.discordId, p.discordName, p.hardwareId, p.ip, p:getIP() }, '|') .. ' ' .. p.ping"), "|||||| -1");
        EQUALS(stage.js.Eval("[p.steamId, p.epicId, p.discordId, p.discordName, p.hardwareId, p.ip, p.getIP()].join('|') + ' ' + p.ping"), "|||||| -1");
        EQUALS(stage.lua.Run("p:kick('bye') p:emit('hello', '{}')"), "");
        EQUALS(stage.js.Thrown("p.kick('bye'); p.emit('hello', '{}')"), "no error");

        // The properties of a connection are read, not written.
        EQUALS(LuaRuntimeTest::Contains(stage.lua.Run("p.ping = 5"), "ping"), true);
    });

    IT("takes the arguments of a Player method as JavaScript does", {
        LuaBuiltinsPlayerTest::Stage stage;
        // The reason is documented as optional and required by both languages.
        EQUALS(LuaRuntimeTest::Contains(stage.lua.Run("p:kick()"), "wrong number of arguments to 'Player:kick'"), true);
        EQUALS(LuaRuntimeTest::Contains(stage.js.Thrown("p.kick()"), "Argument count does not match function definition"), true);
        EQUALS(LuaRuntimeTest::Contains(stage.lua.Run("p:setNametagColor('red')"), "bad argument #1 to 'Player:setNametagColor'"), true);
        EQUALS(stage.js.Thrown("p.setNametagColor('red')") != "no error", true);
        EQUALS(LuaRuntimeTest::Contains(stage.lua.Run("p:setNametagVisible(1)"), "bad argument #1 to 'Player:setNametagVisible'"), true);
    });

    IT("writes the nametag of a player from Lua and reads it in both languages", {
        NametagRig rig;
        LuaBuiltinsPlayerTest::JsSide js;
        LuaBuiltinsPlayerTest::LuaSide lua(Framework::Scripting::Bind::Side::Server, &js.manager);
        EQUALS(lua.Run("p = Player.new(1)"), "");
        js.Eval("globalThis.p = new Player(1); 0");

        EQUALS(lua.Eval("tostring(p:isNametagVisible()) .. tostring(p:isNametagHealthVisible()) .. '[' .. p:getNametagText() .. ']' .. tostring(p:getNametagColor() == 0xFFFFFFFF)"), "truetrue[]true");

        EQUALS(lua.Run("p:setNametagVisible(false)"), "");
        EQUALS(rig.avatar.nametag.Has(Framework::Networking::Replication::NametagComponent::Name), false);
        EQUALS(rig.avatar.nametag.Has(Framework::Networking::Replication::NametagComponent::Health), true);
        EQUALS(lua.Eval("tostring(p:isNametagVisible()) .. tostring(p:isNametagHealthVisible())"), "falsetrue");
        EQUALS(js.Eval("String(p.isNametagVisible()) + String(p.isNametagHealthVisible())"), "falsetrue");

        EQUALS(lua.Run("p:setNametagHealthVisible(false) p:setNametagVisible(true)"), "");
        EQUALS(rig.avatar.nametag.Has(Framework::Networking::Replication::NametagComponent::Name), true);
        EQUALS(rig.avatar.nametag.Has(Framework::Networking::Replication::NametagComponent::Health), false);

        EQUALS(lua.Run("p:setNametagText('Ministry') p:setNametagColor(0xFF33CC99)"), "");
        STREQUALS(rig.avatar.nametag.text.c_str(), "Ministry");
        EQUALS(int64_t(rig.avatar.nametag.color), int64_t(0xFF33CC99));
        EQUALS(js.Eval("p.getNametagText() + ' ' + p.getNametagColor().toString(16)"), "Ministry ff33cc99");

        // What JavaScript writes, Lua reads.
        js.Eval("p.setNametagText('Chef'); p.setNametagColor(0x80112233); 0");
        EQUALS(lua.Eval("p:getNametagText() .. ' ' .. string.format('%x', p:getNametagColor())"), "Chef 80112233");

        // The text is cut to kMaxTextBytes without splitting a character, and replicates.
        EQUALS(lua.Run("p:setNametagText(string.rep('a', 63) .. '\\xC3\\xAD' .. 'tail')"), "");
        EQUALS(rig.avatar.nametag.text == std::string(Framework::Networking::Replication::NametagState::kMaxTextBytes - 1, 'a'), true);
        EQUALS(lua.Run("p:setNametagText('')"), "");
        EQUALS(rig.avatar.nametag.text.empty(), true);
        EQUALS(lua.Run("p:setNametagVisible(false)"), "");
        rig.OwnerTick();
        rig.ServerTick();
        EQUALS(rig.viewer.nametag.Has(Framework::Networking::Replication::NametagComponent::Name), false);
    });

    IT("leaves what the server decides out of a client runtime, in both languages", {
        TextLabelWorld world;
        const std::string id = std::to_string(world.Manager()->CreateEntity(world.targetType)->GetNetworkID());
        LuaBuiltinsPlayerTest::LuaSide lua(Framework::Scripting::Bind::Side::Client);
        LuaBuiltinsPlayerTest::JsSide js(/*isClient*/ true);
        EQUALS(lua.Run("p = Player.new(" + id + ")"), "");
        js.Eval("globalThis.p = new Player(" + id + "); 0");

        EQUALS(lua.Eval("type(p.kick) .. type(p.emit) .. type(p.getIP) .. type(p.setNametagVisible) .. type(p.setNametagHealthVisible) .. type(p.setNametagText) .. type(p.setNametagColor)"), "nilnilnilnilnilnilnil");
        EQUALS(js.Eval("[p.kick, p.emit, p.getIP, p.setNametagVisible, p.setNametagHealthVisible, p.setNametagText, p.setNametagColor].map((member) => typeof member).join('')"), "undefinedundefinedundefinedundefinedundefinedundefinedundefined");
        EQUALS(lua.Eval("type(p.steamId) .. type(p.epicId) .. type(p.discordId) .. type(p.discordName) .. type(p.hardwareId) .. type(p.ping) .. type(p.ip)"), "nilnilnilnilnilnilnil");
        EQUALS(js.Eval("['steamId', 'epicId', 'discordId', 'discordName', 'hardwareId', 'ping', 'ip'].filter((name) => name in p).length"), "0");

        // What reads the replica stays, with what an Entity has on a client.
        EQUALS(lua.Eval("type(p.isNametagVisible) .. type(p.isNametagHealthVisible) .. type(p.getNametagText) .. type(p.getNametagColor) .. type(p.toString)"), "functionfunctionfunctionfunctionfunction");
        EQUALS(js.Eval("[p.isNametagVisible, p.isNametagHealthVisible, p.getNametagText, p.getNametagColor].map((member) => typeof member).join('')"), "functionfunctionfunctionfunction");
        EQUALS(lua.Eval("type(p.setVirtualWorld) .. ' ' .. p.id .. ' ' .. tostring(p:isNametagVisible())"), "nil " + id + " false");
        EQUALS(lua.Eval("type(TextLabel)"), "nil");
    });

    // ==================== TextLabel: creating ====================

    IT("checks the arguments and the options of TextLabel.create before it needs replication, in both languages", {
        Framework::CoreModules::SetReplication(nullptr);
        LuaBuiltinsPlayerTest::JsSide js;
        LuaBuiltinsPlayerTest::LuaSide lua(Framework::Scripting::Bind::Side::Server, &js.manager);
        EQUALS(lua.ready, true);

        EQUALS(lua.Raised("TextLabel.create(0, 0, 0)"), "TextLabel.create: expected (x, y, z, text, fontSize? | options?, drawDistance?, virtualWorld?)");
        EQUALS(lua.Raised("TextLabel.create('0', 0, 0, 'x')"), "TextLabel.create: expected (x, y, z, text, fontSize? | options?, drawDistance?, virtualWorld?)");
        EQUALS(lua.Raised("TextLabel.create(0, 0, 0, 5)"), "TextLabel.create: expected (x, y, z, text, fontSize? | options?, drawDistance?, virtualWorld?)");
        EQUALS(js.Thrown("TextLabel.create(0, 0, 0)"), "TypeError: TextLabel.create: expected (x, y, z, text, fontSize? | options?, drawDistance?, virtualWorld?)");

        // The same option refused in each language, in the same words.
        const std::pair<const char *, const char *> rejected[] {
            {"{ colour = 1 }", "{ colour: 1 }"},
            {"{ fontSize = 0 }", "{ fontSize: 0 }"},
            {"{ fontSize = '12' }", "{ fontSize: '12' }"},
            {"{ color = 'red' }", "{ color: 'red' }"},
            {"{ color = '1234567' }", "{ color: '1234567' }"},
            {"{ color = 1.5 }", "{ color: 1.5 }"},
            {"{ color = Vector3.new(1, 2, 3) }", "{ color: new Vector3(1, 2, 3) }"},
            {"{ outline = { color = 'x00ff00' } }", "{ outline: { color: 'x00ff00' } }"},
            {"{ outline = { width = 9 } }", "{ outline: { width: 9 } }"},
            {"{ outline = 3 }", "{ outline: 3 }"},
            {"{ shadow = 'soft' }", "{ shadow: 'soft' }"},
            {"{ shadow = { blur = 99 } }", "{ shadow: { blur: 99 } }"},
            {"{ background = { 1, 2 } }", "{ background: [1, 2] }"},
            {"{ background = { padding = 65 } }", "{ background: { padding: 65 } }"},
            {"{ sizeMode = 'huge' }", "{ sizeMode: 'huge' }"},
            {"{ style = 7 }", "{ style: 7 }"},
            {"{ style = 'bold' }", "{ style: 'bold' }"},
            {"{ align = 1 }", "{ align: 1 }"},
            {"{ anchor = 'middle' }", "{ anchor: 'middle' }"},
            {"{ attachTo = -1 }", "{ attachTo: -1 }"},
            {"{ attachTo = 'p' }", "{ attachTo: 'p' }"},
            {"{ position = 'here' }", "{ position: 'here' }"},
            {"{ position = { 1, 2 } }", "{ position: [1, 2] }"},
            {"{ offset = { x = 1, y = 2 } }", "{ offset: { x: 1, y: 2 } }"},
            {"{ drawDistance = 0 }", "{ drawDistance: 0 }"},
            {"{ fadeDistance = -1 }", "{ fadeDistance: -1 }"},
            {"{ lineSpacing = 9 }", "{ lineSpacing: 9 }"},
            {"{ maxWidth = 5000 }", "{ maxWidth: 5000 }"},
            {"{ opacity = 2 }", "{ opacity: 2 }"},
            {"{ occlusion = 'yes' }", "{ occlusion: 'yes' }"},
            {"{ markup = 1 }", "{ markup: 1 }"},
            {"{ text = 5 }", "{ text: 5 }"},
            {"{ virtualWorld = 1.5 }", "{ virtualWorld: 1.5 }"},
            {"{ minSize = 20, maxSize = 10 }", "{ minSize: 20, maxSize: 10 }"},
            {"{ showTo = 5 }", "{ showTo: 5 }"},
            {"{ hideFrom = 'all' }", "{ hideFrom: 'all' }"},
            {"{ hideForTarget = 1 }", "{ hideForTarget: 1 }"},
        };
        for (const auto &[luaOptions, jsOptions] : rejected) {
            const std::string raised = lua.Raised(std::string("TextLabel.create(0, 0, 0, 'x', ") + luaOptions + ")");
            EQUALS(raised.starts_with("TextLabel: "), true);
            EQUALS(js.Thrown(std::string("TextLabel.create(0, 0, 0, 'x', ") + jsOptions + ")"), "TypeError: " + raised);
        }
        EQUALS(lua.Raised("TextLabel.create(0, 0, 0, 'x', { colour = 1 })"), "TextLabel: unknown option 'colour'");
        EQUALS(lua.Raised("TextLabel.create(0, 0, 0, 'x', { fontSize = 0 })"), "TextLabel: fontSize must be a number from 1 to 512");
        EQUALS(lua.Raised("TextLabel.create(0, 0, 0, 'x', { color = 'red' })"), "TextLabel: color must be 0xAARRGGBB, \"#RRGGBB[AA]\" or a Color");
        EQUALS(lua.Raised("TextLabel.create(0, 0, 0, 'x', { minSize = 20, maxSize = 10 })"), "TextLabel: minSize must not exceed maxSize");

        // An element of a list is counted from 1 in Lua and from 0 in JavaScript.
        EQUALS(lua.Raised("TextLabel.create(0, 0, 0, 'x', { showTo = { 'a' } })"), "TextLabel: showTo[1] must be an entity or its network id");
        EQUALS(js.Thrown("TextLabel.create(0, 0, 0, 'x', { showTo: ['a'] })"), "TypeError: TextLabel: showTo[0] must be an entity or its network id");
        EQUALS(lua.Raised("TextLabel.create(0, 0, 0, 'x', { hideFrom = { 1, false } })"), "TextLabel: hideFrom[2] must be an entity or its network id");
        EQUALS(js.Thrown("TextLabel.create(0, 0, 0, 'x', { hideFrom: [null] })"), "TypeError: TextLabel: hideFrom[0] must be a Player or their network id");
        EQUALS(js.Thrown("TextLabel.create(0, 0, 0, 'x', { hideFrom: [, 1] })"), "TypeError: TextLabel: hideFrom[0] must be a Player or their network id");

        // Options that pass get as far as the replication there is not.
        EQUALS(lua.Raised("TextLabel.create(0, 0, 0, 'x', { fontSize = 20, opacity = 0.5, outline = { width = 2 }, shadow = { blur = 4 }, background = true, sizeMode = 'world', style = 'box', attachTo = 5, offset = { 0, 0, 10 }, markup = true, "
                          "align = 'left', anchor = 'bottom', color = Color.new(1, 0, 0, 1), showTo = {}, hideFrom = false, hideForTarget = true })"),
            "TextLabel.create: replication unavailable");
        EQUALS(js.Thrown("TextLabel.create(0, 0, 0, 'x', { fontSize: 20, opacity: 0.5, outline: { width: 2 }, shadow: { blur: 4 }, background: true, sizeMode: 'world', style: 'box', attachTo: 5, offset: [0, 0, 10], markup: true, "
                         "align: 'left', anchor: 'bottom', color: new Color(1, 0, 0, 1), showTo: [], hideFrom: null, hideForTarget: true })"),
            "Error: TextLabel.create: replication unavailable");
        EQUALS(lua.Raised("TextLabel.create(0, 0, 0, 'x', 18, 40)"), "TextLabel.create: replication unavailable");
    });

    IT("creates a label from Lua, in the older form and with options", {
        LuaBuiltinsPlayerTest::Stage stage;
        EQUALS(stage.lua.Run("l = TextLabel.create(1, 2, 3, 'hello')"), "");
        auto *label = stage.Label("l");
        EQUALS(label != nullptr, true);
        EQUALS(label->position == glm::vec3(1.0f, 2.0f, 3.0f), true);
        STREQUALS(label->text.c_str(), "hello");
        EQUALS(label->drawDistance == Framework::Scripting::Builtins::TextLabel::kDefaultMetres, true);
        EQUALS(label->streaming.range == Framework::Scripting::Builtins::TextLabel::kDefaultMetres, true);
        EQUALS(stage.lua.Eval("l:toString() .. ' ' .. tostring(l)"), "TextLabel{ id: " + std::to_string(label->GetNetworkID()) + ", text: \"hello\" } TextLabel{ id: " + std::to_string(label->GetNetworkID()) + ", text: \"hello\" }");
        EQUALS(stage.lua.Eval("l.text .. ' ' .. string.format('%x', l.color) .. ' ' .. l.fontSize .. ' ' .. l.drawDistance .. ' ' .. l.fadeDistance .. ' ' .. l.style .. ' ' .. l.virtualWorld"), "hello ffffffff 16.0 30.0 0.0 1 0");

        // The older form: font size, draw distance, virtual world.
        EQUALS(stage.lua.Run("old = TextLabel.create(0, 0, 0, 'old', 18, 40, 3)"), "");
        EQUALS(stage.lua.Eval("old.fontSize .. ' ' .. old.drawDistance .. ' ' .. old.virtualWorld"), "18.0 40.0 3");
        EQUALS(stage.js.Eval("(() => { const old = TextLabel.create(0, 0, 0, 'old', 18, 40, 3); return [old.fontSize, old.drawDistance, old.virtualWorld].join(' '); })()"), "18 40 3");

        // With options, the colour as a number, as text and as a Color.
        EQUALS(stage.lua.Run("made = TextLabel.create(0, 0, 0, 'x', { fontSize = 20, color = 0x80112233, virtualWorld = 4, drawDistance = 50, text = 'from options' })"), "");
        auto *made = stage.Label("made");
        STREQUALS(made->text.c_str(), "from options");
        EQUALS(int64_t(made->color), int64_t(0x80112233));
        EQUALS(made->fontSize == 20.0f && made->drawDistance == 50.0f && made->GetVirtualWorld() == 4, true);
        EQUALS(stage.lua.Eval("string.format('%x', TextLabel.create(0, 0, 0, 'x', { color = '#11223380' }).color)"), "80112233");
        EQUALS(stage.lua.Eval("string.format('%x', TextLabel.create(0, 0, 0, 'x', { color = Color.fromRGB(17, 34, 51, 255) }).color)"), "ff112233");
        EQUALS(stage.lua.Eval("string.format('%x', TextLabel.create(0, 0, 0, 'x', { color = -1 }).color)"), "ffffffff");

        // A handle of a label that exists, by its id, in both languages.
        EQUALS(stage.lua.Eval("TextLabel.new(l.id).text .. ' ' .. TextLabel(l.id).text"), "hello hello");
        EQUALS(stage.js.Eval("new TextLabel(" + std::to_string(label->GetNetworkID()) + ").text"), "hello");
    });

    IT("refuses a label past the limit of the server", {
        LuaBuiltinsPlayerTest::Stage stage;
        EQUALS(stage.lua.Run("for i = 1, " + std::to_string(Framework::Scripting::Builtins::TextLabel::kMaxLabels) + " do TextLabel.create(0, 0, 0, 'x') end"), "");
        EQUALS(stage.LabelCount(), size_t(Framework::Scripting::Builtins::TextLabel::kMaxLabels));
        EQUALS(stage.lua.Raised("TextLabel.create(0, 0, 0, 'x')"), "TextLabel.create: label limit reached");
        EQUALS(stage.js.Thrown("TextLabel.create(0, 0, 0, 'x')"), "Error: TextLabel.create: label limit reached");
        EQUALS(stage.LabelCount(), size_t(Framework::Scripting::Builtins::TextLabel::kMaxLabels));
    });

    // ==================== TextLabel: changing ====================

    IT("changes a label through its setters from Lua", {
        LuaBuiltinsPlayerTest::Stage stage;
        EQUALS(stage.lua.Run("l = TextLabel.create(0, 0, 0, 'x')"), "");
        auto *label = stage.Label("l");

        EQUALS(stage.lua.Run("l:setText('new') l:setFontSize(1000) l:setDrawDistance(55) l:setFadeDistance(-3) l:setStyle(3)"), "");
        STREQUALS(label->text.c_str(), "new");
        EQUALS(label->fontSize == Framework::Scripting::Builtins::TextLabel::kMaxFontSize && label->drawDistance == 55.0f && label->streaming.range == 55.0f && label->fadeDistance == 0.0f, true);
        EQUALS(stage.lua.Eval("l.style .. ' ' .. l:getOptions().style"), "3 box");
        EQUALS(stage.lua.Run("l:setStyle(9)"), "");
        EQUALS(stage.lua.Eval("l.style"), "3");
        EQUALS(stage.lua.Run("l:setText(string.rep('a', 2000))"), "");
        EQUALS(label->text.size(), Framework::Scripting::Builtins::TextLabel::kMaxTextBytes);

        // setColor takes a Color, or the bytes of one, alpha last and opaque when left out.
        EQUALS(stage.lua.Run("l:setColor(Color.fromRGB(17, 34, 51, 128))"), "");
        EQUALS(int64_t(label->color), int64_t(0x80112233));
        EQUALS(stage.lua.Run("l:setColor(1, 2, 3)"), "");
        EQUALS(int64_t(label->color), int64_t(0xFF010203));
        EQUALS(stage.lua.Run("l:setColor(300, -5, 3, 4)"), "");
        EQUALS(int64_t(label->color), int64_t(0x04FF0003));
        EQUALS(stage.js.Eval("(() => { const l = TextLabel.create(0, 0, 0, 'x'); l.setColor(300, -5, 3, 4); const bytes = l.color; l.setColor(new Color(1, 0, 0, 1)); l.setColor(); return bytes.toString(16) + ' ' + l.color.toString(16); })()"), "4ff0003 ff000000");
        EQUALS(stage.lua.Run("l:setColor()"), "");
        EQUALS(int64_t(label->color), int64_t(0xFF000000));

        // What an Entity has, a label has.
        EQUALS(stage.lua.Run("l.position = Vector3.new(4, 5, 6) l:setVirtualWorld(7)"), "");
        EQUALS(label->position == glm::vec3(4.0f, 5.0f, 6.0f) && label->GetVirtualWorld() == 7, true);

        // A destroyed label is a stale receiver: every member is quiet about it.
        const std::string id = stage.lua.Eval("math.tointeger(l.id)");
        EQUALS(stage.lua.Run("l:destroy()"), "");
        EQUALS(stage.world.Manager()->GetEntityByNetworkID(std::stoull(id)) == nullptr, true);
        EQUALS(stage.lua.Run("l:destroy() l:setText('x') l:setColor(1, 2, 3) l:update({ fontSize = 20 }) l:update(5) l:attachTo(e) l:detach() l:setVisibleFor(p, true) l:setVisibleFor()"), "");
        EQUALS(stage.lua.Eval("tostring(l:getOptions()) .. tostring(l:getAudience()) .. '[' .. l.text .. ']' .. l.fontSize"), "nilnil[]0.0");
        EQUALS(stage.js.Eval("(() => { const l = TextLabel.create(0, 0, 0, 'x'); l.destroy(); l.update(5); l.setVisibleFor(); l.attachTo(); return String(l.getOptions()) + String(l.getAudience()); })()"), "nullnull");
    });

    IT("reads a label back as the options update() takes, the same in both languages", {
        LuaBuiltinsPlayerTest::Stage stage;
        EQUALS(stage.lua.Run(LuaBuiltinsPlayerTest::kLuaShow), "");
        stage.js.Eval(LuaBuiltinsPlayerTest::kJsShow);

        EQUALS(stage.lua.Run("l = TextLabel.create(1, 2, 3, 'hello')"), "");
        const std::string id       = stage.lua.Eval("math.tointeger(l.id)");
        const std::string defaults = stage.lua.Eval("show(l:getOptions())");
        EQUALS(defaults, "{align=center,anchor=center,background=false,color=4294967295,drawDistance=30,fadeDistance=0,fontSize=16,lineSpacing=1,markup=false,maxSize=0,maxWidth=0,minSize=0,occlusion=true,offset={x=0,y=0,z=0},opacity=1,outline=false,"
                         "position={x=1,y=2,z=3},shadow={blur=0,color=4278190080,offsetX=1,offsetY=1},sizeMode=screen,style=shadow,text=hello,virtualWorld=0}");
        EQUALS(stage.js.Eval("show(new TextLabel(" + id + ").getOptions())"), defaults);
        // null in JavaScript, where Lua has no key.
        EQUALS(stage.js.Eval("String(new TextLabel(" + id + ").getOptions().attachTo)"), "null");
        EQUALS(stage.lua.Eval("tostring(l:getOptions().attachTo)"), "nil");
        // A colour and an id are integers in Lua.
        EQUALS(stage.lua.Eval("math.type(l:getOptions().color) .. math.type(l:getOptions().virtualWorld) .. math.type(l:getOptions().fontSize)"), "integerintegerfloat");

        EQUALS(stage.lua.Run("l:update({ text = 'a{b}c', markup = true, position = Vector3.new(4, 5, 6), fontSize = 24, sizeMode = 'world', minSize = 4, maxSize = 40, color = '#102030', opacity = 0.5, style = 'box', "
                             "outline = { width = 2, color = 0xFF00FF00 }, shadow = false, background = { padding = 8 }, align = 'left', anchor = 'bottom', lineSpacing = 1.5, maxWidth = 200, drawDistance = 80, fadeDistance = 60, "
                             "occlusion = false, virtualWorld = 9, attachTo = p, offset = { 0, 0, 2 } })"),
            "");
        const std::string changed = stage.lua.Eval("show(l:getOptions())");
        EQUALS(changed, "{align=left,anchor=bottom,attachTo=" + stage.a
                            + ",background={color=2566914048,padding=8,radius=4},color=4279246896,drawDistance=80,fadeDistance=60,fontSize=24,lineSpacing=1.5,markup=true,maxSize=40,maxWidth=200,minSize=4,occlusion=false,offset={x=0,y=0,z=2},opacity=0.5,"
                              "outline={color=4278255360,width=2},position={x=4,y=5,z=6},shadow=false,sizeMode=world,style=box,text=a{b}c,virtualWorld=9}");
        EQUALS(stage.js.Eval("show(new TextLabel(" + id + ").getOptions())"), changed);

        // One label's look seeds another's, across the languages too.
        EQUALS(stage.lua.Run("copy = TextLabel.create(0, 0, 0, 'copy') copy:update(l:getOptions())"), "");
        EQUALS(stage.lua.Eval("show(copy:getOptions())"), changed);
        EQUALS(stage.js.Eval("(() => { const copy = TextLabel.create(0, 0, 0, 'copy'); copy.update(new TextLabel(" + id + ").getOptions()); return show(copy.getOptions()); })()"), changed);
    });

    IT("changes nothing of a label when one option of an update is refused", {
        LuaBuiltinsPlayerTest::Stage stage;
        EQUALS(stage.lua.Run(LuaBuiltinsPlayerTest::kLuaShow), "");
        EQUALS(stage.lua.Run("l = TextLabel.create(0, 0, 0, 'x', { maxSize = 10 })"), "");
        const std::string before = stage.lua.Eval("show(l:getOptions())");

        EQUALS(stage.lua.Raised("l:update()"), "TextLabel: update expects an options object");
        EQUALS(stage.lua.Raised("l:update(5)"), "TextLabel: update expects an options object");
        EQUALS(stage.lua.Raised("l:update({ 1, 2 })"), "TextLabel: update expects an options object");
        EQUALS(stage.js.Thrown("TextLabel.create(0, 0, 0, 'x').update([1, 2])"), "TypeError: TextLabel: update expects an options object");
        EQUALS(stage.lua.Raised("l:update({ opacity = 0.5, colour = 1 })"), "TextLabel: unknown option 'colour'");
        EQUALS(stage.lua.Raised("l:update({ opacity = 0.5, fontSize = 0 })"), "TextLabel: fontSize must be a number from 1 to 512");
        EQUALS(stage.lua.Raised("l:update({ opacity = 0.5, minSize = 20 })"), "TextLabel: minSize must not exceed maxSize");
        EQUALS(stage.lua.Raised("l:update({ opacity = 0.5, attachTo = l })"), "TextLabel: attachTo must be another live entity");
        EQUALS(stage.lua.Raised("l:update({ opacity = 0.5, attachTo = 99999 })"), "TextLabel: attachTo must be another live entity");
        EQUALS(stage.lua.Raised("l:update({ opacity = 0.5, hideFrom = { 99999 } })"), "TextLabel: hideFrom: 99999 is not a player's avatar");
        EQUALS(stage.js.Thrown("TextLabel.create(0, 0, 0, 'x').update({ opacity: 0.5, hideFrom: [99999] })"), "Error: TextLabel: hideFrom: 99999 is not a player's avatar");
        EQUALS(stage.lua.Eval("show(l:getOptions())"), before);

        // An empty table changes nothing and is no error.
        EQUALS(stage.lua.Run("l:update({})"), "");
        EQUALS(stage.lua.Eval("show(l:getOptions())"), before);
        EQUALS(stage.lua.Raised("TextLabel.create(0, 0, 0, 'x', { attachTo = 99999 })"), "TextLabel: attachTo must be another live entity");
    });

    IT("keeps null apart from a key left out in JavaScript, and takes false for it in Lua", {
        LuaBuiltinsPlayerTest::Stage stage;
        stage.js.Eval(LuaBuiltinsPlayerTest::kJsShow);
        stage.js.Eval("globalThis.l = TextLabel.create(0, 0, 0, 'x', { attachTo: p, showTo: [p], hideFrom: [" + stage.b + "] }); 0");

        // undefined is a key left out.
        EQUALS(stage.js.Thrown("l.update({ attachTo: undefined, showTo: undefined, hideFrom: undefined, fontSize: undefined, outline: undefined })"), "no error");
        EQUALS(stage.js.Eval("show(l.getAudience()) + ' ' + l.getOptions().attachTo"), "{hideForTarget=false,hideFrom=[" + stage.b + "],showTo=[" + stage.a + "]} " + stage.a);
        // null is a value: wrong for a number, and the way to detach, to show to everyone and to hide from nobody.
        EQUALS(stage.js.Thrown("l.update({ fontSize: null })"), "TypeError: TextLabel: fontSize must be a number from 1 to 512");
        EQUALS(stage.js.Thrown("l.update({ color: null })"), "TypeError: TextLabel: color must be 0xAARRGGBB, \"#RRGGBB[AA]\" or a Color");
        EQUALS(stage.js.Thrown("l.update({ offset: null })"), "TypeError: TextLabel: offset must be a Vector3, {x, y, z} or [x, y, z]");
        EQUALS(stage.js.Thrown("l.update({ outline: { width: null } })"), "TypeError: TextLabel: outline.width must be a number from 0 to 4");
        EQUALS(stage.js.Thrown("l.update({ attachTo: null, showTo: null, hideFrom: null, outline: null, shadow: null })"), "no error");
        EQUALS(stage.js.Eval("show(l.getAudience()) + ' ' + l.getOptions().attachTo + ' ' + l.getOptions().outline + l.getOptions().shadow"), "{hideForTarget=false,hideFrom=[]} null falsefalse");
        // false is not null in JavaScript.
        EQUALS(stage.js.Thrown("l.update({ showTo: false })"), "TypeError: TextLabel: showTo must be an array of Players or their network ids");
        EQUALS(stage.js.Thrown("l.update({ attachTo: false })"), "TypeError: TextLabel: attachTo must be an entity or its network id");
        EQUALS(stage.js.Thrown("l.update({ showTo: {} })"), "TypeError: TextLabel: showTo must be an array of Players or their network ids");
        // The method detaches on null and refuses nothing at all.
        EQUALS(stage.js.Thrown("l.attachTo(p); l.attachTo(null)"), "no error");
        EQUALS(stage.js.Eval("String(l.getOptions().attachTo)"), "null");
        EQUALS(stage.js.Thrown("l.attachTo()"), "TypeError: TextLabel: target must be an entity or its network id");
        EQUALS(stage.js.Thrown("l.attachTo(p, null)"), "TypeError: TextLabel: offset must be a Vector3, {x, y, z} or [x, y, z]");
        EQUALS(stage.js.Thrown("l.attachTo(p, undefined)"), "no error");

        // Lua has no null to put in a table: false stands for it.
        EQUALS(stage.lua.Run(LuaBuiltinsPlayerTest::kLuaShow), "");
        EQUALS(stage.lua.Run("l = TextLabel.create(0, 0, 0, 'x', { attachTo = p, showTo = { p }, hideFrom = { " + stage.b + " } })"), "");
        EQUALS(stage.lua.Eval("show(l:getAudience()) .. ' ' .. l:getOptions().attachTo"), "{hideForTarget=false,hideFrom=[" + stage.b + "],showTo=[" + stage.a + "]} " + stage.a);
        EQUALS(stage.lua.Run("l:update({ attachTo = false, showTo = false, hideFrom = false, outline = false, shadow = false })"), "");
        EQUALS(stage.lua.Eval("show(l:getAudience()) .. ' ' .. tostring(l:getOptions().attachTo) .. ' ' .. tostring(l:getOptions().outline) .. tostring(l:getOptions().shadow)"), "{hideForTarget=false,hideFrom=[]} nil falsefalse");
        // Nobody and everybody are not the same audience.
        EQUALS(stage.lua.Run("l:update({ showTo = {} })"), "");
        EQUALS(stage.lua.Eval("show(l:getAudience())"), "{hideForTarget=false,hideFrom=[],showTo=[]}");
        // The method takes an entity or an id, and 0 detaches.
        EQUALS(stage.lua.Raised("l:attachTo()"), "TextLabel: target must be an entity or its network id");
        EQUALS(stage.lua.Raised("l:attachTo(false)"), "TextLabel: target must be an entity or its network id");
        EQUALS(stage.lua.Run("l:attachTo(p) l:attachTo(0)"), "");
        EQUALS(stage.lua.Eval("tostring(l:getOptions().attachTo)"), "nil");
    });

    IT("stops reading the options of JavaScript at the first getter that throws, and applies none", {
        LuaBuiltinsPlayerTest::Stage stage;
        stage.js.Eval("globalThis.l = TextLabel.create(0, 0, 0, 'x'); globalThis.ran = []; 0");
        // The keys are read in the order of the options, not of the object: fontSize before opacity.
        EQUALS(stage.js.Thrown("l.update({ get opacity() { ran.push('opacity'); return 0.5; }, get fontSize() { ran.push('fontSize'); throw new RangeError('mine'); }, get text() { ran.push('text'); return 'read'; } })"), "RangeError: mine");
        EQUALS(stage.js.Eval("ran.join(',') + ' ' + l.text + ' ' + l.fontSize + ' ' + l.getOptions().opacity"), "text,fontSize x 16 1");
        EQUALS(stage.js.Thrown("l.update({ outline: { get width() { throw new RangeError('inner'); } } })"), "RangeError: inner");
        EQUALS(stage.js.Thrown("l.update({ showTo: [{ get id() { throw new RangeError('element'); } }] })"), "RangeError: element");
        EQUALS(stage.js.Thrown("l.update({ offset: { get x() { throw new RangeError('component'); }, get y() { ran.push('y'); return 0; } } })"), "RangeError: component");
        EQUALS(stage.js.Thrown("l.update(new Proxy({}, { ownKeys() { throw new RangeError('keys'); } }))"), "RangeError: keys");
        EQUALS(stage.js.Eval("ran.includes('y')"), "false");

        // A getter that destroys the label leaves an update with nothing to change.
        EQUALS(stage.js.Eval("(() => { let fresh; l.update({ get opacity() { l.destroy(); fresh = TextLabel.create(0, 0, 0, 'z'); return 0.25; } }); return fresh.getOptions().opacity + '|' + l.getOptions(); })()"), "1|null");
    });

    // ==================== TextLabel: attaching ====================

    IT("attaches a label to an entity from Lua, follows it and returns when detached", {
        LuaBuiltinsPlayerTest::Stage stage;
        stage.entity->position = glm::vec3(100.0f, 0.0f, 0.0f);
        stage.entity->SetVirtualWorld(5);
        EQUALS(stage.lua.Run("l = TextLabel.create(1, 2, 3, 'tag')"), "");
        auto *label = stage.Label("l");

        EQUALS(stage.lua.Raised("l:attachTo(l)"), "TextLabel: attachTo must be another live entity");
        EQUALS(stage.lua.Raised("l:attachTo(l.id)"), "TextLabel: attachTo must be another live entity");
        EQUALS(stage.lua.Raised("l:attachTo(99999)"), "TextLabel: attachTo must be another live entity");
        EQUALS(stage.lua.Raised("l:attachTo('e')"), "TextLabel: target must be an entity or its network id");
        EQUALS(stage.lua.Raised("l:attachTo(e, 'up')"), "TextLabel: offset must be a Vector3, {x, y, z} or [x, y, z]");
        EQUALS(stage.lua.Raised("l:attachTo(e, { 1, 2, 'z' })"), "TextLabel: offset must be a number from -1e+09 to 1e+09");
        EQUALS(label->attachNetworkId, uint64_t(0));

        // By handle, with the offset in each of its forms; by id; by a table with an id.
        EQUALS(stage.lua.Run("l:attachTo(e, Vector3.new(0, 0, 2))"), "");
        EQUALS(label->attachNetworkId == stage.entity->GetNetworkID() && label->attachOffset == glm::vec3(0.0f, 0.0f, 2.0f), true);
        EQUALS(label->position == stage.entity->position && label->GetVirtualWorld() == 5, true);
        EQUALS(stage.lua.Run("l:attachTo(" + stage.e + ", { 0, 1, 0 })"), "");
        EQUALS(label->attachOffset == glm::vec3(0.0f, 1.0f, 0.0f), true);
        EQUALS(stage.lua.Run("l:attachTo({ id = " + stage.e + " }, { x = 1, y = 0, z = 0 })"), "");
        EQUALS(label->attachOffset == glm::vec3(1.0f, 0.0f, 0.0f), true);
        EQUALS(stage.lua.Run("l:attachTo(p)"), "");
        EQUALS(label->attachNetworkId == stage.players[0]->GetNetworkID() && label->attachOffset == glm::vec3(0.0f), true);

        // It follows its target on the server tick.
        stage.players[0]->position = glm::vec3(50.0f, 60.0f, 70.0f);
        Framework::Scripting::Builtins::TextLabel::FollowTargets(*stage.world.Manager());
        EQUALS(label->position == glm::vec3(50.0f, 60.0f, 70.0f), true);

        // hideForTarget keeps it from the player it follows.
        EQUALS(stage.lua.Run("l:update({ hideForTarget = true })"), "");
        EQUALS(label->streaming.Admits(stage.players[0]->ownerGUID), false);
        EQUALS(label->streaming.Admits(stage.players[1]->ownerGUID), true);
        EQUALS(stage.lua.Eval("tostring(l:getAudience().hideForTarget)"), "true");

        EQUALS(stage.lua.Run("l:detach()"), "");
        EQUALS(label->attachNetworkId == 0 && label->position == glm::vec3(1.0f, 2.0f, 3.0f) && label->GetVirtualWorld() == 0, true);
        EQUALS(label->streaming.Admits(stage.players[0]->ownerGUID), true);

        // An attached label is destroyed with its target.
        EQUALS(stage.lua.Run("l:attachTo(e)"), "");
        const uint64_t id = label->GetNetworkID();
        stage.world.Manager()->DestroyEntity(stage.entity);
        Framework::Scripting::Builtins::TextLabel::FollowTargets(*stage.world.Manager());
        EQUALS(stage.world.Manager()->GetEntityByNetworkID(id) == nullptr, true);
        EQUALS(stage.lua.Eval("tostring(l:getOptions())"), "nil");
    });

    // ==================== TextLabel: audience ====================

    IT("takes the players a label is for from Lua and reads them back as network ids", {
        LuaBuiltinsPlayerTest::Stage stage;
        EQUALS(stage.lua.Run(LuaBuiltinsPlayerTest::kLuaShow), "");
        auto *npc           = stage.world.Manager()->CreateEntity(stage.world.targetType);
        const std::string n = std::to_string(npc->GetNetworkID());
        const auto guidA    = stage.players[0]->ownerGUID;
        const auto guidB    = stage.players[1]->ownerGUID;

        // A player as a handle, as an id and as a table with an id.
        EQUALS(stage.lua.Run("l = TextLabel.create(0, 0, 0, 'x', { showTo = { p }, hideFrom = { { id = " + stage.b + " } }, hideForTarget = true })"), "");
        auto *label = stage.Label("l");
        EQUALS(stage.lua.Eval("show(l:getAudience())"), "{hideForTarget=true,hideFrom=[" + stage.b + "],showTo=[" + stage.a + "]}");
        EQUALS(label->streaming.Admits(guidA) && !label->streaming.Admits(guidB), true);
        EQUALS(stage.lua.Eval("math.type(l:getAudience().showTo[1])"), "integer");

        EQUALS(stage.lua.Run("l:setVisibleFor(" + stage.b + ", true) l:setVisibleFor(p, false)"), "");
        EQUALS(stage.lua.Eval("show(l:getAudience())"), "{hideForTarget=true,hideFrom=[" + stage.a + "],showTo=[" + stage.b + "]}");
        EQUALS(!label->streaming.Admits(guidA) && label->streaming.Admits(guidB), true);

        // getOptions() carries no audience, so applying it changes nobody's view.
        EQUALS(stage.lua.Eval("tostring(l:getOptions().showTo) .. tostring(l:getOptions().hideFrom) .. tostring(l:getOptions().hideForTarget)"), "nilnilnil");
        EQUALS(stage.lua.Run("l:update(l:getOptions())"), "");
        EQUALS(stage.lua.Eval("show(l:getAudience())"), "{hideForTarget=true,hideFrom=[" + stage.a + "],showTo=[" + stage.b + "]}");

        EQUALS(stage.lua.Run("l:update({ showTo = false, hideFrom = {} })"), "");
        EQUALS(stage.lua.Eval("show(l:getAudience())"), "{hideForTarget=true,hideFrom=[]}");
        EQUALS(label->streaming.Admits(guidA) && label->streaming.Admits(guidB), true);
        EQUALS(stage.lua.Eval("show(TextLabel.create(0, 0, 0, 'x', { showTo = {} }):getAudience().showTo)"), "[]");

        // What JavaScript set, Lua reads.
        stage.js.Eval("new TextLabel(" + stage.lua.Eval("math.tointeger(l.id)") + ").update({ showTo: [" + stage.a + ", " + stage.b + "] }); 0");
        EQUALS(stage.lua.Eval("show(l:getAudience().showTo)"), "[" + stage.a + "," + stage.b + "]");

        // No player or id is the caller's mistake; an id that is no player's avatar is the world's.
        EQUALS(stage.lua.Raised("TextLabel.create(0, 0, 0, 'x', { showTo = { " + n + " } })"), "TextLabel: showTo: " + n + " is not a player's avatar");
        EQUALS(stage.js.Thrown("TextLabel.create(0, 0, 0, 'x', { showTo: [" + n + "] })"), "Error: TextLabel: showTo: " + n + " is not a player's avatar");
        EQUALS(stage.lua.Raised("TextLabel.create(0, 0, 0, 'x', { hideFrom = { 99999 } })"), "TextLabel: hideFrom: 99999 is not a player's avatar");
        EQUALS(stage.lua.Raised("TextLabel.create(0, 0, 0, 'x', { hideFrom = { 0 } })"), "TextLabel: hideFrom: 0 is not a player's avatar");
        EQUALS(stage.lua.Raised("l:setVisibleFor(" + n + ", true)"), "TextLabel: setVisibleFor: " + n + " is not a player's avatar");
        EQUALS(stage.js.Thrown("TextLabel.create(0, 0, 0, 'x').setVisibleFor(" + n + ", true)"), "Error: TextLabel: setVisibleFor: " + n + " is not a player's avatar");
        EQUALS(stage.lua.Raised("l:setVisibleFor(0, true)"), "TextLabel: setVisibleFor: 0 is not a player's avatar");
        EQUALS(stage.lua.Raised("l:setVisibleFor(nil, true)"), "TextLabel: player must be a Player or their network id");
        EQUALS(stage.js.Thrown("TextLabel.create(0, 0, 0, 'x').setVisibleFor(null, true)"), "TypeError: TextLabel: player must be a Player or their network id");
        EQUALS(stage.lua.Raised("l:setVisibleFor(p)"), "TextLabel: setVisibleFor expects (player, visible)");
        EQUALS(stage.lua.Raised("l:setVisibleFor(p, 1)"), "TextLabel: setVisibleFor expects (player, visible)");
        EQUALS(stage.js.Thrown("TextLabel.create(0, 0, 0, 'x').setVisibleFor(" + stage.a + ")"), "TypeError: TextLabel: setVisibleFor expects (player, visible)");

        // A player with no avatar stays hidden, and is only left out of what getAudience() can show.
        EQUALS(stage.lua.Run("kept = TextLabel.create(0, 0, 0, 'k', { hideFrom = { p } }) snapshot = kept:getOptions()"), "");
        auto *kept = stage.Label("kept");
        stage.world.Manager()->DestroyEntity(stage.players[0]);
        EQUALS(stage.lua.Run("kept:update(snapshot) kept:update(kept:getOptions())"), "");
        EQUALS(stage.lua.Eval("show(kept:getAudience().hideFrom)"), "[]");
        EQUALS(kept->streaming.Admits(guidA), false);
    });

    // ==================== Resources and runtimes ====================

    IT("destroys the labels a Lua resource made when it stops, whenever it made them", {
        TextLabelWorld world;
        LuaResourcesTest::Server server;
        server.files.Resource("labels", {{"main.lua", "first = math.tointeger(TextLabel.create(0, 0, 0, 'at start').id)\nsetTimeout(function() later = math.tointeger(TextLabel.create(0, 0, 0, 'from a timer').id) end, 1)"}});
        server.files.Resource("other", {{"main.lua", "mine = math.tointeger(TextLabel.create(0, 0, 0, 'another resource').id)"}});
        EQUALS(server.Start(), true);
        EQUALS(server.UpdateUntil("labels", "later ~= nil", "true"), true);

        const uint64_t first = std::stoull(server.Eval("labels", "first"));
        const uint64_t later = std::stoull(server.Eval("labels", "later"));
        const uint64_t mine  = std::stoull(server.Eval("other", "mine"));
        EQUALS(world.Manager()->GetEntityByNetworkID(first) != nullptr && world.Manager()->GetEntityByNetworkID(later) != nullptr, true);

        server.module.GetResourceManager()->StopResource("labels");
        EQUALS(world.Manager()->GetEntityByNetworkID(first) == nullptr, true);
        EQUALS(world.Manager()->GetEntityByNetworkID(later) == nullptr, true);
        EQUALS(world.Manager()->GetEntityByNetworkID(mine) != nullptr, true);

        server.module.GetResourceManager()->StopResource("other");
        EQUALS(world.Manager()->GetEntityByNetworkID(mine) == nullptr, true);
        server.module.Shutdown();
    });

    IT("gives a label to the resource whose script created it, in JavaScript and in Lua", {
        LuaBuiltinsPlayerTest::Owners owners;
        auto &rig = owners.rig;
        EQUALS(rig.ready, true);
        // `maker` exports a function that creates a label; `caller` creates one as it loads, one from a timer and
        // one through that export.
        rig.files.Resource("maker", {{"main.js", "Exports.register('make', (text) => String(TextLabel.create(0, 0, 0, text).id)); globalThis.own = String(TextLabel.create(0, 0, 0, 'maker').id);"}}, ",\"exports\":[\"make\"]");
        rig.files.Resource("caller", {{"main.js", "globalThis.atLoad = String(TextLabel.create(0, 0, 0, 'load').id); globalThis.viaExport = Exports.get('maker', 'make')('export'); setTimeout(() => { globalThis.later = String(TextLabel.create(0, 0, 0, 'timer').id); }, 1);"}},
            ",\"dependencies\":[\"maker\"]");
        rig.files.Resource("luaside", {{"main.lua", "atLoad = math.tointeger(TextLabel.create(0, 0, 0, 'lua load').id)\nsetTimeout(function() later = math.tointeger(TextLabel.create(0, 0, 0, 'lua timer').id) end, 1)"}});
        EQUALS(rig.Start("maker"), "");
        EQUALS(rig.Start("caller"), "");
        EQUALS(rig.Start("luaside"), "");
        EQUALS(owners.Until("caller", "typeof later === 'string'"), true);
        // The Lua clock is the rig's to move.
        rig.Advance(60000);
        EQUALS(rig.Eval("luaside", "later ~= nil"), "true");

        const std::string own = owners.Js("maker", "own"), atLoad = owners.Js("caller", "atLoad"), later = owners.Js("caller", "later"), viaExport = owners.Js("caller", "viaExport");
        const std::string luaLoad = rig.Eval("luaside", "atLoad"), luaLater = rig.Eval("luaside", "later");
        EQUALS(owners.Lives(own) && owners.Lives(atLoad) && owners.Lives(later) && owners.Lives(viaExport) && owners.Lives(luaLoad) && owners.Lives(luaLater), true);

        // The caller takes what it created itself, at load and from its timer. What the export created is the
        // maker's, though the caller asked for it.
        EQUALS((bool)rig.manager.StopResource("caller"), true);
        EQUALS(owners.Lives(atLoad), false);
        EQUALS(owners.Lives(later), false);
        EQUALS(owners.Lives(viaExport), true);
        EQUALS(owners.Lives(own) && owners.Lives(luaLoad) && owners.Lives(luaLater), true);

        EQUALS((bool)rig.manager.StopResource("luaside"), true);
        EQUALS(owners.Lives(luaLoad), false);
        EQUALS(owners.Lives(luaLater), false);
        EQUALS(owners.Lives(own) && owners.Lives(viaExport), true);

        EQUALS((bool)rig.manager.StopResource("maker"), true);
        EQUALS(owners.Lives(own), false);
        EQUALS(owners.Lives(viaExport), false);
    });

    IT("forgets a label its script destroyed, so that the id is nobody's when it is used again", {
        LuaBuiltinsPlayerTest::Owners owners;
        auto &rig = owners.rig;
        rig.files.Resource("fleeting", {{"main.js", "const label = TextLabel.create(0, 0, 0, 'gone'); globalThis.id = String(label.id); label.destroy();"}});
        rig.files.Resource("luafleeting", {{"main.lua", "local label = TextLabel.create(0, 0, 0, 'gone')\nid = math.tointeger(label.id)\nlabel:destroy()"}});
        EQUALS(rig.Start("fleeting"), "");
        EQUALS(rig.Start("luafleeting"), "");
        const std::string ids[] {owners.Js("fleeting", "id"), rig.Eval("luafleeting", "id")};
        EQUALS(owners.Lives(ids[0]) || owners.Lives(ids[1]), false);

        // Entities of the host under the ids the labels had: a resource that still tracked one would destroy it.
        for (const std::string &id : ids) {
            owners.world.Manager()->CreateEntity(owners.world.targetType)->SetNetworkID(std::stoull(id));
        }
        EQUALS(owners.Lives(ids[0]) && owners.Lives(ids[1]), true);
        EQUALS((bool)rig.manager.StopResource("fleeting"), true);
        EQUALS((bool)rig.manager.StopResource("luafleeting"), true);
        EQUALS(owners.Lives(ids[0]) && owners.Lives(ids[1]), true);
    });

    // ==================== Handles of other classes ====================

    IT("takes a handle a project bound by hand, with an id and no Entity behind it, as JavaScript always did", {
        LuaBuiltinsPlayerTest::Stage stage;
        stage.js.js.Enter([](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            LuaBuiltinsPlayerTest::ForeignHandle::Register(isolate, context);
        });
        stage.js.Eval(LuaBuiltinsPlayerTest::kJsShow);
        stage.js.Eval("globalThis.fe = new ForeignHandle(" + stage.e + "); globalThis.fa = new ForeignHandle(" + stage.a + "); globalThis.fb = new ForeignHandle(" + stage.b + "); globalThis.none = new ForeignHandle(99999); 0");

        EQUALS(stage.js.Thrown("globalThis.l = TextLabel.create(0, 0, 0, 'x', { attachTo: fe, showTo: [fa], hideFrom: [fb] })"), "no error");
        EQUALS(stage.js.Eval("l.getOptions().attachTo + ' ' + show(l.getAudience())"), stage.e + " {hideForTarget=false,hideFrom=[" + stage.b + "],showTo=[" + stage.a + "]}");
        EQUALS(stage.js.Thrown("l.detach(); l.attachTo(fa); l.setVisibleFor(fb, true); l.setVisibleFor(fa, false)"), "no error");
        EQUALS(stage.js.Eval("l.getOptions().attachTo + ' ' + show(l.getAudience())"), stage.a + " {hideForTarget=false,hideFrom=[" + stage.a + "],showTo=[" + stage.b + "]}");
        EQUALS(stage.js.Thrown("l.update({ attachTo: fe })"), "no error");
        EQUALS(stage.js.Eval("String(l.getOptions().attachTo)"), stage.e);

        // Its id is checked as any id is.
        EQUALS(stage.js.Thrown("l.attachTo(none)"), "TypeError: TextLabel: attachTo must be another live entity");
        EQUALS(stage.js.Thrown("l.setVisibleFor(none, true)"), "Error: TextLabel: setVisibleFor: 99999 is not a player's avatar");
        EQUALS(stage.js.Thrown("l.attachTo(new ForeignHandle(1.5))"), "TypeError: TextLabel: target must be an entity or its network id");
        // A bound object without an id is no target.
        EQUALS(stage.js.Thrown("l.attachTo(new Vector3(1, 2, 3))"), "TypeError: TextLabel: target must be an entity or its network id");

        // Any bound object with an x, a y and a z is a place.
        EQUALS(stage.js.Thrown("l.update({ offset: fe, position: new Vector4(7, 8, 9, 1) })"), "no error");
        EQUALS(stage.js.Eval("show(l.getOptions().offset) + show(l.getOptions().position)"), "{x=1,y=2,z=3}{x=7,y=8,z=9}");
        EQUALS(stage.js.Thrown("l.update({ offset: new Vector2(1, 2) })"), "TypeError: TextLabel: offset must be a number from -1e+09 to 1e+09");

        // In Lua a bound object is read through the properties its class declares.
        EQUALS(stage.lua.Run("l = TextLabel.create(0, 0, 0, 'x') l:update({ attachTo = Entity.new(" + stage.e + "), offset = Vector4.new(4, 5, 6, 1) })"), "");
        EQUALS(stage.lua.Eval("math.tointeger(l:getOptions().attachTo) .. ' ' .. l:getOptions().offset.x .. l:getOptions().offset.z"), stage.e + " 4.06.0");
        EQUALS(stage.lua.Raised("l:attachTo(Vector3.new(1, 2, 3))"), "TextLabel: target must be an entity or its network id");
        EQUALS(stage.lua.Raised("l:update({ offset = Vector2.new(1, 2) })"), "TextLabel: offset must be a number from -1e+09 to 1e+09");
    });

    IT("carries a Player and a TextLabel to another runtime as themselves, in both languages", {
        Framework::Scripting::Builtins::RegisterTransferTypes();
        LuaBuiltinsPlayerTest::Stage stage;
        lua_State *L = stage.lua.Runtime().State();
        EQUALS(stage.lua.Run("l = TextLabel.create(0, 0, 0, 'crossing')"), "");
        stage.js.Eval("globalThis.l = new TextLabel(" + stage.lua.Eval("math.tointeger(l.id)") + "); 0");

        // Lua to JavaScript.
        for (const std::string name : {"p", "l"}) {
            EQUALS(luaL_loadstring(L, ("return " + name).c_str()) == LUA_OK && lua_pcall(L, 0, 1, 0) == LUA_OK, true);
            auto copied = Framework::Scripting::LuaTransfer::Copy(L, -1);
            lua_pop(L, 1);
            EQUALS(static_cast<bool>(copied), true);
            EQUALS(copied.GetValue().kind == Framework::Scripting::TransferredValue::Kind::HostObject, true);
            EQUALS(copied.GetValue().text, std::string(name == "p" ? "Player" : "TextLabel"));
            stage.js.js.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                v8::Local<v8::Value> rebuilt;
                if (Framework::Scripting::ValueTransfer::Rebuild(isolate, context, copied.GetValue()).ToLocal(&rebuilt)) {
                    context->Global()->Set(context, v8pp::to_v8(isolate, "arrived_" + name), rebuilt).Check();
                }
            });
        }
        EQUALS(stage.js.Eval("[arrived_p instanceof Player, arrived_p.toString(), arrived_l instanceof TextLabel, arrived_l.text].join(' ')"), "true Player{ id: " + stage.a + " } true crossing");

        // JavaScript to Lua.
        for (const std::string name : {"p", "l"}) {
            auto copied = Framework::Utils::Result<Framework::Scripting::TransferredValue, std::string>::Err("not copied");
            stage.js.js.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                v8::Local<v8::Value> value;
                if (context->Global()->Get(context, v8pp::to_v8(isolate, name)).ToLocal(&value)) {
                    copied = Framework::Scripting::ValueTransfer::Copy(isolate, context, value);
                }
            });
            EQUALS(static_cast<bool>(copied), true);
            EQUALS(copied.GetValue().text, std::string(name == "p" ? "Player" : "TextLabel"));
            std::string error;
            EQUALS(Framework::Scripting::LuaTransfer::Rebuild(L, copied.GetValue(), error), true);
            lua_setglobal(L, ("arrived_" + name).c_str());
        }
        EQUALS(stage.lua.Eval("arrived_p:toString() .. ' ' .. type(arrived_p.kick) .. ' ' .. arrived_l.text .. ' ' .. type(arrived_l.update)"), "Player{ id: " + stage.a + " } function crossing function");

        // A label that went on the way does not arrive.
        EQUALS(luaL_loadstring(L, "return l") == LUA_OK && lua_pcall(L, 0, 1, 0) == LUA_OK, true);
        auto late = Framework::Scripting::LuaTransfer::Copy(L, -1);
        lua_pop(L, 1);
        EQUALS(stage.lua.Run("l:destroy()"), "");
        std::string error;
        EQUALS(Framework::Scripting::LuaTransfer::Rebuild(L, late.GetValue(), error), false);
        EQUALS(LuaRuntimeTest::Contains(error, "is not valid!"), true);
    });
})
