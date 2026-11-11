/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "callable.h"

#include <lua.h>

#include <memory>
#include <optional>
#include <string>

namespace Framework::Scripting {
    class LuaRuntime;
} // namespace Framework::Scripting

// The Lua side of ScriptCallable and Deferred (callable.h).
//
// All of it needs a state a LuaRuntime made: the function of a callable runs in a coroutine of its scheduler, and a
// deferred result waits on one.
//
// What is kept for a runtime, its callables and the calls that have not ended, is dropped when the runtime goes:
// LuaRuntimeDisposing, which the listener of WatchRuntimes calls. A runtime no engine owns is heard of as its state
// closes.
namespace Framework::Scripting::Bind {
    // The callable of the function at `index`: the one it has while anyone holds one, a new one otherwise, and for
    // a function that stands for a callable of another runtime, that callable. nullptr for what is not a function.
    // The stack is left as it was. Raises a Lua error when Lua is out of memory or of stack.
    std::shared_ptr<ScriptCallable> LuaToCallable(lua_State *L, int index);

    // Pushes a function that invokes `callable` (Call::FromTransfer says how), or the function itself in the state
    // that owns it. Raises a Lua error when Lua is out of memory or of stack.
    void LuaPushCallable(lua_State *L, const std::shared_ptr<ScriptCallable> &callable);

    // Marks dead what is kept for `runtime`: its callables, whose calls that have not ended are told
    // kRuntimeStopped, and its deferred results. Call it while the state is open; a second call does nothing.
    void LuaRuntimeDisposing(LuaRuntime &runtime);

    namespace Detail {
        // The deferred result of a Lua call.
        class LuaDeferred: public DeferredState {
          public:
            // What the call gives the script once its function has returned without throwing. Resolved by then:
            // the value, pushed, and 1. Rejected by then, or resolved with what Lua cannot be given: the message
            // to raise in `error`, and 0. Not settled yet: it does not return, as the coroutine waits, or as Lua
            // raises that it cannot, with `label` in front when there is one.
            virtual int Finish(lua_State *L, const char *label, std::optional<std::string> &error) = 0;
        };

        // Raises a Lua error when Lua is out of memory or of stack.
        std::shared_ptr<LuaDeferred> LuaDefer(lua_State *L);
    } // namespace Detail
} // namespace Framework::Scripting::Bind
