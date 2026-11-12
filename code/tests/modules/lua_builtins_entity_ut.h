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
#include "node_test_helpers.h"
#include "text_label_ut.h"

#include "networking/replication/state_bag.h"
#include "scripting/bind/call.h"
#include "scripting/bind/callable.h"
#include "scripting/builtins/builtins.h"
#include "scripting/builtins/entity.h"
#include "scripting/builtins/player.h"
#include "scripting/builtins/state_bag.h"
#include "scripting/engine_scope.h"
#include "scripting/lua/lua_engine.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/lua/lua_transfer.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/scripting_catalog.h"
#include "scripting/value_transfer.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Entity and StateBag, declared once for JavaScript and Lua: what a Lua script gets of them, held against what a
// JavaScript one gets, over one replication manager both languages write to.
namespace LuaBuiltinsEntityTest {
    using namespace Framework::Scripting;
    using Builtins::Entity;
    using Builtins::StateBag;
    using Framework::Networking::Replication::NetworkEntity;
    using Framework::Networking::Replication::StateScope;
    using Framework::Networking::Replication::StateValue;
    using NativeBag = Framework::Networking::Replication::StateBag;

    constexpr const char *kLuaResource = "lua_side";
    constexpr const char *kJsResource  = "js_side";

    // A Lua engine whose runtimes get the value types and Entity the way the server module gives them, with one
    // runtime of the resource.
    struct LuaSide final {
        LuaEngine engine;

        // `manager` is what the runtimes answer their calls with, as the server module tells them its own.
        explicit LuaSide(Bind::Side side = Bind::Side::Server, ResourceManager *manager = nullptr): engine(LuaBuiltinsCoreTest::LuaSide::Options(side)) {
            Bind::WatchRuntimes(engine);
            engine.AddRuntimeDisposingListener([](LuaRuntime &runtime) {
                StateBag::DropDeadSubscriptions();
                Bind::SetCallResourceManager(&runtime, nullptr);
            });
            engine.SetRuntimeSetupCallback([side, manager](LuaRuntime &runtime) {
                Bind::SetCallResourceManager(&runtime, manager);
                Builtins::RegisterValueTypesLua(runtime.State(), side);
                Entity::RegisterLua(runtime);
            });
            ready = engine.Init() == ScriptingError::SCRIPTING_NONE && engine.CreateResourceRuntime(kLuaResource);
        }

        LuaRuntime &Runtime() {
            return *engine.GetRuntime(kLuaResource);
        }

        // Empty when the chunk ran, the first line of its error otherwise.
        std::string Run(const std::string &source) {
            return LuaRuntimeTest::FirstLine(LuaRuntimeTest::Run(Runtime(), source));
        }

        std::string Eval(const std::string &expression) {
            return LuaRuntimeTest::Eval(Runtime(), expression);
        }

        bool ready = false;
    };

    // A JavaScript runtime with the value types and Entity registered through their Register, under a manager that
    // says which resource is running.
    struct JsSide final {
        LuaBuiltinsCoreTest::JsRuntime js;
        ResourceManager manager;

