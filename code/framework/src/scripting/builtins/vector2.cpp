/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "vector2.h"
#include "../scripting_catalog.h"

#include <sstream>

namespace Framework::Scripting::Builtins {

    namespace {
        // toJSON method for JSON.stringify support - returns plain JS object
        void ToJson(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
            if (!self)
                return;

            auto iso = info.GetIsolate();
            auto ctx = iso->GetCurrentContext();
            auto obj = v8::Object::New(iso);
            obj->Set(ctx, v8pp::to_v8(iso, "x"), v8::Number::New(iso, self->getX())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "y"), v8::Number::New(iso, self->getY())).Check();
            info.GetReturnValue().Set(obj);
        }
    } // namespace

    Vector2 &Vector2::normalize() {
        float len = glm::length(_vec);
        if (len > 0.0f)
            _vec /= len;
        return *this;
    }

    std::string Vector2::toString() const {
        std::ostringstream ss;
        ss << "Vector2(" << _vec.x << ", " << _vec.y << ")";
        return ss.str();
    }

    const Bind::Class<Vector2> &Vector2::Definition() {
        using Bind::Doc;
        using Bind::LuaMetaEvent;
        static const auto definition =
            Bind::Class<Vector2>("Vector2", "Mutable two-dimensional vector.")
                .Constructor<float, float>(Doc("Creates a vector from two numeric components.").Param("x", "Initial X component.").Param("y", "Initial Y component."))
                // Instance methods
                .Method("add", &Vector2::add, Doc("Adds another vector to this vector in place.").Param("other", "Vector to add component-wise.").Returns("This mutated vector for chaining."))
                .Method("sub", &Vector2::sub, Doc("Subtracts another vector from this vector in place.").Param("other", "Vector to subtract component-wise.").Returns("This mutated vector for chaining."))
                .Method("mul", &Vector2::mul, Doc("Multiplies this vector by a scalar in place.").Param("scalar", "Multiplier applied to both components.").Returns("This mutated vector for chaining."))
                .Method("div", &Vector2::div, Doc("Divides this vector by a scalar in place.").Param("scalar", "Divisor applied to both components; must be non-zero.").Returns("This mutated vector for chaining."))
                .Method("dot", &Vector2::dot, Doc("Computes the dot product without changing either vector.").Param("other", "Vector used for the dot product.").Returns("Scalar dot product."))
                .Method("normalize", &Vector2::normalize, Doc("Normalizes this vector in place; a zero vector remains unchanged.").Returns("This mutated vector for chaining."))
                .Method("lerp", &Vector2::lerp,
                    Doc("Linearly interpolates this vector toward a target in place.").Param("target", "Destination vector.").Param("t", "Interpolation factor; 0 keeps the current value and 1 reaches target.").Returns("This mutated vector for chaining."))
                .Method("set", &Vector2::set, Doc("Replaces both components in place.").Param("x", "Replacement X component.").Param("y", "Replacement Y component.").Returns("This mutated vector for chaining."))
                .Method("distance", &Vector2::distance, Doc("Computes Euclidean distance to another vector.").Param("other", "Vector to measure from this vector.").Returns("Distance between the two vectors."))
                .Method("clone", &Vector2::clone, Doc("Creates an independent copy of this vector.").Returns("New vector with the same components."))
                .Method("toString", &Vector2::toString, Doc("Formats this vector for logging and debugging.").Returns("Text in Vector2(x, y) form."))
                .Property("x", &Vector2::getX, &Vector2::setX, "Mutable X component.")
                .Property("y", &Vector2::getY, &Vector2::setY, "Mutable Y component.")
                .Property("length", &Vector2::getLength, "Read-only Euclidean magnitude of this vector.")
                .Property("lengthSquared", &Vector2::getLengthSquared, "Read-only squared magnitude, avoiding a square-root calculation.")
                // Static factory methods
                .Static("zero", &Vector2::zero, Doc("Creates a vector whose components are zero.").Returns("New Vector2(0, 0)."))
                .Static("one", &Vector2::one, Doc("Creates a vector whose components are one.").Returns("New Vector2(1, 1)."))
                // JavaScript only, until Lua has JSON.
                .V8Hook([](v8pp::class_<Vector2> &cls, v8::Isolate *) {
                    cls.prototype_function("toJSON", &ToJson, v8pp::metadata::docs("{ x: number; y: number }", {}, "Converts this vector to a plain object for JSON.stringify.", "Object containing the current components."));
                })
                // Lua operators. Each gives a new vector and leaves its operands as they were.
                .LuaMeta(LuaMetaEvent::Add,
                    [](const Vector2 &left, const Vector2 &right) {
                        return Vector2(left.vec() + right.vec());
                    })
                .LuaMeta(LuaMetaEvent::Sub,
                    [](const Vector2 &left, const Vector2 &right) {
                        return Vector2(left.vec() - right.vec());
                    })
                .LuaMeta(LuaMetaEvent::Mul,
                    [](const Vector2 &vector, float scalar) {
                        return Vector2(vector.vec() * scalar);
                    })
                .LuaMeta(LuaMetaEvent::Mul,
                    [](float scalar, const Vector2 &vector) {
                        return Vector2(scalar * vector.vec());
                    })
                .LuaMeta(LuaMetaEvent::Div,
                    [](const Vector2 &vector, float scalar) {
                        return Vector2(vector.vec() / scalar);
                    })
                .LuaMeta(LuaMetaEvent::Unm,
                    [](const Vector2 &vector) {
                        return Vector2(-vector.vec());
                    })
                .LuaMeta(LuaMetaEvent::Eq, [](const Vector2 &left, const Vector2 &right) {
                    return left.vec() == right.vec();
                });
        return definition;
    }

    v8pp::class_<Vector2> &Vector2::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            return Definition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), ScriptingSide(isolate));
        });
    }

    void Vector2::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<Vector2> &cls = GetClass(isolate);
        auto ctx                   = isolate->GetCurrentContext();
        global->Set(ctx, v8pp::to_v8(isolate, "Vector2"), cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
    }

    v8::Local<v8::Object> Vector2::NewInstance(v8::Isolate *isolate, const glm::vec2 &value) {
        v8pp::class_<Vector2> &cls = GetClass(isolate);
        return cls.import_external(isolate, new Vector2(value));
    }

    void Vector2::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.Unregister(isolate);
    }

    void Vector2::RegisterLua(lua_State *L, Bind::Side side) {
        Definition().MaterialiseLua(L, side);
        lua_pushglobaltable(L);
        Definition().PublishLua(L, -1);
        lua_pop(L, 1);
    }

} // namespace Framework::Scripting::Builtins
