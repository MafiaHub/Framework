/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../value_transfer.h"

#include <function2/function2.hpp>
#include <lua.h>

#include <utils/result.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Scripting {
    /**
     * How much one copy out of a Lua state, or one JSON text written from one, may hold: values, and bytes of
     * strings and binary data, or of text. The bounds of ValueTransfer, unless a caller has a tighter budget.
     *
     * They bound what the host allocates as well as what it returns. A table with more keys than the copy may
     * still take is refused as its keys are read, not after they were gathered.
     */
    struct LuaValueLimits final {
        size_t values = ValueTransfer::kMaxValues;
        size_t bytes  = ValueTransfer::kMaxBytes;
    };

    /**
     * The Lua side of TransferredValue: copies a Lua value into one and builds one in a Lua state, so that event
     * arguments, export values and call results cross between Lua states and between Lua and JavaScript.
     * Section 4.1 of docs/superpowers/specs/2026-11-11-polyglot-scripting-design.md lists what each value becomes.
     *
     * Both functions may be called from a function a script called and from host code outside any entry into the
     * state. Neither lets a Lua error out: the work runs under a protected call, and a state that is out of memory
     * or of stack fails the copy like anything else. `L` is a thread a function can be called on: the main thread,
     * the one running the calling function, or one that has not been started.
     *
     * A table is read raw. Its metatable does not cross and none of its metamethods runs, so the table of a
     * script's own class arrives as a plain table of its fields. An object of a bound class is recognised by its
     * class and crosses when that class is a host type.
     *
     * A weak table (`__mode`) may lose entries to the collector while it is copied, since the copy allocates. The
     * copy then holds the entries that were still there when it reached them, each under its own key; an entry
     * that went is absent, or undefined where it was an element of an array.
     *
     * A function crosses as its callable (bind/callable.h), which keeps it alive in its state, and arrives as a
     * function that runs it there: bind/call.h, at Call::FromTransfer, says what calling it gives. In the state it
     * came from it arrives as itself. This needs a state a LuaRuntime made, on both sides. One that arrives as a
     * member of an object can be called as a method of the table it arrived in: `t:f(x)` is `t.f(x)` there
     * (Bind::LuaNoteHolder).
     */
    class LuaTransfer final {
      public:
        // Writes the payload of the object at `index`, an absolute index, and returns true when the object is of
        // this host type. It is given objects of the class named `name` and of the classes derived from it.
        using HostCopy = fu2::function<bool(lua_State *, int index, std::vector<uint8_t> &bytes) const>;

        // Builds the object again from its payload and pushes it: one value, and true. False when it cannot.
        using HostRebuild = fu2::function<bool(lua_State *, const std::vector<uint8_t> &bytes) const>;

        /**
         * Teach the transfer a bound class, under the name ValueTransfer::RegisterHostType knows the same type by
         * and with the same payload, so that it arrives as the same type in either language. `name` is the script
         * name of the class. Bind::Class::Transfer declares both registrations at once.
         *
         * An object crosses as its own class when that is registered, as the first of its bases that is otherwise.
         * Registering a name again replaces it. Call it while the process starts, before any script runs.
         *
         * A callback may raise a Lua error or throw a std::exception: either fails the copy.
         */
        static void RegisterHostType(std::string name, HostCopy copy, HostRebuild rebuild);

        // Forgets a host type: for a test that registered one of its own, which nothing else should meet.
        static void UnregisterHostType(std::string_view name);

        /**
         * Copy the value at `index` out of its state. The stack is left as it was.
         *
         * Fails, naming where in the value it gave up ("Cannot copy value.items[2].name: ..."), on a coroutine, a
         * userdata that is not an object of a host type, a table that refers back to itself or has a
         * key that is neither a string nor an integer, a string that is not UTF-8, tables nested more than 128
         * deep, and a value larger than `limits` allow, which are those of ValueTransfer when none are given. An
         * index in that path is the one Lua counts with, from 1, and a key in it is cut to its first 64 bytes.
         */
        static Utils::Result<TransferredValue, std::string> Copy(lua_State *L, int index);
        static Utils::Result<TransferredValue, std::string> Copy(lua_State *L, int index, const LuaValueLimits &limits);

        /**
         * Build a copied value in the state of `L` and push it: one value. On failure nothing is pushed and `error`
         * says why, naming where in the value: a RegExp, a BigInt beyond 64 bits, a Map key that is neither a
         * string nor a number, a host type Lua does not have, a function that carries no callable, as one that
         * was exported between JavaScript runtimes alone. An index in that path counts from 0, as the path of
         * ValueTransfer does.
         */
        static bool Rebuild(lua_State *L, const TransferredValue &value, std::string &error);
    };
} // namespace Framework::Scripting
