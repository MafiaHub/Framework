/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "vector4.h"
#include "../scripting_catalog.h"

#include <sstream>

namespace Framework::Scripting::Builtins {

    namespace {
        // toJSON method for JSON.stringify support - returns plain JS object
        void ToJson(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Vector4>::unwrap_object(info.GetIsolate(), info.This());
            if (!self)
                return;

            auto iso = info.GetIsolate();
            auto ctx = iso->GetCurrentContext();
            auto obj = v8::Object::New(iso);
            obj->Set(ctx, v8pp::to_v8(iso, "x"), v8::Number::New(iso, self->getX())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "y"), v8::Number::New(iso, self->getY())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "z"), v8::Number::New(iso, self->getZ())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "w"), v8::Number::New(iso, self->getW())).Check();
            info.GetReturnValue().Set(obj);
        }
    } // namespace

    Vector4 &Vector4::normalize() {
        float len = glm::length(_vec);
        if (len > 0.0f)
            _vec /= len;
        return *this;
    }

    std::string Vector4::toString() const {
        std::ostringstream ss;
        ss << "Vector4(" << _vec.x << ", " << _vec.y << ", " << _vec.z << ", " << _vec.w << ")";
        return ss.str();
    }

    const Bind::Class<Vector4> &Vector4::Definition() {
        using Bind::Doc;
        using Bind::LuaMetaEvent;
        static const auto definition =
            Bind::Class<Vector4>("Vector4", "Mutable four-dimensional vector.")
                .Constructor<float, float, float, float>(Doc("Creates a vector from four numeric components.").Param("x", "Initial X component.").Param("y", "Initial Y component.").Param("z", "Initial Z component.").Param("w", "Initial W component."))
                // Instance methods
                .Method("add", &Vector4::add, Doc("Adds another vector to this vector in place.").Param("other", "Vector to add component-wise.").Returns("This mutated vector for chaining."))
                .Method("sub", &Vector4::sub, Doc("Subtracts another vector from this vector in place.").Param("other", "Vector to subtract component-wise.").Returns("This mutated vector for chaining."))
                .Method("mul", &Vector4::mul, Doc("Multiplies this vector by a scalar in place.").Param("scalar", "Multiplier applied to every component.").Returns("This mutated vector for chaining."))
                .Method("div", &Vector4::div, Doc("Divides this vector by a scalar in place.").Param("scalar", "Divisor applied to every component; must be non-zero.").Returns("This mutated vector for chaining."))
                .Method("dot", &Vector4::dot, Doc("Computes the dot product without changing either vector.").Param("other", "Vector used for the dot product.").Returns("Scalar dot product."))
                .Method("distance", &Vector4::distance, Doc("Computes Euclidean distance to another vector.").Param("other", "Vector to measure from this vector.").Returns("Distance between the two vectors."))
                .Method("normalize", &Vector4::normalize, Doc("Normalizes this vector in place; a zero vector remains unchanged.").Returns("This mutated vector for chaining."))
                .Method("lerp", &Vector4::lerp,
                    Doc("Linearly interpolates this vector toward a target in place.").Param("target", "Destination vector.").Param("t", "Interpolation factor; 0 keeps the current value and 1 reaches target.").Returns("This mutated vector for chaining."))
                .Method("set", &Vector4::set,
                    Doc("Replaces all components in place.").Param("x", "Replacement X component.").Param("y", "Replacement Y component.").Param("z", "Replacement Z component.").Param("w", "Replacement W component.").Returns("This mutated vector for chaining."))
                .Method("clone", &Vector4::clone, Doc("Creates an independent copy of this vector.").Returns("New vector with the same components."))
                .Method("toString", &Vector4::toString, Doc("Formats this vector for logging and debugging.").Returns("Text in Vector4(x, y, z, w) form."))
                .Property("x", &Vector4::getX, &Vector4::setX, "Mutable X component.")
                .Property("y", &Vector4::getY, &Vector4::setY, "Mutable Y component.")
                .Property("z", &Vector4::getZ, &Vector4::setZ, "Mutable Z component.")
                .Property("w", &Vector4::getW, &Vector4::setW, "Mutable W component.")
                .Property("length", &Vector4::getLength, "Read-only Euclidean magnitude of this vector.")
                .Property("lengthSquared", &Vector4::getLengthSquared, "Read-only squared magnitude, avoiding a square-root calculation.")
                // Static factory methods
                .Static("zero", &Vector4::zero, Doc("Creates a zero vector.").Returns("New Vector4(0, 0, 0, 0)."))
                .Static("one", &Vector4::one, Doc("Creates a vector whose components are one.").Returns("New Vector4(1, 1, 1, 1)."))
                // JavaScript only, until Lua has JSON.
                .V8Hook([](v8pp::class_<Vector4> &cls, v8::Isolate *) {
                    cls.prototype_function("toJSON", &ToJson, v8pp::metadata::docs("{ x: number; y: number; z: number; w: number }", {}, "Converts this vector to a plain object for JSON.stringify.", "Object containing the current components."));
                })
                // Lua operators. Each gives a new vector and leaves its operands as they were.
                .LuaMeta(LuaMetaEvent::Add,
                    [](const Vector4 &left, const Vector4 &right) {
                        return Vector4(left.vec() + right.vec());
                    })
                .LuaMeta(LuaMetaEvent::Sub,
                    [](const Vector4 &left, const Vector4 &right) {
                        return Vector4(left.vec() - right.vec());
                    })
                .LuaMeta(LuaMetaEvent::Mul,
                    [](const Vector4 &vector, float scalar) {
                        return Vector4(vector.vec() * scalar);
                    })
                .LuaMeta(LuaMetaEvent::Mul,
                    [](float scalar, const Vector4 &vector) {
                        return Vector4(scalar * vector.vec());
                    })
                .LuaMeta(LuaMetaEvent::Div,
                    [](const Vector4 &vector, float scalar) {
                        return Vector4(vector.vec() / scalar);
                    })
                .LuaMeta(LuaMetaEvent::Unm,
                    [](const Vector4 &vector) {
                        return Vector4(-vector.vec());
                    })
                .LuaMeta(LuaMetaEvent::Eq, [](const Vector4 &left, const Vector4 &right) {
                    return left.vec() == right.vec();
                });
        return definition;
    }

    v8pp::class_<Vector4> &Vector4::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            return Definition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), ScriptingSide(isolate));
        });
    }

    void Vector4::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<Vector4> &cls = GetClass(isolate);
        auto ctx                   = isolate->GetCurrentContext();
        global->Set(ctx, v8pp::to_v8(isolate, "Vector4"), cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
    }

    v8::Local<v8::Object> Vector4::NewInstance(v8::Isolate *isolate, const glm::vec4 &value) {
        v8pp::class_<Vector4> &cls = GetClass(isolate);
        return cls.import_external(isolate, new Vector4(value));
    }

    void Vector4::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.Unregister(isolate);
    }

    void Vector4::RegisterLua(lua_State *L, Bind::Side side) {
        Definition().RegisterLuaGlobal(L, side);
    }

} // namespace Framework::Scripting::Builtins
