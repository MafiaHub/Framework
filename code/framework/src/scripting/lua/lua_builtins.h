/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Scripting {
    class LuaEngine;
} // namespace Framework::Scripting

namespace Framework::Scripting::Builtins {
    /**
     * Has what the shared builtins keep for a Lua runtime dropped as the engine disposes of it: its callables and
     * the results it is owed (Bind::WatchRuntimes), the state changes it watched, the message requests its scripts
     * waited on and the manager its scripting module named for it. Call it once for each engine, before a script
     * runs in it.
     */
    void WatchLuaRuntimes(LuaEngine &engine);
} // namespace Framework::Scripting::Builtins
