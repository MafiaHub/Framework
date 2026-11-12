/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_engine.h"

#include "../errors.h"

#include <functional>
#include <string>
#include <vector>

namespace Framework::Scripting {
    class LuaRuntime;
    class ResourceManager;

    // The Lua half of a client's scripting: a Lua engine with the limits of a client, the shared builtins in every
    // state it creates, and the resource manager told of what its scripts raise. The client scripting module owns
    // one next to its V8 engine. It is here, and not in that module, so that it builds and is tested on every host.
    //
    // The scripts are what a server sent. Everything they reach is what Init registers in their state, and a later
    // builtin of the client is added to the setup the same way.
    class LuaClientHost final {
      public:
        explicit LuaClientHost(LuaEngineOptions options = LuaEngineOptions::Client());

        // Brings the engine up and gives it to the manager, which then runs the `.lua` scripts of its resources in
        // it. Call it again after Shutdown, with the same manager or the next one.
        [[nodiscard]] ScriptingError Init(ResourceManager &manager, const std::string &modVersion);

        // Has `setup` run in every state created from now on, after the shared builtins are in it: how the
        // client scripting module adds the builtins only a client has. Call it before a resource starts.
        // Call once per process; setups persist across `Reset()`. A setup may add another, which runs in the
        // state being set up too.
        void AddRuntimeSetup(std::function<void(LuaRuntime &)> setup);

        // Fires the timers that are due and wakes the coroutines whose sleep is over, then hands the manager the
        // errors nobody waited for, outside any script. Call it from the game loop. Between Shutdown and Init it
        // only closes the states a shutdown that came from inside a script left open.
        void Update();

        // Destroys every Lua state, and with it its handlers, timers and callables, and forgets the errors that
        // were not handed over yet. For a client that disconnects as for one that closes: after the resources
        // were stopped, and before the manager goes when it does.
        void Shutdown();

        LuaEngine &GetEngine() {
            return _engine;
        }

        const LuaEngine &GetEngine() const {
            return _engine;
        }

      private:
        LuaEngine _engine;
        std::string _modVersion;
        std::vector<std::function<void(LuaRuntime &)>> _setups;
    };
} // namespace Framework::Scripting