        explicit JsSide(bool isClient = false): manager(&js.engine) {
            static int count = 0;
            SetScriptingCatalog(js.Isolate(), "lua_builtins_entity/" + std::to_string(++count));
            SetScriptingEnvironment(js.Isolate(), isClient);
            js.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                Builtins::RegisterValueTypes(isolate, context->Global());
                Entity::Register(isolate, context->Global());
                Bind::SetCallResourceManager(isolate, &manager);
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

    // What a Lua error says after the position of the script that raised it.
    inline std::string Raised(LuaSide &lua, const std::string &source) {
        const std::string error = lua.Run(source);
        const size_t at         = error.find(": ");
        return error.empty() ? "no error" : error.substr(at == std::string::npos ? 0 : at + 2);
    }

    // The world both languages write to, with one entity each language holds as the global `e`.
    struct Stage final {
        TextLabelWorld world;
        NetworkEntity *entity;
        std::string id;
        JsSide js;
        LuaSide lua;

        Stage(): entity(world.Manager()->CreateEntity(world.targetType)), id(std::to_string(entity->GetNetworkID())), lua(Bind::Side::Server, &js.manager) {
            lua.Run("e = Entity.new(" + id + ")");
            js.Eval("globalThis.e = new Entity(" + id + "); 0");
        }

        const StateValue *Stored(const std::string &key) const {
            return entity->state.Get(key);
        }

        std::string StoredJson(const std::string &key) const {
            const StateValue *value = Stored(key);
            return value != nullptr && value->type == StateValue::Type::Json ? value->text : "<not json>";
        }
    };

    // Host functions for the scripts of a Stage: destroyEntity() takes the entity of the stage away.
    inline int LuaDestroyEntity(lua_State *L) {
        auto *stage = static_cast<Stage *>(lua_touserdata(L, lua_upvalueindex(1)));
        stage->world.Manager()->DestroyEntity(stage->entity);
        return 0;
    }

    inline void InstallDestroyEntity(Stage &stage) {
        lua_State *L = stage.lua.Runtime().State();
        lua_pushlightuserdata(L, &stage);
        lua_pushcclosure(L, LuaDestroyEntity, 1);
        lua_setglobal(L, "destroyEntity");
        stage.js.js.Enter([&stage](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            const auto destroy = [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                auto *self = static_cast<Stage *>(info.Data().As<v8::External>()->Value());
                self->world.Manager()->DestroyEntity(self->entity);
            };
            context->Global()->Set(context, v8pp::to_v8(isolate, "destroyEntity"), v8::Function::New(context, destroy, v8::External::New(isolate, &stage)).ToLocalChecked()).Check();
        });
    }

    // The server scripting module over resources on disk, with the world its scripts write to: every resource
    // runs in runtimes of its own, as on a server. A JavaScript runtime gets Entity the way a game registers
    // it, and both languages get stop(name), which stops a resource. `ENTITY` in a script is the id of the one
    // entity.
    struct Realm final {
        TextLabelWorld world;
        NetworkEntity *entity;
        std::string id;
        LuaResourcesTest::Files files;
        // What the manager told its listeners: "<resource>: <first line>" for each error, "<resource>" for each
        // stop.
        std::vector<std::string> errors;
        std::vector<std::string> stopped;
        LuaResourcesTest::ServerScriptingModule module;

        Realm(): entity(world.Manager()->CreateEntity(world.targetType)), id(std::to_string(entity->GetNetworkID())) {}

        void Resource(const std::string &name, const std::string &file, std::string script, const std::string &errorBehavior = "continue") {
            for (size_t at = script.find("ENTITY"); at != std::string::npos; at = script.find("ENTITY", at)) {
                script.replace(at, 6, id);
            }
            files.Resource(name, {{file, script}}, ",\"errorBehavior\":\"" + errorBehavior + "\"");
        }

        bool Start() {
            module.SetResourcesPath(files.root.string());
            const auto registered = module.Init([this](Engine *engine) {
                EngineScope scope(engine);
                v8::Isolate *isolate           = scope.Isolate();
                v8::Local<v8::Context> context = scope.Context();
                Entity::Register(isolate, context->Global());
                context->Global()->Set(context, v8pp::to_v8(isolate, "stop"), v8::Function::New(context, JavaScriptStop, v8::External::New(isolate, this)).ToLocalChecked()).Check();
            });
            if (registered != ScriptingError::SCRIPTING_NONE) {
                return false;
            }
            module.GetResourceManager()->AddOnResourceError([this](const std::string &name, const std::string &error) {
                errors.push_back(name + ": " + LuaRuntimeTest::FirstLine(error));
            });
            module.GetResourceManager()->AddOnResourceStopped([this](const std::string &name) {
                stopped.push_back(name);
            });
            // The module's own bindings give a Lua script no stop(): it is added once the runtimes exist, for
            // the handlers to call.
            return module.StartAllResources() && InstallLuaStop();
        }

        bool InstallLuaStop() {
            for (const std::string &name : module.GetResourceManager()->GetAllResourceNames()) {
                if (LuaRuntime *runtime = module.GetLuaEngine()->GetRuntime(name)) {
                    lua_State *L = runtime->State();
                    lua_pushlightuserdata(L, this);
                    lua_pushcclosure(L, LuaStop, 1);
                    lua_setglobal(L, "stop");
                }
            }
            return true;
        }

        static int LuaStop(lua_State *L) {
            auto *self = static_cast<Realm *>(lua_touserdata(L, lua_upvalueindex(1)));
            self->module.GetResourceManager()->StopResource(luaL_checkstring(L, 1));
            return 0;
        }

        static void JavaScriptStop(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = static_cast<Realm *>(info.Data().As<v8::External>()->Value());
            v8::String::Utf8Value name(info.GetIsolate(), info[0]);
            self->module.GetResourceManager()->StopResource(*name ? *name : "");
        }

        // A write of the host, which wakes the handlers of every resource.
        void Write(const std::string &key, double number) {
            StateValue value;
            value.type   = StateValue::Type::Number;
            value.number = number;
            entity->state.Set(key, value);
        }

        std::string Lua(const std::string &resource, const std::string &expression) const {
            LuaRuntime *runtime = module.GetLuaEngine()->GetRuntime(resource);
            return runtime != nullptr ? LuaRuntimeTest::Eval(*runtime, expression) : "<no runtime>";
        }

        std::string Js(const std::string &resource, const std::string &expression) const {
            NodeRuntime *runtime = module.GetEngine()->GetResourceRuntime(resource);
            return runtime != nullptr ? NodeTest::Eval(*runtime, expression) : "<no runtime>";
        }

        // Ticks the module as a server does, until `count` errors were handed to the manager or time runs out.
        bool UpdateUntilErrors(size_t count, int timeoutMs = 3000) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (errors.size() < count && std::chrono::steady_clock::now() < deadline) {
                module.Update();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return errors.size() >= count;
        }

        // The error the manager was handed for `resource`, or "<none>".
        std::string ErrorOf(const std::string &resource) const {
            for (const std::string &error : errors) {
                if (error.starts_with(resource + ": ")) {
                    return error.substr(resource.size() + 2);
                }
            }
            return "<none>";
        }
    };
} // namespace LuaBuiltinsEntityTest

MODULE(lua_builtins_entity, {
    using namespace LuaBuiltinsEntityTest;

    // ==================== Entity ====================

    IT("gives a Lua state Entity under the name JavaScript has, and no StateBag to construct", {
        Stage stage;
        EQUALS(stage.lua.ready, true);
        EQUALS(stage.lua.Eval("type(Entity)"), "table");
        EQUALS(stage.lua.Eval("type(StateBag)"), "nil");
        EQUALS(stage.js.Eval("typeof Entity + '/' + typeof StateBag"), "function/undefined");

        EQUALS(stage.lua.Eval("e.id"), stage.id);
        EQUALS(stage.lua.Eval("math.type(e.id)"), "integer");
        EQUALS(stage.lua.Eval("Entity(" + stage.id + ").id"), stage.id);
        EQUALS(stage.js.Eval("String(e.id)"), stage.id);

        const std::string spelled = "Entity{ id: " + stage.id + " }";
        EQUALS(stage.lua.Eval("e:toString()"), spelled);
        EQUALS(stage.lua.Eval("tostring(e)"), spelled);
        EQUALS(stage.js.Eval("e.toString()"), spelled);

        // A handle is made for an entity that exists, in both languages.
        EQUALS(Raised(stage.lua, "Entity.new(999999)"), "Entity handle '999999' is not valid!");
        EQUALS(stage.js.Thrown("new Entity(999999)"), "Error: Entity handle '999999' is not valid!");
        EQUALS(stage.lua.Run("e.id = 5"), "test:1: attempt to set 'id' on Entity");
    });

    IT("reads and writes the transform of the entity from Lua as from JavaScript", {
        Stage stage;
        EQUALS(stage.lua.Run("e.position = Vector3.new(1, 2, 3)"), "");
        EQUALS(stage.entity->position.y == 2.0f, true);
        EQUALS(stage.js.Eval("e.position.toString()"), "Vector3(1, 2, 3)");
        stage.js.Eval("e.position = new Vector3(4, 5, 6); 0");
        EQUALS(stage.lua.Eval("tostring(e.position)"), "Vector3(4, 5, 6)");

        // The rotation reads as a quaternion and takes a quaternion or Euler angles in degrees.
        EQUALS(stage.lua.Run("e.rotation = Quaternion.new(0, 0, 1, 0)"), "");
        EQUALS(stage.entity->rotation.w == 0.0f && stage.entity->rotation.x == 0.0f, true);
        EQUALS(stage.lua.Eval("tostring(e.rotation)"), stage.js.Eval("e.rotation.toString()"));
        EQUALS(stage.lua.Run("e.rotation = Vector3.new(0, 0, 0)"), "");
        EQUALS(stage.entity->rotation.w == 1.0f, true);
        EQUALS(stage.js.Eval("e.rotation = new Vector3(0, 180, 0); e.rotation instanceof Quaternion"), "true");
        EQUALS(stage.entity->rotation.w < 0.001f && stage.entity->rotation.w > -0.001f, true);
        EQUALS(Raised(stage.lua, "e.rotation = 5"), "rotation must be a Vector3 (euler degrees) or Quaternion");
        EQUALS(stage.js.Thrown("e.rotation = 5"), "TypeError: rotation must be a Vector3 (euler degrees) or Quaternion");

        EQUALS(stage.lua.Eval("e.virtualWorld"), "0");
        EQUALS(stage.lua.Run("e:setVirtualWorld(7)"), "");
        EQUALS(stage.entity->GetVirtualWorld(), 7u);
        EQUALS(stage.js.Eval("e.setVirtualWorld(9); e.virtualWorld"), "9");
        EQUALS(stage.lua.Eval("e.virtualWorld"), "9");

        // A handle whose entity is gone reads as nothing and writes nothing.
        stage.world.Manager()->DestroyEntity(stage.entity);
        EQUALS(stage.lua.Eval("tostring(e.position) .. ' ' .. e.virtualWorld"), "Vector3(0, 0, 0) 0");
        EQUALS(stage.lua.Run("e.position = Vector3.new(1, 1, 1) e.rotation = Quaternion.new(1, 0, 0, 0) e:setVirtualWorld(3) e:setVisibleTo(nil)"), "");
        EQUALS(stage.js.Eval("e.position = new Vector3(1, 1, 1); e.setVirtualWorld(3); e.position.toString()"), "Vector3(0, 0, 0)");
    });

    IT("restricts an entity to the connection of a player, or refuses the target", {
        Stage stage;
        auto *player            = stage.world.Manager()->CreateEntity(stage.world.targetType);
        player->ownerGUID       = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(71));
        const std::string owned = std::to_string(player->GetNetworkID());
        auto *npc               = stage.world.Manager()->CreateEntity(stage.world.targetType);
        const std::string loose = std::to_string(npc->GetNetworkID());

        EQUALS(stage.lua.Run("e:setVisibleTo(Entity.new(" + owned + "))"), "");
        EQUALS(stage.entity->streaming.targetGUID == player->ownerGUID, true);
        EQUALS(stage.lua.Run("e:setVisibleTo(nil)"), "");
        EQUALS(stage.entity->streaming.targetGUID == MafiaNet::UNASSIGNED_PEER_GUID, true);
        EQUALS(stage.js.Eval("e.setVisibleTo(new Entity(" + owned + ")); 0"), "0");
        EQUALS(stage.entity->streaming.targetGUID == player->ownerGUID, true);
        EQUALS(stage.lua.Run("e:setVisibleTo()"), "");
        EQUALS(stage.entity->streaming.targetGUID == MafiaNet::UNASSIGNED_PEER_GUID, true);

        EQUALS(Raised(stage.lua, "e:setVisibleTo(5)"), "setVisibleTo: expected a Player (or null to clear)");
        EQUALS(stage.js.Thrown("e.setVisibleTo(5)"), "TypeError: setVisibleTo: expected a Player (or null to clear)");
        EQUALS(Raised(stage.lua, "e:setVisibleTo(Entity.new(" + loose + "))"), "setVisibleTo: target has no owning connection");
        EQUALS(stage.js.Thrown("e.setVisibleTo(new Entity(" + loose + "))"), "Error: setVisibleTo: target has no owning connection");
    });

    IT("leaves what the server decides out of a client runtime, in both languages", {
        TextLabelWorld world;
        const std::string id = std::to_string(world.Manager()->CreateEntity(world.targetType)->GetNetworkID());
        LuaSide lua(Bind::Side::Client);
        JsSide js(/*isClient*/ true);
        lua.Run("e = Entity.new(" + id + ")");
        js.Eval("globalThis.e = new Entity(" + id + "); 0");

        EQUALS(lua.Eval("type(e.setVirtualWorld) .. type(e.setVisibleTo) .. type(e.state.set) .. type(e.state.remove)"), "nilnilnilnil");
        EQUALS(js.Eval("typeof e.setVirtualWorld + typeof e.setVisibleTo + typeof e.state.set + typeof e.state.remove"), "undefinedundefinedundefinedundefined");
        EQUALS(lua.Eval("type(e.state.get) .. type(e.state.has) .. type(e.state.keys) .. type(e.state.toObject) .. type(e.state.onChange)"), "functionfunctionfunctionfunctionfunction");
        EQUALS(lua.Eval("e.virtualWorld"), "0");
    });

    IT("keeps the JavaScript Player an Entity", {
        Stage stage;
        auto *avatar         = stage.world.Manager()->CreateEntity(stage.world.targetType);
        avatar->ownerGUID    = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(72));
        const std::string id = std::to_string(avatar->GetNetworkID());
        stage.js.js.Enter([](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            Builtins::Player::Register(isolate, context->Global());
        });

        EQUALS(stage.js.Eval("globalThis.p = new Player(" + id + "); [p instanceof Entity, p.toString(), String(p.id), p.virtualWorld].join(' ')"), "true Player{ id: " + id + " } " + id + " 0");
        EQUALS(stage.js.Eval("p.position = new Vector3(7, 8, 9); p.rotation = new Vector3(0, 0, 0); p.setVirtualWorld(2); p.position.toString() + ' ' + (p.rotation instanceof Quaternion) + ' ' + p.virtualWorld"), "Vector3(7, 8, 9) true 2");
        EQUALS(avatar->position.z == 9.0f, true);
        EQUALS(stage.js.Eval("p.state.set('job', 'smith'); p.state.get('job')"), "smith");
        EQUALS(avatar->state.Has("job"), true);
        EQUALS(stage.js.Eval("e.setVisibleTo(p); 0"), "0");
        EQUALS(stage.entity->streaming.targetGUID == avatar->ownerGUID, true);

        // It crosses to another runtime as a Player, the newest registration, and not as its base.
        Builtins::RegisterTransferTypes();
        std::string kind;
        stage.js.js.Enter([&kind](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            v8::Local<v8::Value> player;
            if (!context->Global()->Get(context, v8pp::to_v8(isolate, "p")).ToLocal(&player)) {
                return;
            }
            const auto copied = ValueTransfer::Copy(isolate, context, player);
            kind              = copied ? copied.GetValue().text : copied.GetError();
        });
        EQUALS(kind, "Player");
    });

