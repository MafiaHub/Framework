/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8pp/metadata.hpp>

namespace Framework::Scripting {
    // The events the framework raises itself, declared in its own catalog.
    //
    // A mod's catalog creates EventMap for its own events, and MergeScriptingCatalog blends these
    // into it, so a game documents what it raises and nothing else. Before that blend existed these
    // had to be restated by hand in every mod, which is a copy that silently goes stale the moment
    // an argument here changes.
    //
    // Only events the framework is the sole raiser of belong here, and only with the tuple it
    // actually emits. An entry that overstates the surface is worse than a missing one: a script
    // written against it compiles and then does not work.
    inline void RegisterEventMetadata(v8pp::metadata::registry &catalog) {
        auto &events = catalog.data_type("EventMap", "Native events dispatched through `Events.on`. Each property is the exact callback argument tuple for that event.");

        events.add_property("resourceStart", "[resourceName: string]", "Dispatched after a resource entry point has run and immediately before the resource becomes running.");
        events.add_property("resourceStop", "[resourceName: string]", "Dispatched while a resource is stopping, before its stop callback, timers, exports and event handlers are cleaned up.");
        events.add_property("entityStateChange", "[entity: Entity, key: string, value: any, previous: any]",
            "Dispatched when one key of an entity's state changes: on the server when a script writes it, on a client when the write arrives. `value` is undefined when the key was removed and `previous` is undefined when it held nothing "
            "before, so a stored null stays distinguishable from an absent key. The entity is whatever the game's WrapScriptEntity answers, and the base Entity handle by default.");
    }
} // namespace Framework::Scripting
