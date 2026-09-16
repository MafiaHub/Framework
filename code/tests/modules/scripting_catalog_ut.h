/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/event_metadata.h"
#include "scripting/scripting_catalog.h"

#include <v8pp/metadata.hpp>

#include <algorithm>
#include <string>

// Merging the framework's catalog into a project's. The rule has two halves that pull in opposite
// directions: a class both sides define must stay the project's, because the two declare the same
// members differently; a data type both sides define must blend, because EventMap is one map that
// both sides put their own events into. Getting either half wrong is silent -- the export still
// writes, it just documents the wrong surface.
MODULE(scripting_catalog, {
    using Framework::Scripting::MergeScriptingCatalog;

    const auto hasProperty = [](const v8pp::metadata::registry &registry, const std::string &symbolName, const std::string &propertyName) {
        for (const auto &symbol : registry.symbols()) {
            if (symbol.name != symbolName) {
                continue;
            }
            return std::any_of(symbol.properties.begin(), symbol.properties.end(), [&propertyName](const v8pp::metadata::property &property) {
                return property.name == propertyName;
            });
        }
        return false;
    };

    const auto propertyType = [](const v8pp::metadata::registry &registry, const std::string &symbolName, const std::string &propertyName) {
        for (const auto &symbol : registry.symbols()) {
            if (symbol.name != symbolName) {
                continue;
            }
            for (const auto &property : symbol.properties) {
                if (property.name == propertyName) {
                    return property.value_type.name;
                }
            }
        }
        return std::string();
    };

    const auto countSymbols = [](const v8pp::metadata::registry &registry, const std::string &name) {
        return std::count_if(registry.symbols().begin(), registry.symbols().end(), [&name](const v8pp::metadata::symbol &symbol) {
            return symbol.name == name;
        });
    };

    IT("carries a data type the project does not define at all", {
        v8pp::metadata::registry source;
        source.data_type("FrameworkOnly").add_property("a", "string", "");

        v8pp::metadata::registry destination;
        MergeScriptingCatalog(destination, source);
        EQUALS(hasProperty(destination, "FrameworkOnly", "a"), true);
    });

    IT("blends the framework's events into a map the project already created", {
        v8pp::metadata::registry source;
        source.data_type("EventMap").add_property("resourceStart", "[resourceName: string]", "");

        v8pp::metadata::registry destination;
        destination.data_type("EventMap").add_property("playerConnect", "[player: Player]", "");

        MergeScriptingCatalog(destination, source);

        // The whole point: a mod documents what it raises, and inherits what the framework raises.
        EQUALS(hasProperty(destination, "EventMap", "playerConnect"), true);
        EQUALS(hasProperty(destination, "EventMap", "resourceStart"), true);
        // Blended into the one map rather than appended as a second symbol of the same name.
        EQUALS(countSymbols(destination, "EventMap") == 1, true);
    });

    IT("leaves an event the project already declares alone", {
        v8pp::metadata::registry source;
        source.data_type("EventMap").add_property("resourceStart", "[resourceName: string]", "");

        v8pp::metadata::registry destination;
        destination.data_type("EventMap").add_property("resourceStart", "[projectShape: number]", "");

        MergeScriptingCatalog(destination, source);
        // A project that has deliberately narrowed an event keeps its own tuple.
        STREQUALS(propertyType(destination, "EventMap", "resourceStart").c_str(), "[projectShape: number]");
    });

    IT("does not blend a class both sides define", {
        v8pp::metadata::registry source;
        source.constructor("Player").add_property("frameworkOnly", "string", "");

        v8pp::metadata::registry destination;
        destination.constructor("Player").add_property("projectOnly", "string", "");

        MergeScriptingCatalog(destination, source);
        // Both sides declare Player with different members; blending them is not expressible in
        // TypeScript, so the project's specialised one is taken whole.
        EQUALS(hasProperty(destination, "Player", "projectOnly"), true);
        EQUALS(hasProperty(destination, "Player", "frameworkOnly"), false);
    });

    IT("does not blend across different kinds of symbol", {
        v8pp::metadata::registry source;
        source.data_type("Player").add_property("frameworkOnly", "string", "");

        v8pp::metadata::registry destination;
        destination.constructor("Player").add_property("projectOnly", "string", "");

        // Adding a data type over a constructor of the same name throws inside the registry, so the
        // merge has to check both kinds rather than only the source's.
        MergeScriptingCatalog(destination, source);
        EQUALS(hasProperty(destination, "Player", "frameworkOnly"), false);
    });

    IT("drops a skipped symbol even when the project has no symbol of that name", {
        v8pp::metadata::registry source;
        source.global_object("Events").add_property("on", "Function", "");

        v8pp::metadata::registry destination;
        MergeScriptingCatalog(destination, source, {"Events"});
        EQUALS(countSymbols(destination, "Events") == 0, true);
    });

    IT("is idempotent: merging twice carries nothing across a second time", {
        // A client re-initialises its scripting on every connect and the catalogs are process-global,
        // so the merge runs again over its own output. Left unguarded it collided with what it wrote
        // last time and minted a Base... twin of every framework class, plus BasePlayer_ for the one
        // already carried across under that name.
        const auto build = [](v8pp::metadata::registry &source, v8pp::metadata::registry &destination) {
            source.constructor("Entity", "framework entity").add_property("id", "number", "");
            source.constructor("Player", "framework player").add_property("id", "number", "");
            source.data_type("EventMap").add_property("resourceStart", "[resourceName: string]", "");
            destination.constructor("Player", "project player").add_property("nickname", "string", "");
        };

        v8pp::metadata::registry source;
        v8pp::metadata::registry once;
        build(source, once);
        MergeScriptingCatalog(once, source);

        v8pp::metadata::registry twice;
        build(source, twice);
        MergeScriptingCatalog(twice, source);
        MergeScriptingCatalog(twice, source);

        EQUALS(twice.symbols().size() == once.symbols().size(), true);
        EQUALS(countSymbols(twice, "BasePlayer") == 1, true);
        EQUALS(countSymbols(twice, "BasePlayer_") == 0, true);
        EQUALS(countSymbols(twice, "BaseEntity") == 0, true);
        EQUALS(countSymbols(twice, "Entity") == 1, true);
        // The project still owns its own class, and the framework's is still reachable as the base.
        EQUALS(hasProperty(twice, "Player", "nickname"), true);
        EQUALS(hasProperty(twice, "BasePlayer", "id"), true);
    });

    IT("still sidesteps a class the project itself calls Base...", {
        // The suffix is the fallback, not the bug: a project that defines its own BasePlayer must
        // keep it, and the framework's Player still has to reach the output under some name.
        v8pp::metadata::registry source;
        source.constructor("Player", "framework player").add_property("id", "number", "");

        v8pp::metadata::registry destination;
        destination.constructor("Player", "project player").add_property("nickname", "string", "");
        destination.constructor("BasePlayer", "project base player").add_property("projectOnly", "string", "");

        MergeScriptingCatalog(destination, source);
        EQUALS(hasProperty(destination, "BasePlayer", "projectOnly"), true);
        EQUALS(hasProperty(destination, "BasePlayer_", "id"), true);
    });

    IT("declares the events the framework raises", {
        v8pp::metadata::registry framework;
        Framework::Scripting::RegisterEventMetadata(framework);

        v8pp::metadata::registry project;
        project.data_type("EventMap");
        MergeScriptingCatalog(project, framework);

        // A mod inherits these rather than restating them; that is what makes the framework's own
        // documentation self-sufficient.
        EQUALS(hasProperty(project, "EventMap", "resourceStart"), true);
        EQUALS(hasProperty(project, "EventMap", "resourceStop"), true);
        EQUALS(hasProperty(project, "EventMap", "entityStateChange"), true);
    });
})
