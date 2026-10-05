/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cerrno>

#include <v8.h>

#include <function2/function2.hpp>

#include <utils/result.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Framework::Scripting {

    /**
     * A JavaScript value copied out of one isolate, ready to be rebuilt in another.
     *
     * Server resources run in isolates of their own, so nothing passes between them by reference:
     * event arguments, export values and call results are copied into one of these on one side and
     * rebuilt on the other. docs/scripting_resource_isolation.md lists what each kind of value
     * arrives as.
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
        bool boolean = false;

        // Number; Date as milliseconds since the epoch; RegExp flags.
        double number = 0;

        // String; BigInt in decimal; RegExp source; the view type of a TypedArray; the registered name of a HostObject; the
        // message of an Error.
        std::string text;

        // ArrayBuffer and TypedArray contents; a HostObject's payload.
        std::vector<uint8_t> bytes;

        // Array elements; Map keys and values interleaved; Set members.
        std::vector<TransferredValue> items;

        // Own enumerable string-keyed properties of an Object, and of an Error beyond its name, message and stack (which
        // ride in `properties` too, under those names).
        std::vector<std::pair<std::string, TransferredValue>> properties;

        // Function: the id the exporting side handed out for it.
        uint64_t reference = 0;
    };

    /**
     * How functions cross. Without these, copying a function fails.
     */
    struct TransferFunctions {
        // Source side: keep the function reachable and return the id the other side will call it by.
        fu2::function<uint64_t(v8::Isolate *, v8::Local<v8::Function>) const> exportFunction;

        // Target side: make the callable stand-in for an exported function.
        fu2::function<v8::MaybeLocal<v8::Function>(v8::Isolate *, v8::Local<v8::Context>, uint64_t) const> importFunction;
    };

    class ValueTransfer final {
      public:
        // Writes an object's payload and returns true when the object is of this host type.
        using HostCopy = fu2::function<bool(v8::Isolate *, v8::Local<v8::Object>, std::vector<uint8_t> &) const>;

        // Builds the object again from its payload, in the target isolate.
        using HostRebuild = fu2::function<v8::MaybeLocal<v8::Value>(v8::Isolate *, v8::Local<v8::Context>, const std::vector<uint8_t> &) const>;

        /**
         * Teach the transfer a native-backed type, such as a value type or an entity handle, so it arrives as the same
         * type rather than as a plain object.
         *
         * Types are tried newest first. A derived handle must therefore be registered after its base, or the base claims
         * it: the framework registers its own types first, and a project's derived entities come after.
         */
        static void RegisterHostType(std::string name, HostCopy copy, HostRebuild rebuild);

        /**
         * Copy a value out of its isolate. The caller holds the isolate's scopes and the context. Fails, naming where in
         * the value it gave up, on a symbol, a cycle, a promise, a SharedArrayBuffer or another object that cannot be
         * copied, and on a function unless `functions` exports it.
         */
        static Utils::Result<TransferredValue, std::string> Copy(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value, const TransferFunctions &functions = {});

        /**
         * Build a copied value in the target isolate. The caller holds its scopes and the context. On failure a
         * JavaScript exception is pending and the result is empty.
         */
        static v8::MaybeLocal<v8::Value> Rebuild(v8::Isolate *isolate, v8::Local<v8::Context> context, const TransferredValue &value, const TransferFunctions &functions = {});
    };

} // namespace Framework::Scripting