    // ==================== StateBag: values ====================

    IT("stores scalars as themselves and reads them back in Lua", {
        Stage stage;
        EQUALS(stage.lua.Eval("type(e.state)"), "userdata");
        EQUALS(stage.lua.Eval("e.state:set('flag', true)"), "true");
        EQUALS(stage.Stored("flag")->type == StateValue::Type::Boolean && stage.Stored("flag")->boolean, true);
        EQUALS(stage.lua.Eval("e.state:set('level', 42.5)"), "true");
        EQUALS(stage.Stored("level")->type == StateValue::Type::Number && stage.Stored("level")->number == 42.5, true);
        EQUALS(stage.lua.Eval("e.state:set('job', 'blacksmith')"), "true");
        EQUALS(stage.Stored("job")->type == StateValue::Type::String, true);
        STREQUALS(stage.Stored("job")->text.c_str(), "blacksmith");

        // nil is a stored null: the key is set, and reads as nothing.
        EQUALS(stage.lua.Eval("e.state:set('nothing', nil)"), "true");
        EQUALS(stage.Stored("nothing")->type == StateValue::Type::Null, true);
        EQUALS(stage.lua.Eval("tostring(e.state:get('nothing')) .. ' ' .. tostring(e.state:has('nothing'))"), "nil true");

        EQUALS(stage.lua.Eval("tostring(e.state:get('flag')) .. ' ' .. e.state:get('level') .. ' ' .. e.state:get('job')"), "true 42.5 blacksmith");
        EQUALS(stage.lua.Eval("tostring(e.state:get('missing')) .. ' ' .. tostring(e.state:has('missing'))"), "nil false");
        EQUALS(stage.lua.Eval("select('#', e.state:get('missing'))"), "1");
        // A whole number comes back as one, whatever wrote it.
        EQUALS(stage.lua.Eval("e.state:set('count', 3) and math.type(e.state:get('count')) .. ' ' .. math.type(e.state:get('level'))"), "integer float");
        EQUALS(stage.js.Eval("e.state.set('zero', -0); Object.is(e.state.get('zero'), -0) + ' ' + e.state.get('count') + ' ' + e.state.get('level')"), "true 3 42.5");

        // The same value again is not a change.
        EQUALS(stage.lua.Eval("e.state:set('job', 'blacksmith')"), "false");
        EQUALS(stage.lua.Eval("table.concat(e.state:keys(), ',')"), "count,flag,job,level,nothing,zero");
        EQUALS(stage.lua.Eval("tostring(e.state:remove('job')) .. ' ' .. tostring(e.state:remove('job')) .. ' ' .. tostring(e.state:has('job'))"), "true false false");
    });

    IT("stores anything else as the JSON JavaScript writes for the same data", {
        Stage stage;
        EQUALS(stage.lua.Eval("e.state:set('fromLua', { name = 'anvil', tags = { 'a', 'b' }, nested = { count = 2, ok = true } })"), "true");
        EQUALS(stage.js.Eval("e.state.set('fromJs', { name: 'anvil', nested: { count: 2, ok: true }, tags: ['a', 'b'] })"), "true");
        EQUALS(stage.StoredJson("fromLua"), R"({"name":"anvil","nested":{"count":2,"ok":true},"tags":["a","b"]})");
        EQUALS(stage.StoredJson("fromJs"), stage.StoredJson("fromLua"));

        EQUALS(stage.lua.Eval("e.state:set('list', { 1, 2, 3 })"), "true");
        EQUALS(stage.StoredJson("list"), "[1,2,3]");
        EQUALS(stage.lua.Eval("e.state:set('where', Vector3.new(1, 2, 3))"), "true");
        EQUALS(stage.js.Eval("e.state.set('whereJs', new Vector3(1, 2, 3))"), "true");
        EQUALS(stage.StoredJson("where"), R"({"x":1,"y":2,"z":3})");
        EQUALS(stage.StoredJson("whereJs"), stage.StoredJson("where"));

        // Read back, a document is a table.
        EQUALS(stage.lua.Eval("(function() local v = e.state:get('fromLua') return v.name .. v.tags[2] .. v.nested.count .. tostring(v.nested.ok) .. math.type(v.nested.count) end)()"), "anvilb2trueinteger");
        EQUALS(stage.lua.Eval("#e.state:get('list')"), "3");

        // A document that no longer parses reads as its text, and raises nothing.
        StateValue broken;
        broken.type = StateValue::Type::Json;
        broken.text = "{not json";
        stage.entity->state.Set("broken", broken);
        EQUALS(stage.lua.Eval("e.state:get('broken')"), "{not json");
        EQUALS(stage.js.Eval("e.state.get('broken')"), "{not json");
    });

