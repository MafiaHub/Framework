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

#include "integrations/client/scripting/builtins/chat.h"
#include "integrations/client/scripting/builtins/discord.h"
#include "integrations/client/scripting/builtins/keybinds.h"
#ifdef FW_TEST_CLIENT_NAMETAGS
#include "integrations/client/scripting/builtins/nametags.h"
#endif
#include "integrations/client/scripting/builtins/voice.h"
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
// everything the Lua definition file of a client declares. The module itself builds on Windows only, so what it
// registers is repeated here, in its order. Nametags needs the headers of ImGui (FW_TEST_CLIENT_NAMETAGS): a
// binary built without them holds every other builtin to the gate.
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

    // What ClientScriptingModule writes its Lua definition file from. `withNametags` is for a binary that has it.
    inline std::vector<const Bind::ClassDesc *> BuiltinDefinitions(bool withNametags = HasNametags()) {
        std::vector<const Bind::ClassDesc *> definitions = Builtins::SharedDefinitions(
            {&ClientBuiltins::Web::Definition().Describe(), &ClientBuiltins::Keybinds::Definition().Describe(), &ClientBuiltins::Chat::Definition().Describe(), &ClientBuiltins::Discord::Definition().Describe(), &ClientBuiltins::Voice::Definition().Describe()});
#ifdef FW_TEST_CLIENT_NAMETAGS
        if (withNametags) {
            definitions.push_back(&ClientBuiltins::Nametags::Definition().Describe());
        }
#else
        (void)withNametags;
#endif
        return definitions;
    }

    // What a client records for JavaScript: what ClientScriptingModule::RegisterFrameworkBindings registers, and
    // the handle classes a game publishes on top of it. Normalised; null when the engine does not start.
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

                ClientBuiltins::Web::Register(isolate, context, global, &manager);
                ClientBuiltins::Keybinds::Register(isolate, context, global, &manager);
                ClientBuiltins::Chat::Register(isolate, context, global, &manager);
                ClientBuiltins::Discord::Register(isolate, context, global, &manager);
                ClientBuiltins::Voice::Register(isolate, context, global, &manager);
#ifdef FW_TEST_CLIENT_NAMETAGS
                ClientBuiltins::Nametags::Register(isolate, context, global, &manager);
