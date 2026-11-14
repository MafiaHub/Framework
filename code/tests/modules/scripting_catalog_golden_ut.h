/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "integrations/server/scripting/builtins/pending_connection.h"
#include "integrations/server/scripting/builtins/voice.h"
#include "integrations/server/scripting/event_metadata.h"
#include "scripting/builtins/builtins.h"
#include "scripting/builtins/chat.h"
#include "scripting/builtins/console.h"
#include "scripting/builtins/entity.h"
#include "scripting/builtins/events.h"
#include "scripting/builtins/execution_environment.h"
#include "scripting/builtins/exports.h"
#include "scripting/builtins/imports.h"
#include "scripting/builtins/messages.h"
#include "scripting/builtins/player.h"
#include "scripting/builtins/state_bag.h"
#include "scripting/builtins/text_label.h"
#include "scripting/event_metadata.h"
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/scripting_catalog.h"
#include "scripting/timer_metadata.h"

#include <nlohmann/json.hpp>
#include <v8pp/metadata.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>

// The catalog a server runtime's builtins record, held against a committed fixture. The catalog is
// what the TypeScript declarations are generated from, so a builtin that is rewritten must keep
// recording exactly this; the fixture was written from the hand-written builtins, before any port.
//
// A deliberate change to a builtin's documentation is taken into the fixture by running the module
// once with FW_UPDATE_GOLDEN=1 and committing the result.
class ScriptingCatalogGolden {
  public:
    // A catalog of its own: the named catalogs are process-global, and every other test that
    // registers a builtin records into the default one.
    static constexpr const char *kCatalogName = "golden-server";

    static std::string FixturePath() {
        return std::string(FW_TEST_FIXTURES_DIR) + "/scripting_catalog_server.json";
    }

    static bool UpdateRequested() {
        const char *value = std::getenv("FW_UPDATE_GOLDEN");
        return value && *value && std::strcmp(value, "0") != 0;
    }

    // Registers what ServerScriptingModule::RegisterFrameworkBindings registers, and the classes a
    // server brings up outside it, then returns the catalog they recorded, normalised. Null when the
    // engine does not start.
    static nlohmann::json BuildServerCatalog() {
        Framework::Scripting::NodeEngine engine({});
        if (engine.Init() != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
            return nullptr;
        }
        Framework::Scripting::ResourceManager manager(&engine, {});

        nlohmann::json catalog;
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);
            v8::Local<v8::Object> global = context->Global();

            Framework::Scripting::SetScriptingCatalog(isolate, kCatalogName);
            Framework::Scripting::SetScriptingEnvironment(isolate, /*isClient*/ false);
            v8pp::metadata::registry &registry = Framework::Scripting::GetScriptingCatalog(isolate);

            Framework::Scripting::RegisterEventMetadata(registry);
            Framework::Integrations::Server::Scripting::RegisterServerEventMetadata(registry);
            Framework::Scripting::RegisterTimerMetadata(registry, /*isClient*/ false);

            Framework::Scripting::Bind::Registrar registrar(isolate, context, global, Framework::Scripting::Bind::Side::Server);
            Framework::Scripting::Builtins::RegisterShared(registrar, Framework::Scripting::Bind::Languages::All, manager, "");
            Framework::Scripting::Builtins::Chat::Register(registrar);
            Framework::Integrations::Server::Scripting::Builtins::Voice::Register(registrar);
            Framework::Integrations::Server::Scripting::Builtins::PendingConnection::Register(registrar);

            // The handle classes are not part of RegisterFrameworkBindings: the server builds them on
            // first use, or a game registers them. TextLabel goes through Register because that is
            // where its static create and TextLabelOptions are recorded.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);
            Framework::Scripting::Builtins::Player::GetClass(isolate);
            Framework::Scripting::Builtins::StateBag::GetClass(isolate);
            Framework::Scripting::Builtins::TextLabel::Register(registrar);