    IT("reads in each language what the other wrote", {
        Stage stage;
        stage.js.Eval("e.state.set('flag', true); e.state.set('level', 7); e.state.set('job', 'smith'); e.state.set('none', null); e.state.set('doc', { tools: ['hammer', 'tongs'], skill: { forge: 3 } }); 0");
        EQUALS(stage.lua.Eval("tostring(e.state:get('flag')) .. ' ' .. e.state:get('level') .. ' ' .. e.state:get('job') .. ' ' .. tostring(e.state:get('none'))"), "true 7 smith nil");
        EQUALS(stage.lua.Eval("(function() local doc = e.state:get('doc') return doc.tools[1] .. ',' .. doc.tools[2] .. ',' .. doc.skill.forge end)()"), "hammer,tongs,3");

        EQUALS(stage.lua.Run("e.state:set('luaFlag', false) e.state:set('luaLevel', 9) e.state:set('luaJob', 'farmer') e.state:set('luaNone', nil) e.state:set('luaDoc', { crops = { 'wheat', 'rye' }, field = { acres = 12 } })"), "");
        EQUALS(stage.js.Eval("[e.state.get('luaFlag'), e.state.get('luaLevel'), e.state.get('luaJob'), e.state.get('luaNone'), e.state.get('missing')].map((v) => v === undefined ? 'undefined' : JSON.stringify(v)).join(' ')"), "false 9 \"farmer\" null undefined");
        EQUALS(stage.js.Eval("JSON.stringify(e.state.get('luaDoc'))"), R"({"crops":["wheat","rye"],"field":{"acres":12}})");

        // The whole bag, as each language holds it.
        EQUALS(stage.js.Eval("Object.keys(e.state.toObject()).join(',')"), "doc,flag,job,level,luaDoc,luaFlag,luaJob,luaLevel,luaNone,none");
        EQUALS(stage.js.Eval("JSON.stringify(e.state.keys())"), R"(["doc","flag","job","level","luaDoc","luaFlag","luaJob","luaLevel","luaNone","none"])");
        EQUALS(LuaRuntimeTest::Keys(stage.lua.Runtime(), "e.state:toObject()"), "doc,flag,job,level,luaDoc,luaFlag,luaJob,luaLevel");
        EQUALS(stage.lua.Eval("(function() local all = e.state:toObject() return all.job .. ' ' .. all.luaDoc.crops[2] .. ' ' .. tostring(all.flag) end)()"), "smith rye true");
        EQUALS(stage.js.Eval("(() => { const all = e.state.toObject(); return all.luaJob + ' ' + all.doc.tools[0] + ' ' + all.none; })()"), "farmer hammer null");
    });

    IT("keeps a key that names a prototype or a number a plain key of the copy", {
        Stage stage;
        EQUALS(stage.js.Eval("e.state.set('__proto__', { polluted: true }); e.state.set('10', 'ten'); 0"), "0");
        EQUALS(stage.js.Eval("(() => { const all = e.state.toObject(); return [Object.getPrototypeOf(all) === Object.prototype, Object.keys(all).join(','), all.polluted, all['__proto__'].polluted].join(' '); })()"), "true 10,__proto__  true");
        EQUALS(stage.lua.Eval("(function() local all = e.state:toObject() return tostring(all['10']) .. ' ' .. tostring(all[10]) .. ' ' .. tostring(all.__proto__.polluted) end)()"), "ten nil true");
    });

    IT("names the scope a key reaches, and refuses one it does not know", {
        Stage stage;
        EQUALS(stage.lua.Run("e.state:set('everyone', 1) e.state:set('mine', 1, { scope = 'owner' }) e.state:set('kept', 1, { scope = 'server' }) e.state:set('named', 1, { scope = 'broadcast' }) e.state:set('bare', 1, {})"), "");
        EQUALS(stage.entity->state.ScopeOf("everyone") == StateScope::Broadcast, true);
        EQUALS(stage.entity->state.ScopeOf("mine") == StateScope::Owner, true);
        EQUALS(stage.entity->state.ScopeOf("kept") == StateScope::Server, true);
        EQUALS(stage.entity->state.ScopeOf("named") == StateScope::Broadcast, true);
        EQUALS(stage.entity->state.ScopeOf("bare") == StateScope::Broadcast, true);
        EQUALS(stage.js.Eval("e.state.set('jsMine', 1, { scope: 'owner' })"), "true");
        EQUALS(stage.entity->state.ScopeOf("jsMine") == StateScope::Owner, true);

        EQUALS(Raised(stage.lua, "e.state:set('k', 1, 'owner')"), "StateBag.set: options must be an object");
        EQUALS(stage.js.Thrown("e.state.set('k', 1, 'owner')"), "TypeError: StateBag.set: options must be an object");
        EQUALS(Raised(stage.lua, "e.state:set('k', 1, { scope = 5 })"), "StateBag.set: scope must be a string");
        EQUALS(stage.js.Thrown("e.state.set('k', 1, { scope: 5 })"), "TypeError: StateBag.set: scope must be a string");
        EQUALS(Raised(stage.lua, "e.state:set('k', 1, { scope = 'everyone' })"), "StateBag.set: unknown scope 'everyone', expected 'broadcast', 'owner' or 'server'");
        EQUALS(stage.js.Thrown("e.state.set('k', 1, { scope: 'everyone' })"), "TypeError: StateBag.set: unknown scope 'everyone', expected 'broadcast', 'owner' or 'server'");
        // A scope that throws as it is read is not taken for no scope.
        EQUALS(stage.js.Thrown("e.state.set('k', 1, { get scope() { throw new RangeError('no scope'); } })"), "RangeError: no scope");
        EQUALS(stage.entity->state.Has("k"), false);
    });

    IT("raises the errors of JavaScript, under the same words", {
        Stage stage;
        for (const char *method : {"get", "has", "set", "remove"}) {
            const std::string expected = std::string("StateBag.") + method + ": expected a string key";
            EQUALS(Raised(stage.lua, std::string("e.state:") + method + "()"), expected);
            EQUALS(Raised(stage.lua, std::string("e.state:") + method + "(5)"), expected);
            EQUALS(stage.js.Thrown(std::string("e.state.") + method + "()"), "TypeError: " + expected);
            EQUALS(stage.js.Thrown(std::string("e.state.") + method + "(5)"), "TypeError: " + expected);
        }
        EQUALS(Raised(stage.lua, "e.state:set('k')"), "StateBag.set: expected (key, value, options?)");
        EQUALS(stage.js.Thrown("e.state.set('k')"), "TypeError: StateBag.set: expected (key, value, options?)");

        // The limits of a bag are the world's to refuse.
        const std::string tooLarge = std::string("StateBag.set: ") + NativeBag::WriteResultToString(NativeBag::WriteResult::ValueTooLarge);
        const std::string tooLong  = std::string("StateBag.set: ") + NativeBag::WriteResultToString(NativeBag::WriteResult::KeyTooLong);
        EQUALS(tooLarge, "StateBag.set: value is larger than 4096 bytes");
        EQUALS(stage.lua.Eval("e.state:set('fits', ('x'):rep(4096))"), "true");
        EQUALS(Raised(stage.lua, "e.state:set('big', ('x'):rep(4097))"), tooLarge);
        EQUALS(stage.js.Thrown("e.state.set('big', 'x'.repeat(4097))"), "Error: " + tooLarge);
        EQUALS(Raised(stage.lua, "local list = {} for i = 1, 3000 do list[i] = i end e.state:set('big', list)"), tooLarge);
        EQUALS(Raised(stage.lua, "e.state:set(('k'):rep(65), 1)"), tooLong);
        EQUALS(stage.js.Thrown("e.state.set('k'.repeat(65), 1)"), "Error: " + tooLong);
        EQUALS(stage.entity->state.Has("big"), false);

        // What has no JSON is refused, and nothing is stored.
        EQUALS(Raised(stage.lua, "local t = {} t.self = t e.state:set('loop', t)").starts_with("StateBag.set: "), true);
        EQUALS(stage.js.Thrown("const o = {}; o.self = o; e.state.set('loop', o)").starts_with("TypeError: StateBag.set: "), true);
        EQUALS(Raised(stage.lua, "e.state:set('loop', print)").starts_with("StateBag.set: "), true);
        EQUALS(stage.entity->state.Has("loop"), false);
        // The call that failed left nothing pending for the next one.
        EQUALS(stage.js.Eval("e.state.set('after', 1)"), "true");

        EQUALS(Raised(stage.lua, "e.state:onChange('k')"), "StateBag.onChange: expected (key, handler); pass null as the key to watch every key");
        EQUALS(Raised(stage.lua, "e.state:onChange(5, print)"), "StateBag.onChange: expected (key, handler); pass null as the key to watch every key");
        EQUALS(stage.js.Thrown("e.state.onChange('k')"), "TypeError: StateBag.onChange: expected (key, handler); pass null as the key to watch every key");
        EQUALS(Raised(stage.lua, "e.state:onChange('k', 5)"), "StateBag.onChange: handler must be a function");
        EQUALS(stage.js.Thrown("e.state.onChange(null, 5)"), "TypeError: StateBag.onChange: handler must be a function");
        EQUALS(StateBag::SubscriptionCount(), 0u);
    });