#endif

                Builtins::Entity::GetClass(isolate);
                Builtins::Player::GetClass(isolate);
                Builtins::StateBag::GetClass(isolate);
                Builtins::TextLabel::Register(isolate, global);

                catalog = nlohmann::json::parse(v8pp::metadata::to_json(registry), nullptr, /*allow_exceptions*/ false);
            }
            ClientBuiltins::Web::Shutdown();
            ClientBuiltins::Keybinds::Shutdown();
            ClientBuiltins::Discord::Shutdown();
        }
        engine.Shutdown();
        return ScriptingCatalogGolden::Normalise(std::move(catalog));
    }

    // The scripting of a client with every builtin in its Lua states, and one Lua resource, `lua`, started.
    struct Stage final {
        LuaClientTest::Client client;
        bool ready = false;

        Stage() {
            client.lua.AddRuntimeSetup([](LuaRuntime &runtime) {
                ClientBuiltins::Web::RegisterLua(runtime);
                ClientBuiltins::Keybinds::RegisterLua(runtime);
                ClientBuiltins::Chat::RegisterLua(runtime);
                ClientBuiltins::Discord::RegisterLua(runtime);
                ClientBuiltins::Voice::RegisterLua(runtime);
#ifdef FW_TEST_CLIENT_NAMETAGS
                ClientBuiltins::Nametags::RegisterLua(runtime);
#endif
            });
            ready = client.Init() && LuaClientTest::Mount("lua", R"("clientScripts":["client.lua"])", {{"client.lua", ""}}).empty() && client.Start("lua").empty();
        }

        // In the order of ClientScriptingModule::Shutdown.
        ~Stage() {
            if (client.manager) {
                client.manager->StopAll();
            }
            ClientBuiltins::Web::Shutdown();
            ClientBuiltins::Keybinds::Shutdown();
            client.manager.reset();
            client.lua.Shutdown();
            ClientBuiltins::Discord::Shutdown();
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

    // `text` without the definition of Nametags, which is the last one before the globals of the runtime.
    inline std::string WithoutNametags(const std::string &text) {
        const size_t declared = text.find("---@class Nametags\n");
        const size_t globals  = text.find("function setTimeout(");
        if (declared == std::string::npos || globals == std::string::npos || globals < declared) {
            return text;
        }
        const size_t start = text.rfind("\n\n", declared);
        const size_t end   = text.rfind("\n\n", globals);
        return start == std::string::npos || end == std::string::npos || end < start ? text : text.substr(0, start) + text.substr(end);
    }
} // namespace LuaClientParityTest

MODULE(lua_client_parity, {
    using Framework::Scripting::Bind::Side;
    using LuaClientParityTest::Stage;

    IT("gives Lua every member of every global the client catalog documents for JavaScript", {
        const nlohmann::json catalog = LuaClientParityTest::BuildCatalog();
        EQUALS(catalog.is_object(), true);

        Stage stage;
        EQUALS(stage.ready, true);
        lua_State *L = stage.State();
        NEQUALS(L, nullptr);

        const auto builtins = LuaClientParityTest::BuiltinDefinitions();
        LuaParity::Gate gate(L, Side::Client, builtins, LuaClientParityTest::Exemptions());
        const LuaParity::Report report = gate.Check(catalog);

        EQUALS(LuaParity::Join(report.missing), "");
        EQUALS(LuaParity::Join(report.stale), "");
        EQUALS(report.symbols >= 20, true);
        EQUALS(report.members >= 250, true);
        EQUALS(static_cast<size_t>(report.exempted), LuaClientParityTest::Exemptions().size());
        for (const char *name : {"Web", "Key", "Chat", "Discord", "Voice", "Events", "Vector3", "Entity", "Player", "TextLabel"}) {
            EQUALS(ScriptingCatalogGolden::HasSymbol(catalog, name), true);
        }
    });

    IT("has in a Lua state everything the client definition file declares, and declares every global", {
        Stage stage;
        EQUALS(stage.ready, true);
        lua_State *L = stage.State();
        NEQUALS(L, nullptr);

        const auto builtins                = LuaClientParityTest::BuiltinDefinitions();
        const std::string definitions      = LuaParity::Generate(builtins, Side::Client);
        const LuaParity::Declared declared = LuaParity::CheckDefinitions(definitions, L, Side::Client, builtins);

        EQUALS(LuaParity::CompileError(definitions), "");
        EQUALS(LuaParity::Join(declared.missing), "");
        EQUALS(LuaParity::Join(LuaParity::UndeclaredGlobals(L, declared.globals)), "");
        EQUALS(declared.globals.size() >= 23, true);
        EQUALS(declared.members >= 230, true);
    });

    IT("the client definition file matches the committed one", {
        const std::string generated = LuaParity::Generate(LuaClientParityTest::BuiltinDefinitions(), Side::Client);
        std::string committed       = LuaParity::ReadDefinitions("client.d.lua");
        const bool hasNametags      = committed.find("---@class Nametags\n") != std::string::npos;

        if (LuaParity::UpdateRequested()) {
            // A binary without Nametags would write a file without it: only over one that has none.
            EQUALS(LuaClientParityTest::HasNametags() || !hasNametags, true);
            EQUALS(LuaParity::WriteDefinitions("client.d.lua", generated), true);
            committed = generated;
        }

        // The file was written by a binary with Nametags or by one without, and is read by either.
        if (LuaClientParityTest::HasNametags() && !hasNametags) {
            EQUALS(LuaParity::FirstDifference(committed, LuaParity::Generate(LuaClientParityTest::BuiltinDefinitions(false), Side::Client)), "");
        }
        else if (!LuaClientParityTest::HasNametags() && hasNametags) {
            EQUALS(LuaParity::FirstDifference(LuaClientParityTest::WithoutNametags(committed), generated), "");
        }
        else {
            EQUALS(LuaParity::FirstDifference(committed, generated), "");
        }
        EQUALS(generated.find("---@class Web\n") != std::string::npos, true);
        EQUALS(generated.find("---@class Nametags\n") != std::string::npos, LuaClientParityTest::HasNametags());
    });
})
