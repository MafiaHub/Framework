/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <lua.h>

#include <utils/result.h>

#include <string>
#include <string_view>

/**
 * JSON for Lua values, written and read in C++: the payload of a network event and the value of a state bag travel
 * as JSON text, and JavaScript is on the other end of either. The text is what JSON.stringify writes for the same
 * data, and what it wrote is read as JSON.parse reads it, with the mapping of section 4.1 of
 * docs/superpowers/specs/2026-11-11-polyglot-scripting-design.md.
 *
 * Both functions may be called from a function a script called and from host code outside any entry into the
 * state, and neither lets a Lua error out (lua_transfer.h says how, and which threads `L` may be). A table is read
 * raw: no metamethod runs.
 */
namespace Framework::Scripting {
    struct LuaValueLimits; // lua_transfer.h
} // namespace Framework::Scripting

namespace Framework::Scripting::LuaJson {
    /**
     * Write the value at `index` as JSON. The stack is left as it was.
     *
     * - nil is null. A float is written as JavaScript spells the number, so one with an integral value has no
     *   fraction; a NaN and an infinity are null, as JSON.stringify writes them.
     * - A table whose keys are exactly 1..n is an array, and so is the empty table: []. Any other table is an object,
     *   its integer keys ascending as their decimal text, then its string keys sorted by their bytes. Where a table
     *   has an integer key and the string key of the same text, the string key is the one written.
     * - An object of a bound class is the object of the properties its class declares with
     *   Bind::Class::JsonProperties, which is what its toJSON gives in JavaScript.
     *
     * Fails, naming where in the value it gave up ("Cannot encode value.items[2].name: ..."), on a function, a
     * coroutine, a userdata or a bound object without a JSON form, a table that refers back to itself or has a key
     * that is neither a string nor an integer, a string that is not UTF-8, tables nested more than 128 deep, and a
     * value of more values, or of more bytes of text once escaped, than `limits` allow, which are the bounds of
     * ValueTransfer when none are given. An index in that path counts from 1, and a key in it is cut to its first
     * 64 bytes.
     *
     * A weak table may lose entries to the collector while it is written: an entry that went is left out, or is
     * null where it was an element of an array.
     */
    Utils::Result<std::string, std::string> Encode(lua_State *L, int index);
    Utils::Result<std::string, std::string> Encode(lua_State *L, int index, const LuaValueLimits &limits);

    /**
     * Read JSON text and push what it holds: one value. On failure nothing is pushed and `error` says why:
     * "Invalid JSON: ..." with the line and the column for text that is not JSON, and with where in the value
     * for text that nests deeper than 128 levels ("Invalid JSON: value.list[0]...: it is nested too deeply").
     *
     * - An object is a table with string keys, an array a sequence. null is nil: a member that is null is absent,
     *   and an element that is null leaves a hole.
     * - A number with an integral value within 2^53 is an integer, however it is spelled; any other is a float,
     *   which is the number JavaScript reads from the same text.
     * - Of two members with the same key the last one counts.
     */
    bool Decode(lua_State *L, std::string_view text, std::string &error);
} // namespace Framework::Scripting::LuaJson