    IT("does nothing through a handle whose entity is gone, and still checks its arguments", {
        Stage stage;
        stage.lua.Run("bag = e.state bag:set('job', 'smith')");
        stage.js.Eval("globalThis.bag = e.state; 0");
        stage.world.Manager()->DestroyEntity(stage.entity);

        EQUALS(stage.lua.Eval("tostring(bag:get('job')) .. ' ' .. tostring(bag:has('job')) .. ' ' .. #bag:keys() .. ' ' .. tostring(next(bag:toObject())) .. ' ' .. tostring(bag:remove('job'))"), "nil false 0 nil false");
        EQUALS(stage.lua.Eval("select('#', bag:set('job', 'farmer')) .. select('#', bag:get('job'))"), "01");
        EQUALS(stage.js.Eval("[bag.get('job'), bag.has('job'), bag.keys().length, Object.keys(bag.toObject()).length, bag.remove('job'), bag.set('job', 'farmer')].join(' ')"), " false 0 0 false ");
        EQUALS(Raised(stage.lua, "bag:get()"), "StateBag.get: expected a string key");
        EQUALS(stage.js.Thrown("bag.set('k')"), "TypeError: StateBag.set: expected (key, value, options?)");

        // Called on what is no bag at all, a method is as quiet.
        EQUALS(stage.lua.Eval("tostring(bag.get({}, 'job'))"), "nil");
        EQUALS(stage.js.Eval("String(bag.get.call({}, 'job'))"), "undefined");
    });

    // ==================== StateBag: onChange ====================

    IT("tells a Lua handler the key, the value and the value before it", {
        Stage stage;
        EQUALS(stage.lua.Run("seen = {} local function spell(v) return type(v) == 'table' and 'table:' .. tostring(v.n) or tostring(v) end "
                             "stop = e.state:onChange('job', function(key, value, previous) seen[#seen + 1] = key .. '=' .. spell(value) .. '<' .. spell(previous) end) "
                             "all = {} e.state:onChange(nil, function(key) all[#all + 1] = key end)"),
            "");
        EQUALS(stage.lua.Eval("type(stop)"), "function");
        EQUALS(StateBag::SubscriptionCount(), 2u);

        EQUALS(stage.lua.Run("e.state:set('job', 'smith') e.state:set('job', 'smith') e.state:set('other', 1) e.state:set('job', { n = 4 }) e.state:set('job', nil) e.state:remove('job')"), "");
        // A stored nil is a value like the absence of one in Lua, which has the one nil.
        EQUALS(stage.lua.Eval("table.concat(seen, ' ')"), "job=smith<nil job=table:4<smith job=nil<table:4 job=nil<nil");
        EQUALS(stage.lua.Eval("table.concat(all, ' ')"), "job other job job job");

        // The function onChange gave ends the subscription, once.
        EQUALS(stage.lua.Run("stop() stop() e.state:set('job', 'farmer')"), "");
        EQUALS(StateBag::SubscriptionCount(), 1u);
        EQUALS(stage.lua.Eval("#seen .. ' ' .. #all"), "4 6");

        // A handler that raises does not undo the write that woke it, nor keep the others from hearing.
        EQUALS(stage.lua.Run("e.state:onChange('job', function() error('listener failed') end) late = 0 e.state:onChange('job', function() late = late + 1 end) e.state:set('job', 'miller')"), "");
        EQUALS(stage.lua.Eval("e.state:get('job') .. ' ' .. late"), "miller 1");
    });

    IT("wakes a Lua handler for a JavaScript write, and a JavaScript handler for a Lua write", {
        Stage stage;
        EQUALS(stage.lua.Run("heard = {} e.state:onChange(nil, function(key, value, previous) heard[#heard + 1] = key .. ':' .. (type(value) == 'table' and value.tools[2] or tostring(value)) .. ':' .. tostring(previous) end)"), "");
        EQUALS(stage.js.Eval("globalThis.heard = []; e.state.onChange(null, (key, value, previous) => heard.push(key + ':' + JSON.stringify(value) + ':' + JSON.stringify(previous))); 0"), "0");
        EQUALS(StateBag::SubscriptionCount(), 2u);

        EQUALS(stage.js.Eval("e.state.set('job', 'smith'); e.state.set('doc', { tools: ['hammer', 'tongs'] }); e.state.set('job', null); e.state.remove('job'); 0"), "0");
        EQUALS(stage.lua.Eval("table.concat(heard, ' ')"), "job:smith:nil doc:tongs:nil job:nil:smith job:nil:nil");
        // JavaScript tells a stored null from a key that is gone or was not there.
        EQUALS(stage.js.Eval("heard.join(' ')"), R"(job:"smith":undefined doc:{"tools":["hammer","tongs"]}:undefined job:null:"smith" job:undefined:null)");

        EQUALS(stage.js.Eval("heard.length = 0; 0"), "0");
        EQUALS(stage.lua.Run("e.state:set('crops', { 'wheat', 'rye' }) e.state:set('acres', 12) e.state:set('where', Vector3.new(1, 2, 3))"), "");
        EQUALS(stage.js.Eval("heard.join(' ')"), R"(crops:["wheat","rye"]:undefined acres:12:undefined where:{"x":1,"y":2,"z":3}:undefined)");

        // A write made by the host itself wakes both.
        StateValue text;
        text.type = StateValue::Type::String;
        text.text = "host";
        stage.entity->state.Set("from", text);
        EQUALS(stage.lua.Eval("heard[#heard]"), "from:host:nil");
        EQUALS(stage.js.Eval("heard[heard.length - 1]"), R"(from:"host":undefined)");
    });

    IT("lets a handler end its own subscription, and write the bag it watches", {
        Stage stage;
        EQUALS(stage.lua.Run("count = 0 local stop stop = e.state:onChange('once', function() count = count + 1 stop() end) e.state:set('once', 1) e.state:set('once', 2)"), "");
        EQUALS(stage.lua.Eval("count"), "1");
        EQUALS(stage.js.Eval("globalThis.count = 0; const stop = e.state.onChange('jsOnce', () => { count++; stop(); }); e.state.set('jsOnce', 1); e.state.set('jsOnce', 2); count"), "1");
        EQUALS(StateBag::SubscriptionCount(), 0u);

        EQUALS(stage.lua.Run("e.state:onChange('a', function(_, value) e.state:set('b', value * 2) end) e.state:set('a', 21)"), "");
        EQUALS(stage.lua.Eval("e.state:get('b')"), "42");
    });

    IT("lets a handler write the key it watches, end another subscription, and take the entity away", {
        Stage stage;
        // A write of the watched key wakes the handler again, inside itself.
        EQUALS(stage.lua.Run("seen = {} e.state:onChange('n', function(_, value) seen[#seen + 1] = value if value < 3 then e.state:set('n', value + 1) end end) e.state:set('n', 1)"), "");
        EQUALS(stage.lua.Eval("table.concat(seen, ',') .. ' ' .. e.state:get('n')"), "1,2,3 3");
        EQUALS(stage.js.Eval("globalThis.seen = []; e.state.onChange('m', (_, value) => { seen.push(value); if (value < 3) e.state.set('m', value + 1); }); e.state.set('m', 1); seen.join(',') + ' ' + e.state.get('m')"), "1,2,3 3");
        // A JavaScript handler writes another key, which a Lua handler hears of.
        EQUALS(stage.lua.Run("doubled = nil e.state:onChange('twice', function(_, value) doubled = value end)"), "");
        EQUALS(stage.js.Eval("e.state.onChange('once', (_, value) => e.state.set('twice', value * 2)); e.state.set('once', 21); e.state.get('twice')"), "42");
        EQUALS(stage.lua.Eval("doubled"), "42");

        // The first handler ends the subscription of the second, which is not called for the change under way.
        EQUALS(stage.lua.Run("first, second = 0, 0 local stopSecond e.state:onChange('pair', function() first = first + 1 stopSecond() end) stopSecond = e.state:onChange('pair', function() second = second + 1 end) e.state:set('pair', 1) e.state:set('pair', 2)"), "");
        EQUALS(stage.lua.Eval("first + second"), "2");
        EQUALS(stage.js.Eval("globalThis.first = 0; globalThis.second = 0; let stopSecond; e.state.onChange('jsPair', () => { first++; stopSecond(); }); stopSecond = e.state.onChange('jsPair', () => second++); e.state.set('jsPair', 1); e.state.set('jsPair', 2); first + second"),
            "2");

        // A handler that takes the entity away leaves handles that do nothing.
        InstallDestroyEntity(stage);
        EQUALS(stage.lua.Run("after = 'unset' e.state:onChange('gone', function() destroyEntity() after = tostring(e.state:get('gone')) .. tostring(e.state:has('gone')) end)"), "");
        EQUALS(stage.js.Eval("globalThis.heardGone = 0; e.state.onChange('gone', () => heardGone++); 0"), "0");
        EQUALS(stage.lua.Run("e.state:set('gone', 1)"), "");
        EQUALS(stage.lua.Eval("after"), "nilfalse");
        EQUALS(stage.world.Manager()->GetEntityByNetworkID(std::stoull(stage.id)) == nullptr, true);
        EQUALS(stage.lua.Eval("select('#', e.state:set('gone', 2))"), "0");
    });

