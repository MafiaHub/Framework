/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8pp/metadata.hpp>

namespace Framework::Integrations::Server::Scripting {
    // The events only a server raises and no builtin declares, added to the EventMap of its catalog next to the
    // shared ones (Framework::Scripting::RegisterEventMetadata). The same rule holds: only what the server is the
    // sole raiser of, with the tuple it emits.
    inline void RegisterServerEventMetadata(v8pp::metadata::registry &catalog) {
        catalog.data_type("EventMap")
            .add_property("consoleCommand", "[command: string, args: string[]]",
                "Dispatched for a line typed into the server console that no built-in command (help, ensure, refresh, ...) claimed. `args` is the rest of the line, split on whitespace. The console is the operator's, so this is the "
                "place for commands no player may run.");
    }
} // namespace Framework::Integrations::Server::Scripting
