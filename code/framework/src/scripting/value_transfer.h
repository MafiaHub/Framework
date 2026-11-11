/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "transferred_value.h"

#include <v8.h>

#include <function2/function2.hpp>

#include <utils/result.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Framework::Scripting {

    /**
     * How functions cross. Without these, copying a function fails, and one is rebuilt only from the callable it
     * carries (TransferredValue::callable).
     */
    struct TransferFunctions {
        // Source side: keep the function reachable and fill in what it crosses as, `reference` and `retainer` or
        // `callable`. `holder` is the object or array the function was found in, empty for a function sent on its
        // own: what it is called on later.
        fu2::function<void(v8::Isolate *, v8::Local<v8::Function>, v8::Local<v8::Object> holder, TransferredValue &) const> exportFunction;

        // Target side: make the callable stand-in for an exported function. A function that carries no reference is
        // not this hook's: it is rebuilt from its callable, as it is without the hook.
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

        // Forgets a host type: for a test that registered one of its own, which no later copy should be asked about.
        static void UnregisterHostType(std::string_view name);

        // A copy holds at most this many values and this many bytes of strings and binary data. An object reached twice
        // is copied twice, so without a bound a few kilobytes of nested references would expand without end.
        static constexpr size_t kMaxValues = 1u << 20;
        static constexpr size_t kMaxBytes  = 256u << 20;

        /**
         * Copy a value out of its isolate. The caller holds the isolate's scopes and the context. Fails, naming where in
         * the value it gave up, on a symbol, a cycle, a promise, a SharedArrayBuffer or another object that cannot be
         * copied, on a value larger than the bounds above, and on a function unless `functions` exports it.
         */
        static Utils::Result<TransferredValue, std::string> Copy(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value, const TransferFunctions &functions = {});

        /**
         * Build a copied value in the target isolate. The caller holds its scopes and the context. On failure a
         * JavaScript exception is pending and the result is empty.
         *
         * A function that carries a callable and no reference `functions` imports arrives as a function that
         * invokes the callable (bind/v8_callable.h), or as the function itself in the isolate that owns it.
         */
        static v8::MaybeLocal<v8::Value> Rebuild(v8::Isolate *isolate, v8::Local<v8::Context> context, const TransferredValue &value, const TransferFunctions &functions = {});
    };

} // namespace Framework::Scripting
