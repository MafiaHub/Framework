/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <scripting/bind/call.h>
#include <scripting/bind/value.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace Framework::Integrations::Client::Scripting::Builtins {
    // An argument that is a flag, read as the language of the script reads a condition, which is how the
    // client builtins always read one from JavaScript: undefined, null, false, 0, NaN, the empty string and
    // the bigint zero are false there, nil and false in Lua.
    inline bool ReadFlag(const Framework::Scripting::Bind::Call &call, size_t index) {
        const Framework::Scripting::Bind::Value flag = call.Arg(index);
        if (flag.IsNil()) {
            return false;
        }
        if (flag.IsBoolean()) {
            return flag.AsBoolean();
        }
        if (call.GetLanguage() == Framework::Scripting::Language::Lua) {
            return true;
        }
        if (flag.IsNumber()) {
            const double number = flag.AsNumber();
            return number != 0.0 && number == number;
        }
        if (flag.IsString()) {
            return !flag.AsString().empty();
        }
        // Of what is left only a bigint can be false, and only zero is spelled "0".
        return flag.Kind() != Framework::Scripting::Bind::ValueKind::Other || flag.ToDisplayString() != "0";
    }

    // The whole part of a number that is an id, a timestamp or a mask, held to what 64 bits have, which is how
    // the client builtins always read one from JavaScript. A Lua integer is itself. 0 for a NaN and for what
    // is not a number.
    inline std::int64_t ToInt64(const Framework::Scripting::Bind::Value &value) {
        if (const auto integer = value.AsInteger()) {
            return *integer;
        }
        const double number = value.AsNumber();
        if (number != number) {
            return 0;
        }
        // 2^63, which a double has and an int64 does not.
        constexpr double limit = 9223372036854775808.0;
        if (number >= limit) {
            return std::numeric_limits<std::int64_t>::max();
        }
        if (number <= -limit) {
            return std::numeric_limits<std::int64_t>::min();
        }
        return static_cast<std::int64_t>(number);
    }
} // namespace Framework::Integrations::Client::Scripting::Builtins
