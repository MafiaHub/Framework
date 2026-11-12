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
#ifdef FW_TEST_CLIENT_NAMETAGS
#include "integrations/client/scripting/builtins/nametags.h"
#endif
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
// registers of its own, in both languages, and what it writes its definition file from are the three functions of
// integrations/client/scripting/builtins/builtins.h, which are what is used here. Nametags needs the headers of
// ImGui (FW_TEST_CLIENT_NAMETAGS): a binary built without them holds every other builtin to the gate, and the
// committed definition file less its Nametags section.
namespace LuaClientParityTest {
    using namespace Framework::Scripting;
    using Framework::Utils::Vfs;
    namespace ClientBuiltins = Framework::Integrations::Client::Scripting::Builtins;

    inline bool HasNametags() {
#ifdef FW_TEST_CLIENT_NAMETAGS
        return true;
#else
        return false;
#endif
    }

    // In the order of ClientScriptingModule::Shutdown, for what the client builtins keep.
    inline void ShutdownBuiltins() {
        ClientBuiltins::Web::Shutdown();
        ClientBuiltins::Keybinds::Shutdown();
        ClientBuiltins::Discord::Shutdown();
#ifdef FW_TEST_CLIENT_NAMETAGS
        ClientBuiltins::Nametags::Shutdown();
#endif
    }

    // What a client records for JavaScript: what ClientScriptingModule::RegisterFrameworkBindings registers, in
    // its order, and the handle classes a game publishes on top of it. Normalised; null when the engine does not
    // start.
    inline nlohmann::json BuildCatalog() {
        V8Engine engine;
        if (engine.Init() != ScriptingError::SCRIPTING_NONE) {
            return nullptr;
        }
        nlohmann::json catalog;
        {
            ResourceManagerConfig config;
            config.isClient = true;
            ResourceManager manager(&engine, config);
            {
                EngineScope scope(&engine);
                v8::Isolate *isolate                 = scope.Isolate();
                const v8::Local<v8::Context> context = scope.Context();
                const v8::Local<v8::Object> global   = context->Global();
                SetScriptingCatalog(isolate, "parity-client");
                SetScriptingEnvironment(isolate, /*isClient*/ true);
                v8pp::metadata::registry &registry = GetScriptingCatalog(isolate);

                RegisterEventMetadata(registry);
                RegisterTimerMetadata(registry, /*isClient*/ true);
                Builtins::RegisterValueTypes(isolate, global);
                manager.GetEvents().Register(isolate, context, global, &manager, /*isClient*/ true);
                Builtins::Messages::Register(isolate, context, global, &manager);
                Builtins::Exports::Register(isolate, context, global, &manager);
                Builtins::Imports::Register(isolate, context, global, &manager);
                Builtins::Console::Register(isolate, context, &manager);
                Builtins::ExecutionEnvironment::Register(isolate, context, global, /*isClient*/ true);
                ClientBuiltins::RegisterClientBuiltins(isolate, context, global, &manager);

                Builtins::Entity::GetClass(isolate);
                Builtins::Player::GetClass(isolate);
                Builtins::StateBag::GetClass(isolate);
                Builtins::TextLabel::Register(isolate, global);

                catalog = nlohmann::json::parse(v8pp::metadata::to_json(registry), nullptr, /*allow_exceptions*/ false);
            }
            ShutdownBuiltins();
        }
        engine.Shutdown();
        return ScriptingCatalogGolden::Normalise(std::move(catalog));
    }

    // The scripting of a client with its builtins in every Lua state, and one Lua resource, `lua`, started.
    struct Stage final {
        LuaClientTest::Client client;
        bool ready = false;

        Stage() {
            client.lua.AddRuntimeSetup(&ClientBuiltins::RegisterClientBuiltinsLua);
            ready = client.Init() && LuaClientTest::Mount("lua", R"("clientScripts":["client.lua"])", {{"client.lua", ""}}).empty() && client.Start("lua").empty();
        }

        ~Stage() {
            if (client.manager) {
                client.manager->StopAll();
            }
            ShutdownBuiltins();
            client.manager.reset();
            client.lua.Shutdown();
            client.v8.Shutdown();
            Vfs::Get().Unmount("lua.zip");
        }

