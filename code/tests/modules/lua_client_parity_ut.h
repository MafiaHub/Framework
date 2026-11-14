/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_client_ut.h"
#include "lua_parity_helpers.h"
#include "scripting_catalog_golden_ut.h"

#include "integrations/client/scripting/builtins/builtins.h"
#include "integrations/client/scripting/builtins/discord.h"
#include "integrations/client/scripting/builtins/keybinds.h"
#include "integrations/client/scripting/builtins/nametags.h"
#include "integrations/client/scripting/builtins/web.h"
#include "scripting/builtins/builtins.h"
#include "scripting/builtins/console.h"
#include "scripting/builtins/entity.h"
#include "scripting/builtins/execution_environment.h"
#include "scripting/builtins/exports.h"
#include "scripting/builtins/imports.h"
#include "scripting/builtins/messages.h"
#include "scripting/builtins/player.h"
#include "scripting/builtins/state_bag.h"
#include "scripting/builtins/text_label.h"
#include "scripting/engine_scope.h"
#include "scripting/event_metadata.h"
#include "scripting/timer_metadata.h"
#include "scripting/v8_engine.h"

// The acceptance gate of Lua scripting on a client (lua_parity_ut.h has the server's): everything the JavaScript
// catalog of a client documents is there in a Lua state set up as ClientScriptingModule sets it up, and so is
// everything the Lua definition file of a client declares. The module itself builds on Windows only; what it
// registers in its isolate, what it adds to every Lua state and what it writes its definition file from are the
// functions of integrations/client/scripting/builtins/builtins.h, which are what is used here.
namespace LuaClientParityTest {
    // What the client builtins keep is let go of in the order of ClientScriptingModule::Shutdown: what names the
    // manager before the manager goes, what holds V8 globals after the Lua host and before the engine.
    inline void ShutdownManagerBuiltins() {
        Framework::Integrations::Client::Scripting::Builtins::Web::Shutdown();
        Framework::Integrations::Client::Scripting::Builtins::Keybinds::Shutdown();
    }

    inline void ShutdownIsolateBuiltins() {
        Framework::Integrations::Client::Scripting::Builtins::Discord::Shutdown();
        Framework::Integrations::Client::Scripting::Builtins::Nametags::Shutdown();
    }

    // What a client records for JavaScript: what ClientScriptingModule::RegisterFrameworkBindings registers,
    // through the two functions it registers it with, and the handle classes a game publishes on top of it.
    // Normalised; null when the engine does not start.
    inline nlohmann::json BuildCatalog() {
        Framework::Scripting::V8Engine engine;
        if (engine.Init() != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
            return nullptr;
        }
        nlohmann::json catalog;
        {
            Framework::Scripting::ResourceManagerConfig config;
            config.isClient = true;
            Framework::Scripting::ResourceManager manager(&engine, config);
            {
                Framework::Scripting::EngineScope scope(&engine);
                v8::Isolate *isolate                 = scope.Isolate();
                const v8::Local<v8::Context> context = scope.Context();
                const v8::Local<v8::Object> global   = context->Global();
                Framework::Scripting::SetScriptingCatalog(isolate, "parity-client");
                v8pp::metadata::registry &registry = Framework::Scripting::GetScriptingCatalog(isolate);

                Framework::Scripting::Bind::Registrar registrar(isolate, context, global, Framework::Scripting::Bind::Side::Client);
                Framework::Integrations::Client::Scripting::Builtins::RegisterSharedBuiltins(registrar, manager, "");
                Framework::Integrations::Client::Scripting::Builtins::RegisterClientBuiltins(registrar, Framework::Scripting::Bind::Languages::All, &manager);

                Framework::Scripting::Builtins::Entity::GetClass(isolate);
                Framework::Scripting::Builtins::Player::GetClass(isolate);
                Framework::Scripting::Builtins::StateBag::GetClass(isolate);
                Framework::Scripting::Builtins::TextLabel::Register(registrar);

                catalog = nlohmann::json::parse(v8pp::metadata::to_json(registry), nullptr, /*allow_exceptions*/ false);
            }
            ShutdownManagerBuiltins();
        }
        ShutdownIsolateBuiltins();
        engine.Shutdown();
        return ScriptingCatalogGolden::Normalise(std::move(catalog));
    }

    // The scripting of a client with its builtins in every Lua state, and one Lua resource, `lua`, started.
    struct Stage final {
        LuaClientTest::Client client;
        bool ready = false;

        Stage() {
            Framework::Scripting::Bind::Registrar registrar;
            Framework::Integrations::Client::Scripting::Builtins::RegisterClientBuiltins(registrar);
            client.lua.AddRuntimeSetup([bindings = registrar.Lua()](Framework::Scripting::LuaRuntime &runtime) {
                bindings.Apply(runtime);
            });
            ready = client.Init() && LuaClientTest::Mount("lua", R"("clientScripts":["client.lua"])", {{"client.lua", ""}}).empty() && client.Start("lua").empty();
        }