    IT("pins what JavaScript is refused when a value has no JSON form", {
        Stage stage;
        // A function was once stored as the text "undefined"; it is refused now, as in Lua.
        EQUALS(stage.js.Thrown("e.state.set('fn', () => {})"), "TypeError: StateBag.set: a value of this kind has no JSON form");
        EQUALS(stage.js.Thrown("e.state.set('sym', Symbol('s'))"), "TypeError: StateBag.set: a value of this kind has no JSON form");
        EQUALS(stage.js.Thrown("const o = {}; o.self = o; e.state.set('loop', o)").starts_with("TypeError: StateBag.set: Converting circular structure to JSON"), true);
        // What a toJSON throws is the message of the TypeError.
        EQUALS(stage.js.Thrown("e.state.set('custom', { toJSON() { throw new RangeError('no json today'); } })"), "TypeError: StateBag.set: no json today");
        EQUALS(stage.entity->state.Size(), size_t(0));
        // A function inside a document is left out, as JSON.stringify leaves it out.
        EQUALS(stage.js.Eval("e.state.set('doc', { keep: 1, drop() {} }); JSON.stringify(e.state.get('doc'))"), R"({"keep":1})");

        // The conversion the host is offered is the one set runs.
        std::string refused = "converted";
        stage.js.js.Enter([&refused](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            v8::Local<v8::Value> function = v8::Function::New(context, [](const v8::FunctionCallbackInfo<v8::Value> &) {}).ToLocalChecked();
            v8::TryCatch tryCatch(isolate);
            StateValue out;
            if (!StateBag::ToStateValue(isolate, context, function, out) && tryCatch.HasCaught()) {
                v8::String::Utf8Value message(isolate, tryCatch.Exception());
                refused = *message ? *message : "";
            }
        });
        EQUALS(refused, "TypeError: StateBag.set: a value of this kind has no JSON form");
    });

    IT("copies a key that is not UTF-8 once", {
        Stage stage;
        EQUALS(stage.lua.Run("e.state:set('\\255key', 1) e.state:set('plain', 2)"), "");
        EQUALS(stage.lua.Eval("(function() local all, count = e.state:toObject(), 0 for _ in pairs(all) do count = count + 1 end return count .. ' ' .. tostring(all['\\255key']) .. ' ' .. tostring(all.plain) end)()"), "2 1 2");
        EQUALS(stage.js.Eval("Object.keys(e.state.toObject()).length + ' ' + e.state.keys().length"), "2 2");
    });

    // ==================== StateBag: failing handlers ====================

    IT("reports the failure of a handler to the manager of its resource, which acts on it at its next upkeep", {
        Realm realm;
        realm.Resource("js-sync", "main.js", "globalThis.calls = 0; new Entity(ENTITY).state.onChange('job', () => { calls++; throw new Error('sync boom'); });");
        realm.Resource("js-async", "main.js", "globalThis.calls = 0; new Entity(ENTITY).state.onChange('job', async () => { calls++; await new Promise((resolve) => setTimeout(resolve, 5)); throw new Error('async boom'); });");
        realm.Resource("lua-sync", "main.lua", "calls = 0 Entity.new(ENTITY).state:onChange('job', function() calls = calls + 1 error('lua boom') end)");
        realm.Resource("lua-async", "main.lua", "calls = 0 Entity.new(ENTITY).state:onChange('job', function() calls = calls + 1 sleep(5) error('lua late boom') end)");
        realm.Resource("quiet", "main.lua", "calls = 0 Entity.new(ENTITY).state:onChange('job', function() calls = calls + 1 end)");
        // The writer is not thrown at for what its listeners do.
        realm.Resource("writer", "main.js", "globalThis.write = (value) => { try { return String(new Entity(ENTITY).state.set('job', value)); } catch (e) { return 'threw ' + e.message; } };");
        EQUALS(realm.Start(), true);
        EQUALS(StateBag::SubscriptionCount(), 5u);

        // Nothing is done about a failure inside the write that woke the handler.
        EQUALS(realm.Js("writer", "write('smith')"), "true");
        EQUALS(realm.errors.empty(), true);

        // Those that failed in the call, and those that failed after a wait, are errors of their resources once
        // the manager has had its turn.
        EQUALS(realm.UpdateUntilErrors(4), true);
        EQUALS(realm.ErrorOf("js-sync").starts_with("State change listener for 'job' failed: "), true);
        EQUALS(LuaRuntimeTest::Contains(realm.ErrorOf("js-sync"), "sync boom"), true);
        EQUALS(realm.ErrorOf("lua-sync").starts_with("State change listener for 'job' failed: "), true);
        EQUALS(LuaRuntimeTest::Contains(realm.ErrorOf("lua-sync"), "lua boom"), true);
        EQUALS(realm.ErrorOf("js-async").starts_with("State change listener for 'job' failed: "), true);
        EQUALS(LuaRuntimeTest::Contains(realm.ErrorOf("js-async"), "async boom"), true);
        EQUALS(realm.ErrorOf("lua-async").starts_with("State change listener for 'job' failed: "), true);
        EQUALS(LuaRuntimeTest::Contains(realm.ErrorOf("lua-async"), "lua late boom"), true);
        EQUALS(realm.ErrorOf("quiet") + realm.ErrorOf("writer"), "<none><none>");
        EQUALS(realm.errors.size(), 4u);

        // As errorBehavior continue says, each goes on listening.
        realm.errors.clear();
        EQUALS(realm.Js("writer", "write('farmer')"), "true");
        EQUALS(realm.UpdateUntilErrors(4), true);
        EQUALS(realm.Js("js-sync", "calls") + realm.Js("js-async", "calls") + realm.Lua("lua-sync", "calls") + realm.Lua("lua-async", "calls") + realm.Lua("quiet", "calls"), "22222");
        EQUALS(StateBag::SubscriptionCount(), 5u);
        EQUALS(realm.stopped.empty(), true);
    });

    IT("stops a resource whose handler fails, as errorBehavior stop says, after the write that woke it", {
        Realm realm;
        realm.Resource("js-fragile", "main.js", "new Entity(ENTITY).state.onChange('job', () => { throw new Error('boom'); });", "stop");
        realm.Resource("lua-fragile", "main.lua", "Entity.new(ENTITY).state:onChange('job', function() error('boom') end)", "stop");
        realm.Resource("js-late", "main.js", "new Entity(ENTITY).state.onChange('job', async () => { await new Promise((resolve) => setTimeout(resolve, 5)); throw new Error('late boom'); });", "stop");
        realm.Resource("lua-late", "main.lua", "Entity.new(ENTITY).state:onChange('job', function() sleep(5) error('late boom') end)", "stop");
        // A resource whose own write wakes its own failing handler goes on to the end of its script.
        realm.Resource("js-self", "main.js", "const e = new Entity(ENTITY); e.state.onChange('mine', () => { throw new Error('own boom'); }); globalThis.write = () => String(e.state.set('mine', 1)) + ' ' + String(e.state.set('mine', 2));", "stop");
        realm.Resource("lua-self", "main.lua", "local e = Entity.new(ENTITY) e.state:onChange('theirs', function() error('own boom') end) function write() return tostring(e.state:set('theirs', 1)) .. ' ' .. tostring(e.state:set('theirs', 2)) end", "stop");
        realm.Resource("sturdy", "main.lua", "calls = 0 Entity.new(ENTITY).state:onChange(nil, function() calls = calls + 1 end)");
        EQUALS(realm.Start(), true);
        EQUALS(StateBag::SubscriptionCount(), 7u);
        auto *manager = realm.module.GetResourceManager();

        realm.Write("job", 1);
        EQUALS(realm.Js("js-self", "write()"), "true true");
        EQUALS(realm.Lua("lua-self", "write()"), "true true");
        // Nothing changed state inside a write.
        EQUALS(realm.errors.empty() && realm.stopped.empty(), true);
        EQUALS(StateBag::SubscriptionCount(), 7u);

        // Several failures of one resource retire it once.
        EQUALS(realm.UpdateUntilErrors(6), true);
        for (const char *name : {"js-fragile", "lua-fragile", "js-late", "lua-late", "js-self", "lua-self"}) {
            EQUALS(manager->IsResourceRunning(name), false);
            EQUALS(std::count(realm.stopped.begin(), realm.stopped.end(), name), 1);
            EQUALS(std::count_if(realm.errors.begin(), realm.errors.end(),
                       [name](const std::string &error) {
                           return error.starts_with(std::string(name) + ": ");
                       }),
                1);
        }
        EQUALS(StateBag::SubscriptionCount(), 1u);

        realm.Write("job", 2);
        realm.module.Update();
        EQUALS(realm.errors.size(), 6u);
        EQUALS(realm.Lua("sturdy", "calls"), "6");
    });