        lua_State *State() {
            LuaRuntime *runtime = client.lua.GetEngine().GetRuntime("lua");
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
    using Framework::Scripting::Bind::Side;
    using LuaClientParityTest::Stage;
    namespace ClientBuiltins = Framework::Integrations::Client::Scripting::Builtins;

    // Nametags is one global object in the catalog, and one global table of nine functions in the file.
    const int nametags = LuaClientParityTest::HasNametags() ? 1 : 0;

    IT("gives Lua every member of every global the client catalog documents for JavaScript", {
        const nlohmann::json catalog = LuaClientParityTest::BuildCatalog();
        EQUALS(catalog.is_object(), true);

        Stage stage;
        EQUALS(stage.ready, true);
        lua_State *L = stage.State();
        NEQUALS(L, nullptr);

        const auto builtins = ClientBuiltins::ClientLuaDefinitions();
        LuaParity::Gate gate(L, Side::Client, builtins, LuaClientParityTest::Exemptions());
        const LuaParity::Report report = gate.Check(catalog);

        EQUALS(LuaParity::Join(report.missing), "");
        EQUALS(LuaParity::Join(report.stale), "");
        // The whole catalog: 11 global objects, and Nametags where it builds, and 9 classes. The data types are
        // EventMap, TextLabelOptions and the twelve payloads of the browser events.
        EQUALS(report.symbols, 20 + nametags);
        EQUALS(report.dataTypes, 14);
        EQUALS(report.members >= 250, true);
        EQUALS(static_cast<size_t>(report.exempted), LuaClientParityTest::Exemptions().size());
        EQUALS(ScriptingCatalogGolden::HasSymbol(catalog, "Nametags"), LuaClientParityTest::HasNametags());
        // A builtin registered for one language alone.
        EQUALS(LuaParity::Join(LuaParity::OneSided(catalog, builtins, Side::Client)), "");
    });

    IT("has in a Lua state everything the client definition file declares, and declares every global", {
        Stage stage;
        EQUALS(stage.ready, true);
        lua_State *L = stage.State();
        NEQUALS(L, nullptr);

        const auto builtins                = ClientBuiltins::ClientLuaDefinitions();
        const std::string definitions      = LuaParity::Generate(builtins, Side::Client);
        const LuaParity::Declared declared = LuaParity::CheckDefinitions(definitions, L, Side::Client, builtins);

        EQUALS(LuaParity::CompileError(definitions), "");
        EQUALS(LuaParity::Join(declared.missing), "");
        EQUALS(LuaParity::Join(LuaParity::UndeclaredGlobals(L, declared.globals)), "");
        // 19 global tables, and Nametags where it builds, and 8 global functions; StateBag is no global.
        EQUALS(declared.globals.size(), static_cast<size_t>(27 + nametags));
        EQUALS(declared.tables.size(), static_cast<size_t>(20 + nametags));
        EQUALS(declared.members >= 230, true);
        // As on a server: the operators of the value types, and the classes a script calls.
        EQUALS(declared.operators, 3 * 7 + 2 + 1);
        EQUALS(declared.callable, 8);
    });

    IT("the client definition file matches the committed one", {
        const std::string generated = LuaParity::Generate(ClientBuiltins::ClientLuaDefinitions(), Side::Client);
        EQUALS(generated.find("---@class Nametags\n") != std::string::npos, LuaClientParityTest::HasNametags());

        if (LuaParity::UpdateRequested()) {
            // A binary without Nametags would write a file without it.
            EQUALS(LuaClientParityTest::HasNametags(), true);
            EQUALS(LuaParity::WriteDefinitions("client.d.lua", generated), true);
        }

        // The committed file has every builtin. A binary without Nametags holds the rest of it.
        const std::string committed = LuaParity::ReadDefinitions("client.d.lua");
        EQUALS(committed.find("---@class Nametags\n") != std::string::npos, true);
        EQUALS(LuaParity::FirstDifference(LuaClientParityTest::HasNametags() ? committed : LuaParity::WithoutDefinition(committed, "Nametags"), generated), "");
    });
})
