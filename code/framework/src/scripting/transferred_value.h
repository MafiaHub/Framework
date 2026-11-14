/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Framework::Scripting::Bind {
    class ScriptCallable; // bind/callable.h
} // namespace Framework::Scripting::Bind

namespace Framework::Scripting {

    /**
     * A script value copied out of one runtime, ready to be rebuilt in another.
     *
     * Server resources run in runtimes of their own, so nothing passes between them by reference:
     * event arguments, export values and call results are copied into one of these on one side and
     * rebuilt on the other. docs/scripting_resource_isolation.md lists what each kind of JavaScript
     * value arrives as, lua/lua_transfer.h what a Lua value becomes.
     *
     * The type names neither language: ValueTransfer (value_transfer.h) and LuaTransfer copy and
     * rebuild it.
     */
    struct TransferredValue {
        enum class Kind : uint8_t {
            Undefined,
            Null,
            Boolean,
            Number,
            BigInt,
            String,
            Array,
            Object,
            Date,
            RegExp,
            Map,
            Set,
            ArrayBuffer,
            TypedArray,
            Error,
            HostObject,
            Function,
        };

        Kind kind = Kind::Undefined;

        // Boolean; whether a BigInt is negative.
        bool boolean = false;

        // Number; Date as milliseconds since the epoch; RegExp flags.
        double number = 0;

        // String; RegExp source; the view type of a TypedArray; the registered name of a HostObject; the message of an
        // Error.
        std::string text;

        // ArrayBuffer and TypedArray contents; a HostObject's payload; a BigInt's magnitude as 64-bit words, least
        // significant first.
        std::vector<uint8_t> bytes;

        // Array elements; Map keys and values interleaved; Set members.
        std::vector<TransferredValue> items;

        // Own enumerable string-keyed properties of an Object, and of an Error beyond its name, message and stack (which
        // ride in `properties` too, under those names).
        std::vector<std::pair<std::string, TransferredValue>> properties;

        // Function, between two JavaScript runtimes (function_references.h): the id the exporting side handed out for
        // it, and what keeps it exported while this copy exists.
        uint64_t reference = 0;
        std::shared_ptr<void> retainer;

        // Function, whatever the languages on the two sides: the function itself, to be invoked in the runtime that
        // owns it. Set for a function copied out of Lua, and for one copied out of JavaScript through the binding
        // layer (Bind::Value::ToTransfer, the result of a ScriptCallable).
        std::shared_ptr<Bind::ScriptCallable> callable;
    };

} // namespace Framework::Scripting