        ~Stage() {
            if (client.manager) {
                client.manager->StopAll();
            }
            ShutdownManagerBuiltins();
            client.manager.reset();
            client.lua.Shutdown();
            ShutdownIsolateBuiltins();
            client.v8.Shutdown();
            Framework::Utils::Vfs::Get().Unmount("lua.zip");
        }

        lua_State *State() {
            Framework::Scripting::LuaRuntime *runtime = client.lua.GetEngine().GetRuntime("lua");
            return runtime != nullptr ? runtime->State() : nullptr;
        }
    };

    // What a client gives JavaScript and not Lua, each with its reason: those of a server (lua_parity_ut.h) that a
    // client has too.
    inline const LuaParity::Exemptions &Exemptions() {
        static const LuaParity::Exemptions exemptions = {
            {"queueMicrotask", "JavaScript's own queue"},
            {"StateBag", "not a global of JavaScript either"},
            {"Vector2.toJSON", "the protocol of JSON.stringify"},
            {"Vector3.toJSON", "the protocol of JSON.stringify"},
            {"Vector4.toJSON", "the protocol of JSON.stringify"},
            {"Quaternion.toJSON", "the protocol of JSON.stringify"},
            {"Color.toJSON", "the protocol of JSON.stringify"},
        };
        return exemptions;
    }
} // namespace LuaClientParityTest

MODULE(lua_client_parity, {
    IT("gives Lua every member of every global the client catalog documents for JavaScript", {
        const nlohmann::json catalog = LuaClientParityTest::BuildCatalog();
        EQUALS(catalog.is_object(), true);

        LuaClientParityTest::Stage stage;
        EQUALS(stage.ready, true);
        lua_State *L = stage.State();
        NEQUALS(L, nullptr);

        const auto builtins = Framework::Integrations::Client::Scripting::Builtins::ClientLuaDefinitions();
        LuaParity::Gate gate(L, Framework::Scripting::Bind::Side::Client, builtins, LuaClientParityTest::Exemptions());
        const LuaParity::Report report = gate.Check(catalog);

        EQUALS(LuaParity::Join(report.missing), "");
        EQUALS(LuaParity::Join(report.stale), "");
        // The whole catalog: 12 global objects and 9 classes. The data types are
        // EventMap, TextLabelOptions and the twelve payloads of the browser events.
        EQUALS(report.symbols, 21);
        EQUALS(report.dataTypes, 14);
        EQUALS(report.members >= 250, true);
        EQUALS(static_cast<size_t>(report.exempted), LuaClientParityTest::Exemptions().size());
        EQUALS(ScriptingCatalogGolden::HasSymbol(catalog, "Nametags"), true);
        // A builtin registered for one language alone.
        EQUALS(LuaParity::Join(LuaParity::OneSided(catalog, builtins, Framework::Scripting::Bind::Side::Client)), "");
    });

    IT("has in a Lua state everything the client definition file declares, and declares every global", {
        LuaClientParityTest::Stage stage;
        EQUALS(stage.ready, true);
        lua_State *L = stage.State();
        NEQUALS(L, nullptr);

        const auto builtins                = Framework::Integrations::Client::Scripting::Builtins::ClientLuaDefinitions();
        const std::string definitions      = LuaParity::Generate(builtins, Framework::Scripting::Bind::Side::Client);
        const LuaParity::Declared declared = LuaParity::CheckDefinitions(definitions, L, Framework::Scripting::Bind::Side::Client, builtins);

        EQUALS(LuaParity::CompileError(definitions), "");
        EQUALS(LuaParity::Join(declared.missing), "");
        EQUALS(LuaParity::Join(LuaParity::UndeclaredGlobals(L, declared.globals)), "");
        // 20 global tables and 8 global functions; StateBag is no global.
        EQUALS(declared.globals.size(), static_cast<size_t>(28));
        EQUALS(declared.tables.size(), static_cast<size_t>(21));
        EQUALS(declared.members >= 230, true);
        // As on a server: the operators of the value types, and the classes a script calls.
        EQUALS(declared.operators, 3 * 7 + 2 + 1);
        EQUALS(declared.callable, 8);
    });

    IT("the client definition file matches the committed one", {
        const std::string generated = LuaParity::Generate(Framework::Integrations::Client::Scripting::Builtins::ClientLuaDefinitions(), Framework::Scripting::Bind::Side::Client);
        EQUALS(generated.find("---@class Nametags\n") != std::string::npos, true);

        if (LuaParity::UpdateRequested()) {
            EQUALS(LuaParity::WriteDefinitions("client.d.lua", generated), true);
        }

        EQUALS(LuaParity::FirstDifference(LuaParity::ReadDefinitions("client.d.lua"), generated), "");
    });
})