            catalog = nlohmann::json::parse(v8pp::metadata::to_json(registry), nullptr, /*allow_exceptions*/ false);
        }
        engine.Shutdown();

        return Normalise(std::move(catalog));
    }

    // Sorts every list whose order is the order of registration, so moving a registration is not a
    // difference. Parameters and bases keep their order: it is part of what they mean.
    static nlohmann::json Normalise(nlohmann::json catalog) {
        if (!catalog.is_object()) {
            return nullptr;
        }
        for (const char *list : {"symbols", "variables", "functions"}) {
            Sort(catalog[list]);
        }
        for (nlohmann::json &symbol : catalog["symbols"]) {
            Sort(symbol["functions"]);
            Sort(symbol["properties"]);
        }
        return catalog;
    }

    // The fixture, normalised, or null when it is missing or is not a JSON object.
    static nlohmann::json ReadFixture() {
        std::ifstream input(FixturePath(), std::ios::binary);
        if (!input) {
            return nullptr;
        }
        return Normalise(nlohmann::json::parse(input, nullptr, /*allow_exceptions*/ false));
    }

    static bool WriteFixture(const nlohmann::json &catalog) {
        std::ofstream output(FixturePath(), std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output << catalog.dump(2) << '\n';
        return static_cast<bool>(output);
    }

    // What separates `catalog` from the fixture, or "" when nothing does.
    static std::string CompareWithFixture(const nlohmann::json &catalog) {
        const nlohmann::json fixture = ReadFixture();
        if (!fixture.is_object()) {
            return "no readable fixture at " + FixturePath() + "; run this module once with FW_UPDATE_GOLDEN=1 to write it";
        }
        return Difference(fixture, catalog);
    }

    // The first symbol two normalised catalogs disagree on and how, or "" when they are equal. Missing
    // is in `expected` and not in `actual`; extra is the reverse.
    static std::string Difference(const nlohmann::json &expected, const nlohmann::json &actual) {
        if (std::string difference = FirstDifference("symbol", List(expected, "symbols"), List(actual, "symbols"), &SymbolDifference); !difference.empty()) {
            return difference;
        }
        if (std::string difference = FirstDifference("variable", List(expected, "variables"), List(actual, "variables")); !difference.empty()) {
            return difference;
        }
        if (std::string difference = FirstDifference("global function", List(expected, "functions"), List(actual, "functions")); !difference.empty()) {
            return difference;
        }
        return expected == actual ? std::string() : "the catalog is different outside its symbols, variables and global functions";
    }

    static bool HasSymbol(const nlohmann::json &catalog, const std::string &name) {
        const nlohmann::json symbols = List(catalog, "symbols");
        return std::any_of(symbols.begin(), symbols.end(), [&name](const nlohmann::json &symbol) {
            return symbol.value("name", std::string()) == name;
        });
    }

  private:
    using Detail = std::string (*)(const nlohmann::json &expected, const nlohmann::json &actual);

    // What a list is sorted and matched by. A class may have a static and an instance member of one
    // name; symbols, variables and global functions have no static flag and sort by name alone.
    static std::pair<bool, std::string> Key(const nlohmann::json &entry) {
        return {entry.value("static", false), entry.value("name", std::string())};
    }

    static void Sort(nlohmann::json &list) {
        if (list.is_array()) {
            std::sort(list.begin(), list.end(), [](const nlohmann::json &left, const nlohmann::json &right) {
                return Key(left) < Key(right);
            });
        }
    }

    static nlohmann::json List(const nlohmann::json &owner, const char *name) {
        const nlohmann::json list = owner.is_object() ? owner.value(name, nlohmann::json::array()) : nlohmann::json::array();
        return list.is_array() ? list : nlohmann::json::array();
    }

    static std::string Label(const char *kind, const nlohmann::json &entry) {
        return std::string(entry.value("static", false) ? "static " : "") + kind + " '" + entry.value("name", std::string()) + "'";
    }

    // Walks two sorted lists together and describes the first entry they disagree on.
    static std::string FirstDifference(const char *kind, const nlohmann::json &expected, const nlohmann::json &actual, Detail detail = nullptr) {
        size_t expectedIndex = 0;
        size_t actualIndex   = 0;
        while (expectedIndex < expected.size() || actualIndex < actual.size()) {
            if (actualIndex == actual.size() || (expectedIndex < expected.size() && Key(expected[expectedIndex]) < Key(actual[actualIndex]))) {
                return Label(kind, expected[expectedIndex]) + " is missing";
            }
            if (expectedIndex == expected.size() || Key(actual[actualIndex]) < Key(expected[expectedIndex])) {
                return Label(kind, actual[actualIndex]) + " is extra";
            }
            if (expected[expectedIndex] != actual[actualIndex]) {
                const std::string difference = Label(kind, expected[expectedIndex]) + " is different";
                return detail ? difference + ": " + detail(expected[expectedIndex], actual[actualIndex]) : difference;
            }
            ++expectedIndex;
            ++actualIndex;
        }
        return {};
    }

    // Where inside a symbol the difference is, so a failure points at the member to look at.
    static std::string SymbolDifference(const nlohmann::json &expected, const nlohmann::json &actual) {
        if (std::string difference = FirstDifference("function", List(expected, "functions"), List(actual, "functions")); !difference.empty()) {
            return difference;
        }
        if (std::string difference = FirstDifference("property", List(expected, "properties"), List(actual, "properties")); !difference.empty()) {
            return difference;
        }
        for (const char *field : {"description", "kind", "constructor", "bases"}) {
            if (expected.value(field, nlohmann::json()) != actual.value(field, nlohmann::json())) {
                return std::string("field '") + field + "' is different";
            }
        }
        return "it is different outside its members";
    }
};

MODULE(scripting_catalog_golden, {
    IT("the server catalog matches the committed fixture", {
        const nlohmann::json catalog = ScriptingCatalogGolden::BuildServerCatalog();
        EQUALS(catalog.is_object(), true);

        if (ScriptingCatalogGolden::UpdateRequested()) {
            EQUALS(ScriptingCatalogGolden::WriteFixture(catalog), true);
        }

        const std::string difference = ScriptingCatalogGolden::CompareWithFixture(catalog);
        EQUALS(difference, "");
    });

    IT("the fixture names every builtin", {
        const nlohmann::json fixture = ScriptingCatalogGolden::ReadFixture();

        // A fixture rewritten from half a registration compares equal to that half, so the names are held here too.
        const char *const names[] = {"Vector2", "Vector3", "Vector4", "Quaternion", "Color", "Entity", "Player", "TextLabel", "TextLabelOptions", "StateBag", "Events", "EventMap", "Messages", "Exports", "Imports", "console", "ExecutionEnvironment", "Chat", "Voice",
            "PendingConnection", "Timeout"};

        std::string missing;
        for (const char *name : names) {
            if (!ScriptingCatalogGolden::HasSymbol(fixture, name)) {
                missing += std::string(missing.empty() ? "" : ", ") + name;
            }
        }
        EQUALS(missing, "");
    });
})
