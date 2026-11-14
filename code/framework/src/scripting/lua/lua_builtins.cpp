/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_builtins.h"

#include "lua_engine.h"
#include "lua_runtime.h"

#include "../bind/call.h"
#include "../bind/callable.h"
#include "../builtins/messages.h"
#include "../builtins/state_bag.h"

namespace Framework::Scripting::Builtins {
    void WatchLuaRuntimes(LuaEngine &engine) {
        Bind::WatchRuntimes(engine);

        // After the above, which is what tells a handler that its runtime goes: the state changes a Lua runtime
        // watched are let go of with it.
        engine.AddRuntimeDisposingListener([](LuaRuntime &runtime) {
            StateBag::DropDeadSubscriptions();
            // So are the message requests its scripts were waiting on.
            Messages::DropAbandonedRequests();
            // The address of the runtime may be the next one's.
            Bind::SetCallResourceManager(&runtime, nullptr);
        });
    }
} // namespace Framework::Scripting::Builtins
