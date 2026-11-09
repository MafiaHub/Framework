/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../value_transfer.h"

#include <utils/result.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Framework::Scripting::Clr {
    /**
     * Values between JavaScript and C#: a TransferredValue written as bytes C# reads, and back
     * (docs/scripting_csharp.md). The C# mirror is Interop/ValueCodec.cs; change the two together.
     *
     * Little-endian. Each value is a tag byte and its payload:
     *   0 null (and undefined)        1 false              2 true
     *   3 number: f64                 4 string: u32 length + UTF-8
     *   5 array: u32 count + values   6 object: u32 count + (u32 length + UTF-8 key, value) pairs
     *   7 Vector2: x y f32            8 Vector3: x y z f32  9 Vector4: x y z w f32
     *   10 Quaternion: w x y z f32    11 Color: r g b a f32 in [0, 1]
     *   12 handle: u32 length + UTF-8 registered type name, u64 network id
     *   13 bytes: u32 length + bytes
     *
     * What JavaScript has and C# does not is converted or refused: a Date arrives as its milliseconds, a RegExp as its
     * source, a Set as an array, a Map with string keys as an object, an Error as an object of its name, message and
     * stack, an ArrayBuffer or typed array as bytes; a BigInt, a Map with other keys, a function and an unknown host
     * type fail the copy, naming where.
     */
    class ValueCodec final {
      public:
        static constexpr size_t kMaxDepth = 64;

        static Utils::Result<void, std::string> Encode(const TransferredValue &value, std::vector<uint8_t> &out);

        // Several values as one array, as call arguments travel.
        static Utils::Result<std::vector<uint8_t>, std::string> EncodeList(const std::vector<TransferredValue> &values);

        static Utils::Result<TransferredValue, std::string> Decode(const uint8_t *data, size_t length);

        // The array EncodeList wrote, as its values.
        static Utils::Result<std::vector<TransferredValue>, std::string> DecodeList(const uint8_t *data, size_t length);
    };
} // namespace Framework::Scripting::Clr
