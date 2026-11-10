/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "native_abi.h"

#include "../builtins/color.h"
#include "../builtins/quaternion.h"
#include "../builtins/vector2.h"
#include "../builtins/vector3.h"
#include "../builtins/vector4.h"

namespace Framework::Scripting::Api {
    // The value types cross by value, as blittable structs: in C# they are plain structs with their own maths, not
    // handles onto these classes.

    template <>
    struct NativeTraits<Builtins::Vector2> {
        static constexpr NativeType kType = NativeType::Vector2;
        static std::string CSharpType() {
            return "Vector2";
        }
        static Builtins::Vector2 From(const NativeValue &v, size_t i) {
            Detail::Expect(v, kType, i);
            return Builtins::Vector2(v.f32[0], v.f32[1]);
        }
        static void To(const Builtins::Vector2 &x, NativeValue &v) {
            v.type   = kType;
            v.f32[0] = x.vec().x;
            v.f32[1] = x.vec().y;
        }
    };

    template <>
    struct NativeTraits<Builtins::Vector3> {
        static constexpr NativeType kType = NativeType::Vector3;
        static std::string CSharpType() {
            return "Vector3";
        }
        static Builtins::Vector3 From(const NativeValue &v, size_t i) {
            Detail::Expect(v, kType, i);
            return Builtins::Vector3(v.f32[0], v.f32[1], v.f32[2]);
        }
        static void To(const Builtins::Vector3 &x, NativeValue &v) {
            v.type   = kType;
            v.f32[0] = x.vec().x;
            v.f32[1] = x.vec().y;
            v.f32[2] = x.vec().z;
        }
    };

    template <>
    struct NativeTraits<Builtins::Vector4> {
        static constexpr NativeType kType = NativeType::Vector4;
        static std::string CSharpType() {
            return "Vector4";
        }
        static Builtins::Vector4 From(const NativeValue &v, size_t i) {
            Detail::Expect(v, kType, i);
            return Builtins::Vector4(glm::vec4(v.f32[0], v.f32[1], v.f32[2], v.f32[3]));
        }
        static void To(const Builtins::Vector4 &x, NativeValue &v) {
            v.type   = kType;
            v.f32[0] = x.vec().x;
            v.f32[1] = x.vec().y;
            v.f32[2] = x.vec().z;
            v.f32[3] = x.vec().w;
        }
    };

    template <>
    struct NativeTraits<Builtins::Quaternion> {
        static constexpr NativeType kType = NativeType::Quaternion;
        static std::string CSharpType() {
            return "Quaternion";
        }
        static Builtins::Quaternion From(const NativeValue &v, size_t i) {
            Detail::Expect(v, kType, i);
            return Builtins::Quaternion(glm::quat(v.f32[0], v.f32[1], v.f32[2], v.f32[3]));
        }
        static void To(const Builtins::Quaternion &x, NativeValue &v) {
            v.type   = kType;
            v.f32[0] = x.quat().w;
            v.f32[1] = x.quat().x;
            v.f32[2] = x.quat().y;
            v.f32[3] = x.quat().z;
        }
    };

    template <>
    struct NativeTraits<Builtins::Color> {
        static constexpr NativeType kType = NativeType::Color;
        static std::string CSharpType() {
            return "Color";
        }
        static Builtins::Color From(const NativeValue &v, size_t i) {
            Detail::Expect(v, kType, i);
            return Builtins::Color(glm::vec4(v.f32[0], v.f32[1], v.f32[2], v.f32[3]));
        }
        static void To(const Builtins::Color &x, NativeValue &v) {
            v.type   = kType;
            v.f32[0] = x.vec().r;
            v.f32[1] = x.vec().g;
            v.f32[2] = x.vec().b;
            v.f32[3] = x.vec().a;
        }
    };
} // namespace Framework::Scripting::Api
