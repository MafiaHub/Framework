/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace Framework::Scripting::Api {
    // The flat calling convention a language without V8 uses to reach a binding (docs/scripting_csharp.md). Every
    // binding member is one thunk of the same shape, so a language needs one function-pointer type to call them all;
    // what differs per member is only the values in the array. The C# mirror is Interop/NativeValue.cs: change the two
    // together.
    enum class NativeType : uint32_t {
        Void       = 0,
        Bool       = 1,
        Int32      = 2,
        UInt32     = 3,
        Int64      = 4,
        UInt64     = 5,
        Float      = 6,
        Double     = 7,
        String     = 8,  // UTF-8, `text` + `length`, not terminated
        Vector2    = 9,  // x, y
        Vector3    = 10, // x, y, z
        Vector4    = 11, // x, y, z, w
        Quaternion = 12, // w, x, y, z: scalar-first, as the scripting Quaternion
        Color      = 13, // r, g, b, a in [0, 1]
        Handle     = 14, // network id in `u64`
    };

    struct NativeValue {
        NativeType type = NativeType::Void;
        uint32_t length = 0;
        union {
            int64_t i64;
            uint64_t u64;
            double f64;
            float f32[4];
            const char *text;
        };

        NativeValue(): u64(0) {}
    };
    static_assert(sizeof(NativeValue) == 24, "NativeValue is mirrored by C#; keep its layout");

    enum class NativeStatus : int32_t {
        Ok            = 0,
        ArgumentError = static_cast<int32_t>(ErrorKind::Argument),
        StateError    = static_cast<int32_t>(ErrorKind::State),
        // The receiver's object is gone: the caller treats the call as a no-op and the result as its type's default.
        StaleReceiver = 3,
    };

    // `self` is the receiver's network id (0 for a static). On an error the result is a String holding the message.
    // A String result stays valid until the next call on this thread.
    using NativeThunk = int32_t (*)(uint64_t self, const NativeValue *args, int32_t argc, NativeValue *result);

    namespace Detail {
        inline std::string &ResultText() {
            thread_local std::string text;
            return text;
        }

        inline void SetText(NativeValue &value, std::string text) {
            std::string &storage = ResultText();
            storage              = std::move(text);
            value.type           = NativeType::String;
            value.text           = storage.data();
            value.length         = static_cast<uint32_t>(storage.size());
        }

        inline const char *NativeTypeName(NativeType type) {
            switch (type) {
            case NativeType::Void: return "void";
            case NativeType::Bool: return "bool";
            case NativeType::Int32: return "int";
            case NativeType::UInt32: return "uint";
            case NativeType::Int64: return "long";
            case NativeType::UInt64: return "ulong";
            case NativeType::Float: return "float";
            case NativeType::Double: return "double";
            case NativeType::String: return "string";
            case NativeType::Vector2: return "Vector2";
            case NativeType::Vector3: return "Vector3";
            case NativeType::Vector4: return "Vector4";
            case NativeType::Quaternion: return "Quaternion";
            case NativeType::Color: return "Color";
            case NativeType::Handle: return "handle";
            }
            return "unknown";
        }

        inline void Expect(const NativeValue &value, NativeType type, size_t index) {
            if (value.type != type) {
                ThrowArgumentError("argument " + std::to_string(index) + " must be " + NativeTypeName(type) + ", got " + NativeTypeName(value.type));
            }
        }
    } // namespace Detail

    // How one C++ type crosses the flat convention. Specialised for every type a native-callable binding takes or
    // returns; a binding with any other type can only be bound for JavaScript (Target::JavaScript).
    //   kType       - the NativeType it travels as
    //   CSharpType()- the C# type the generated API declares
    //   From(v, i)  - read argument `i`, throwing an Argument ScriptError on a mismatch
    //   To(x, v)    - write a result
    template <typename T, typename = void>
    struct NativeTraits;

    template <typename T>
    concept NativeConvertible = requires { NativeTraits<std::remove_cvref_t<T>>::kType; };

    template <>
    struct NativeTraits<bool> {
        static constexpr NativeType kType = NativeType::Bool;
        static std::string CSharpType() {
            return "bool";
        }
        static bool From(const NativeValue &v, size_t i) {
            Detail::Expect(v, kType, i);
            return v.u64 != 0;
        }
        static void To(bool x, NativeValue &v) {
            v.type = kType;
            v.u64  = x ? 1 : 0;
        }
    };

#define FW_NATIVE_INTEGER(CppType, Kind, Field, CSharp)                                                                                                                                                                                                                                \
    template <>                                                                                                                                                                                                                                                                        \
    struct NativeTraits<CppType> {                                                                                                                                                                                                                                                     \
        static constexpr NativeType kType = NativeType::Kind;                                                                                                                                                                                                                          \
        static std::string CSharpType() {                                                                                                                                                                                                                                              \
            return CSharp;                                                                                                                                                                                                                                                             \
        }                                                                                                                                                                                                                                                                              \
        static CppType From(const NativeValue &v, size_t i) {                                                                                                                                                                                                                          \
            Detail::Expect(v, kType, i);                                                                                                                                                                                                                                               \
            return static_cast<CppType>(v.Field);                                                                                                                                                                                                                                      \
        }                                                                                                                                                                                                                                                                              \
        static void To(CppType x, NativeValue &v) {                                                                                                                                                                                                                                    \
            v.type  = kType;                                                                                                                                                                                                                                                           \
            v.Field = x;                                                                                                                                                                                                                                                               \
        }                                                                                                                                                                                                                                                                              \
    };

    FW_NATIVE_INTEGER(int32_t, Int32, i64, "int")
    FW_NATIVE_INTEGER(uint32_t, UInt32, u64, "uint")
    FW_NATIVE_INTEGER(int64_t, Int64, i64, "long")
    FW_NATIVE_INTEGER(uint64_t, UInt64, u64, "ulong")
#undef FW_NATIVE_INTEGER

    template <>
    struct NativeTraits<float> {
        static constexpr NativeType kType = NativeType::Float;
        static std::string CSharpType() {
            return "float";
        }
        static float From(const NativeValue &v, size_t i) {
            Detail::Expect(v, kType, i);
            return static_cast<float>(v.f64);
        }
        static void To(float x, NativeValue &v) {
            v.type = kType;
            v.f64  = x;
        }
    };

    template <>
    struct NativeTraits<double> {
        static constexpr NativeType kType = NativeType::Double;
        static std::string CSharpType() {
            return "double";
        }
        static double From(const NativeValue &v, size_t i) {
            Detail::Expect(v, kType, i);
            return v.f64;
        }
        static void To(double x, NativeValue &v) {
            v.type = kType;
            v.f64  = x;
        }
    };

    template <>
    struct NativeTraits<std::string> {
        static constexpr NativeType kType = NativeType::String;
        static std::string CSharpType() {
            return "string";
        }
        static std::string From(const NativeValue &v, size_t i) {
            Detail::Expect(v, kType, i);
            return v.length == 0 ? std::string() : std::string(v.text, v.length);
        }
        static void To(const std::string &x, NativeValue &v) {
            Detail::SetText(v, x);
        }
    };

    // A handle type: constructible from its network id (throwing when the entity is gone) and reporting it back.
    template <typename T>
    concept NativeHandle = std::constructible_from<T, uint64_t> && requires(const T &t) {
        { t.GetId() } -> std::convertible_to<uint64_t>;
        { T::Binding() };
    };

    template <typename T>
    struct NativeTraits<T, std::enable_if_t<NativeHandle<T>>> {
        static constexpr NativeType kType = NativeType::Handle;
        static std::string CSharpType() {
            return T::Binding().GetName();
        }
        static T From(const NativeValue &v, size_t i) {
            Detail::Expect(v, kType, i);
            try {
                return T(v.u64);
            }
            catch (const ScriptError &) {
                throw;
            }
            catch (const std::exception &) {
                ThrowArgumentError("argument " + std::to_string(i) + " names an entity that no longer exists");
            }
        }
        static void To(const T &x, NativeValue &v) {
            v.type = kType;
            v.u64  = x.GetId();
        }
    };
} // namespace Framework::Scripting::Api
