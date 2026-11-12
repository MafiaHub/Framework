/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting_catalog_golden_ut.h"

#include "integrations/client/scripting/builtins/chat.h"
#include "integrations/client/scripting/builtins/discord.h"
#include "integrations/client/scripting/builtins/keybinds.h"
#include "integrations/client/scripting/builtins/nametags.h"
#include "integrations/client/scripting/builtins/voice.h"
#include "integrations/client/scripting/builtins/web.h"
#include "scripting/engine_scope.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/scripting_catalog.h"
#include "scripting/v8_engine.h"

#include <nlohmann/json.hpp>
#include <v8pp/metadata.hpp>

#include <fstream>
#include <string>

// The catalog the client-only builtins record, held against a committed fixture as the server catalog is
// (scripting_catalog_golden_ut.h): it is what the TypeScript declarations of a client are generated from. The
// fixture was written from the hand-written builtins, before their port.
//
// A deliberate change is taken into the fixture by running the module once with FW_UPDATE_GOLDEN=1.
namespace ClientCatalogGolden {
    inline std::string FixturePath() {
        return std::string(FW_TEST_FIXTURES_DIR) + "/scripting_catalog_client.json";
    }

    // What the client-only builtins record in the one isolate of a client, in the order ClientScriptingModule
    // registers them, normalised. Null when the engine does not start.
    inline nlohmann::json BuildCatalog() {
        using namespace Framework::Scripting;
        namespace ClientBuiltins = Framework::Integrations::Client::Scripting::Builtins;

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
                SetScriptingCatalog(scope.Isolate(), "golden-client");
                SetScriptingEnvironment(scope.Isolate(), /*isClient*/ true);
                ClientBuiltins::Web::Register(scope.Isolate(), scope.Context(), scope.Context()->Global(), &manager);
                ClientBuiltins::Keybinds::Register(scope.Isolate(), scope.Context(), scope.Context()->Global(), &manager);
                ClientBuiltins::Chat::Register(scope.Isolate(), scope.Context(), scope.Context()->Global(), &manager);
                ClientBuiltins::Discord::Register(scope.Isolate(), scope.Context(), scope.Context()->Global(), &manager);
                ClientBuiltins::Voice::Register(scope.Isolate(), scope.Context(), scope.Context()->Global(), &manager);
                ClientBuiltins::Nametags::Register(scope.Isolate(), scope.Context(), scope.Context()->Global(), &manager);
                catalog = nlohmann::json::parse(v8pp::metadata::to_json(GetScriptingCatalog(scope.Isolate())), nullptr, /*allow_exceptions*/ false);
            }
            ClientBuiltins::Web::Shutdown();
            ClientBuiltins::Keybinds::Shutdown();
            ClientBuiltins::Discord::Shutdown();
        }
        engine.Shutdown();
        return ScriptingCatalogGolden::Normalise(std::move(catalog));
    }

    inline nlohmann::json ReadFixture() {
        std::ifstream input(FixturePath(), std::ios::binary);
        if (!input) {
            return nullptr;
        }
        return ScriptingCatalogGolden::Normalise(nlohmann::json::parse(input, nullptr, /*allow_exceptions*/ false));
    }
} // namespace ClientCatalogGolden

MODULE(client_catalog_golden, {
    IT("the catalog of the client builtins matches the committed fixture", {
        const nlohmann::json catalog = ClientCatalogGolden::BuildCatalog();
        EQUALS(catalog.is_object(), true);

        if (ScriptingCatalogGolden::UpdateRequested()) {
            std::ofstream output(ClientCatalogGolden::FixturePath(), std::ios::binary | std::ios::trunc);
            output << catalog.dump(2) << '\n';
            EQUALS(static_cast<bool>(output), true);
        }

        const nlohmann::json fixture = ClientCatalogGolden::ReadFixture();
        EQUALS(fixture.is_object(), true);
        EQUALS(ScriptingCatalogGolden::Difference(fixture, catalog), "");
        for (const char *name : {"Web", "Key", "Chat", "Discord", "Voice", "Nametags", "EventMap", "BrowserCreatedEvent", "BrowserOriginChangeEvent"}) {
            EQUALS(ScriptingCatalogGolden::HasSymbol(fixture, name), true);
        }
    });
})
