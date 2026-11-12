/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "descriptors.h"
#include "doc.h"

#include <string>
#include <vector>

namespace Framework::Scripting::Bind {
    // The globals every Lua state has that no definition declares: the timers, sleep, spawn, and print and require
    // as the sandbox provides them. Written by hand next to WriteLuaDefinitions, which describes them too.
    const std::vector<FunctionDesc> &LuaRuntimeGlobals();

    // The definition file of the Lua language server (LuaLS, `---@meta`) for a runtime of side `side`: every class
    // and namespace of `catalog` that exists there, with the members of that side, in the order of the catalog,
    // then LuaRuntimeGlobals. `catalog` holds the definitions a scripting module registers and nothing else: the
    // process-wide one also has whatever else the process defined.
    //
    // A class is a `---@class` with a `---@field` for each property, a function for each method and static,
    // `Name.new` and a call signature when it has a constructor, and an `---@operator` for each Lua operator that
    // has an annotation. A function documented as returning a Promise is `---@async` and returns what the promise
    // settles with, since the calling coroutine waits for it.
    std::string WriteLuaDefinitions(const ApiCatalog &catalog, Side side);

    // Writes WriteLuaDefinitions of `definitions` to the file the environment variable `variable` names. True when
    // the variable is unset or empty, and when the file was written.
    bool ExportLuaDefinitionsFromEnvironment(const std::vector<const ClassDesc *> &definitions, Side side, const char *variable);
} // namespace Framework::Scripting::Bind