    IT("restarts a resource whose handler fails later, as errorBehavior restart says", {
        Realm realm;
        realm.Resource("js-phoenix", "main.js", "new Entity(ENTITY).state.onChange('job', async () => { await new Promise((resolve) => setTimeout(resolve, 5)); throw new Error('late boom'); });", "restart");
        realm.Resource("lua-phoenix", "main.lua", "Entity.new(ENTITY).state:onChange('job', function() sleep(5) error('late boom') end)", "restart");
        EQUALS(realm.Start(), true);
        auto *manager = realm.module.GetResourceManager();

        realm.Write("job", 1);
        EQUALS(realm.errors.empty(), true);
        EQUALS(realm.UpdateUntilErrors(2), true);
        EQUALS(realm.stopped.size(), 2u);

        // Each comes back, with the handler it registers as it starts.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (!(manager->IsResourceRunning("js-phoenix") && manager->IsResourceRunning("lua-phoenix")) && std::chrono::steady_clock::now() < deadline) {
            realm.module.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EQUALS(manager->IsResourceRunning("js-phoenix") && manager->IsResourceRunning("lua-phoenix"), true);
        EQUALS(StateBag::SubscriptionCount(), 2u);
        EQUALS(realm.errors.size(), 2u);
    });

    IT("lets a resource stop when a handler of it fails while it stops, whatever errorBehavior says", {
        Realm realm;
        // Its resourceStop handler writes, which wakes its own failing handler in the middle of the stop.
        realm.Resource("leaver", "main.js", "const e = new Entity(ENTITY); globalThis.heard = 0; e.state.onChange('bye', () => { heard++; throw new Error('boom on the way out'); }); Events.on('resourceStop', (name) => { if (name === 'leaver') e.state.set('bye', 1); });",
            "restart");
        realm.Resource("witness", "main.lua", "byes = 0 Entity.new(ENTITY).state:onChange('bye', function() byes = byes + 1 end)");
        EQUALS(realm.Start(), true);
        auto *manager = realm.module.GetResourceManager();

        EQUALS(static_cast<bool>(manager->StopResource("leaver")), true);
        // The handler did run, and failed, during the stop.
        EQUALS(realm.Lua("witness", "byes"), "1");
        EQUALS(manager->GetResourceState("leaver") == ResourceState::Stopped, true);
        EQUALS(std::count(realm.stopped.begin(), realm.stopped.end(), "leaver"), 1);

        // No upkeep takes the failure for a reason to restart what the operator stopped.
        for (int i = 0; i < 40; ++i) {
            realm.module.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
        while (std::chrono::steady_clock::now() < deadline) {
            realm.module.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        EQUALS(manager->GetResourceState("leaver") == ResourceState::Stopped, true);
        EQUALS(std::count(realm.stopped.begin(), realm.stopped.end(), "leaver"), 1);
        EQUALS(realm.errors.empty(), true);
        EQUALS(StateBag::SubscriptionCount(), 1u);
    });

    IT("acts on a reported failure only for a resource that is running by then", {
        Realm realm;
        realm.Resource("steady", "main.lua", "x = 1", "stop");
        realm.Resource("halted", "main.lua", "x = 1", "restart");
        EQUALS(realm.Start(), true);
        auto *manager = realm.module.GetResourceManager();

        // Reported, then stopped before the manager's turn: dropped with the stop.
        manager->ReportHandlerFailure("halted", "handler failed");
        EQUALS(static_cast<bool>(manager->StopResource("halted")), true);
        // Reported for a resource that is stopped, and for one the manager does not know: dropped at its turn.
        manager->ReportHandlerFailure("halted", "handler failed again");
        manager->ReportHandlerFailure("nobody", "handler failed");
        manager->ProcessHandlerFailures();
        EQUALS(realm.errors.empty(), true);
        EQUALS(manager->GetResourceState("halted") == ResourceState::Stopped, true);
        EQUALS(realm.stopped.size(), 1u);

        // Two reports for a running resource: the first retires it, the second finds it no longer running.
        manager->ReportHandlerFailure("steady", "first");
        manager->ReportHandlerFailure("steady", "second");
        EQUALS(manager->IsResourceRunning("steady"), true);
        manager->ProcessHandlerFailures();
        EQUALS(realm.errors.size(), 1u);
        EQUALS(realm.ErrorOf("steady"), "first");
        EQUALS(manager->IsResourceRunning("steady"), false);
        EQUALS(std::count(realm.stopped.begin(), realm.stopped.end(), "steady"), 1);
        manager->ProcessHandlerFailures();
        EQUALS(realm.errors.size(), 1u);
    });

    IT("drops the subscriptions of a resource its manager stops, also from inside its own handler", {
        Realm realm;
        realm.Resource("lua-watcher", "main.lua", "calls = 0 local e = Entity.new(ENTITY) e.state:onChange(nil, function() calls = calls + 1 end) e.state:onChange('job', function() end)");
        realm.Resource("js-watcher", "main.js", "globalThis.calls = 0; const e = new Entity(ENTITY); e.state.onChange(null, () => calls++); e.state.onChange('job', () => {});");
        realm.Resource("lua-quitter", "main.lua", "Entity.new(ENTITY).state:onChange('quit', function() stop('lua-quitter') end)");
        realm.Resource("js-quitter", "main.js", "new Entity(ENTITY).state.onChange('quit', () => stop('js-quitter'));");
        realm.Resource("sturdy", "main.lua", "calls = 0 Entity.new(ENTITY).state:onChange(nil, function() calls = calls + 1 end)");
        EQUALS(realm.Start(), true);
        EQUALS(StateBag::SubscriptionCount(), 7u);

        // Through the manager, as the server stops a resource: nothing here calls CleanupResource itself.
        EQUALS(static_cast<bool>(realm.module.GetResourceManager()->StopResource("lua-watcher")), true);
        EQUALS(StateBag::SubscriptionCount(), 5u);
        EQUALS(static_cast<bool>(realm.module.GetResourceManager()->StopResource("js-watcher")), true);
        EQUALS(StateBag::SubscriptionCount(), 3u);

        // A handler that stops its own resource: stopping is no failure, and nothing of it is left.
        realm.Write("quit", 1);
        EQUALS(realm.module.GetResourceManager()->IsResourceRunning("lua-quitter"), false);
        EQUALS(realm.module.GetResourceManager()->IsResourceRunning("js-quitter"), false);
        EQUALS(StateBag::SubscriptionCount(), 1u);
        realm.module.Update();
        realm.Write("quit", 2);
        EQUALS(realm.errors.empty(), true);
        EQUALS(realm.Lua("sturdy", "calls"), "2");
    });

    IT("keeps the subscriptions of one manager when another stops a resource of the same name", {
        Stage stage;
        ResourceManager other(&stage.js.js.engine);
        EQUALS(stage.lua.Run("e.state:onChange(nil, function() end)"), "");
        EQUALS(stage.js.Eval("e.state.onChange(null, () => {}); 0"), "0");
        EQUALS(StateBag::SubscriptionCount(), 2u);

        StateBag::CleanupResource(&other, kLuaResource);
        StateBag::CleanupResource(&other, kJsResource);
        StateBag::CleanupResource(static_cast<const ResourceManager *>(nullptr), kJsResource);
        EQUALS(StateBag::SubscriptionCount(), 2u);
        // With no JavaScript engine in sight, the manager and the name are enough.
        StateBag::CleanupResource(&stage.js.manager, kLuaResource);
        EQUALS(StateBag::SubscriptionCount(), 1u);

        // A manager that goes takes what was made under it.
        StateBag::ForgetManager(&stage.js.manager);
        EQUALS(StateBag::SubscriptionCount(), 0u);
    });

    // ==================== StateBag: cleanup ====================

    IT("drops the subscriptions of a resource when it stops, in either language", {
        Stage stage;
        EQUALS(stage.lua.Run("calls = 0 e.state:onChange(nil, function() calls = calls + 1 end)"), "");
        EQUALS(stage.js.Eval("globalThis.calls = 0; e.state.onChange(null, () => calls++); e.state.onChange('job', () => calls++); 0"), "0");
        stage.js.manager.SetCurrentResourceContext("other");
        EQUALS(stage.js.Eval("globalThis.others = 0; e.state.onChange(null, () => others++); 0"), "0");
        EQUALS(StateBag::SubscriptionCount(), 4u);

        // The entry point the resource manager calls for a resource that stops.
        StateBag::CleanupResource(stage.js.js.Isolate(), kJsResource);
        EQUALS(StateBag::SubscriptionCount(), 2u);
        EQUALS(stage.lua.Run("e.state:set('job', 'smith')"), "");
        EQUALS(stage.js.Eval("calls + ' ' + others"), "0 1");
        EQUALS(stage.lua.Eval("calls"), "1");

        StateBag::CleanupResource(stage.js.js.Isolate(), kLuaResource);
        EQUALS(StateBag::SubscriptionCount(), 1u);
        EQUALS(stage.js.Eval("e.state.set('job', 'farmer'); others"), "2");
        EQUALS(stage.lua.Eval("calls"), "1");

        StateBag::CleanupResource(stage.js.js.Isolate(), "other");
        EQUALS(StateBag::SubscriptionCount(), 0u);
    });

    IT("drops the subscriptions of a Lua runtime that goes", {
        Stage stage;
        EQUALS(stage.lua.Run("e.state:onChange(nil, function() end) e.state:onChange('job', function() end)"), "");
        EQUALS(stage.js.Eval("globalThis.calls = 0; e.state.onChange(null, () => calls++); 0"), "0");
        EQUALS(StateBag::SubscriptionCount(), 3u);

        stage.lua.engine.DestroyResourceRuntime(kLuaResource);
        EQUALS(StateBag::SubscriptionCount(), 1u);
        EQUALS(stage.js.Eval("e.state.set('job', 'smith'); calls"), "1");

        // The next runtime of the resource starts with none.
        EQUALS(stage.lua.engine.CreateResourceRuntime(kLuaResource), true);
        EQUALS(stage.lua.Run("e = Entity.new(" + stage.id + ") heard = 0 e.state:onChange(nil, function() heard = heard + 1 end) e.state:set('job', 'farmer')"), "");
        EQUALS(stage.lua.Eval("heard"), "1");
        EQUALS(StateBag::SubscriptionCount(), 2u);
    });

    IT("drops the subscriptions of an isolate that goes, and never calls a dead handler", {
        TextLabelWorld world;
        auto *entity         = world.Manager()->CreateEntity(world.targetType);
        const std::string id = std::to_string(entity->GetNetworkID());
        LuaSide lua;
        lua.Run("e = Entity.new(" + id + ") calls = 0 e.state:onChange(nil, function() calls = calls + 1 end)");
        {
            JsSide js;
            EQUALS(js.Eval("const e = new Entity(" + id + "); e.state.onChange(null, () => {}); e.state.onChange('job', () => {}); 0"), "0");
            EQUALS(StateBag::SubscriptionCount(), 3u);
        }
        EQUALS(StateBag::SubscriptionCount(), 1u);
        EQUALS(lua.Run("e.state:set('job', 'smith')"), "");
        EQUALS(lua.Eval("calls"), "1");

        // UnregisterIsolate alone, as a host that keeps the isolate's other classes calls it.
        JsSide js;
        EQUALS(js.Eval("const e = new Entity(" + id + "); e.state.onChange(null, () => {}); 0"), "0");
        EQUALS(StateBag::SubscriptionCount(), 2u);
        StateBag::UnregisterIsolate(js.js.Isolate());
        EQUALS(StateBag::SubscriptionCount(), 1u);
    });

    IT("refuses a subscription nobody owns", {
        Stage stage;
        stage.js.manager.SetCurrentResourceContext("");
        EQUALS(stage.js.Thrown("e.state.onChange(null, () => {})"), "Error: StateBag.onChange: must be called from within a resource");
        EQUALS(StateBag::SubscriptionCount(), 0u);

        // Without replication there is nothing to watch.
        Framework::CoreModules::SetReplication(nullptr);
        EQUALS(Raised(stage.lua, "e.state:onChange(nil, function() end)"), "StateBag.onChange: replication is not available");
        Framework::CoreModules::SetReplication(stage.world.Manager());
    });

    // ==================== Crossing runtimes ====================

    IT("carries a handle to another runtime by its id, in both languages", {
        Builtins::RegisterTransferTypes();
        Stage stage;
        lua_State *L = stage.lua.Runtime().State();

        // Lua to JavaScript.
        for (const char *expression : {"e", "e.state"}) {
            const std::string chunk = std::string("return ") + expression;
            EQUALS(luaL_loadstring(L, chunk.c_str()) == LUA_OK && lua_pcall(L, 0, 1, 0) == LUA_OK, true);
            auto copied = LuaTransfer::Copy(L, -1);
            lua_pop(L, 1);
            EQUALS(static_cast<bool>(copied), true);
            EQUALS(copied.GetValue().kind == TransferredValue::Kind::HostObject, true);
            stage.js.js.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                v8::Local<v8::Value> rebuilt;
                if (ValueTransfer::Rebuild(isolate, context, copied.GetValue()).ToLocal(&rebuilt)) {
                    context->Global()->Set(context, v8pp::to_v8(isolate, "arrived"), rebuilt).Check();
                }
            });
        }
        // The bag arrived last.
        EQUALS(stage.lua.Run("e.state:set('job', 'smith')"), "");
        EQUALS(stage.js.Eval("arrived.get('job')"), "smith");

        // JavaScript to Lua.
        for (const char *expression : {"e.state", "e"}) {
            auto copied = Framework::Utils::Result<TransferredValue, std::string>::Err("not copied");
            stage.js.js.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                v8::Local<v8::Value> value;
                if (v8::Script::Compile(context, v8pp::to_v8(isolate, expression)).ToLocalChecked()->Run(context).ToLocal(&value)) {
                    copied = ValueTransfer::Copy(isolate, context, value);
                }
            });
            EQUALS(static_cast<bool>(copied), true);
            EQUALS(copied.GetValue().kind == TransferredValue::Kind::HostObject, true);
            std::string error;
            EQUALS(LuaTransfer::Rebuild(L, copied.GetValue(), error), true);
            lua_setglobal(L, "arrived");
            if (std::string(expression) == "e.state") {
                EQUALS(stage.lua.Eval("arrived:get('job')"), "smith");
            }
        }
        EQUALS(stage.lua.Eval("arrived.id == e.id and arrived:toString()"), "Entity{ id: " + stage.id + " }");

        // A handle whose entity went on the way does not arrive.
        EQUALS(luaL_loadstring(L, "return e") == LUA_OK && lua_pcall(L, 0, 1, 0) == LUA_OK, true);
        auto late = LuaTransfer::Copy(L, -1);
        lua_pop(L, 1);
        stage.world.Manager()->DestroyEntity(stage.entity);
        std::string error;
        EQUALS(LuaTransfer::Rebuild(L, late.GetValue(), error), false);
        EQUALS(LuaRuntimeTest::Contains(error, "Entity handle '" + stage.id + "' is not valid!"), true);
    });
})
