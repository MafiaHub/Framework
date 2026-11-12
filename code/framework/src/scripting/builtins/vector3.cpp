/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "vector3.h"
#include "../scripting_catalog.h"
#include "vector_operators.h"

#include <sstream>

namespace Framework::Scripting::Builtins {

    namespace {
        // toJSON method for JSON.stringify support - returns plain JS object
        void ToJson(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
            if (!self)
                return;

            auto iso = info.GetIsolate();
            auto ctx = iso->GetCurrentContext();
            auto obj = v8::Object::New(iso);
            obj->Set(ctx, v8pp::to_v8(iso, "x"), v8::Number::New(iso, self->getX())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "y"), v8::Number::New(iso, self->getY())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "z"), v8::Number::New(iso, self->getZ())).Check();
            info.GetReturnValue().Set(obj);
        }
    } // namespace

    Vector3 &Vector3::normalize() {
        float len = glm::length(_vec);
        if (len > 0.0f)
            _vec /= len;
        return *this;
    }

    std::string Vector3::toString() const {
        std::ostringstream ss;
        ss << "Vector3(" << _vec.x << ", " << _vec.y << ", " << _vec.z << ")";
        return ss.str();
    }

    const Bind::Class<Vector3> &Vector3::Definition() {
        // AddVectorOperators gives the class its Lua operators.
        static const auto definition = AddVectorOperators(Bind::Class<Vector3>("Vector3", "Mutable three-dimensional vector for positions, directions, and Euler angles.")
                .Constructor<float, float, float>(Framework::Scripting::Bind::Doc("Creates a vector from three numeric components.").Param("x", "Initial X component.").Param("y", "Initial Y component.").Param("z", "Initial Z component."))
                // Instance methods
                .Method("add", &Vector3::add, Framework::Scripting::Bind::Doc("Adds another vector to this vector in place.").Param("other", "Vector to add component-wise.").Returns("This mutated vector for chaining."))
                .Method("sub", &Vector3::sub, Framework::Scripting::Bind::Doc("Subtracts another vector from this vector in place.").Param("other", "Vector to subtract component-wise.").Returns("This mutated vector for chaining."))
                .Method("mul", &Vector3::mul, Framework::Scripting::Bind::Doc("Multiplies this vector by a scalar in place.").Param("scalar", "Multiplier applied to every component.").Returns("This mutated vector for chaining."))
                .Method("div", &Vector3::div, Framework::Scripting::Bind::Doc("Divides this vector by a scalar in place.").Param("scalar", "Divisor applied to every component; must be non-zero.").Returns("This mutated vector for chaining."))
                .Method("dot", &Vector3::dot, Framework::Scripting::Bind::Doc("Computes the dot product without changing either vector.").Param("other", "Vector used for the dot product.").Returns("Scalar dot product."))
                .Method("cross", &Vector3::cross, Framework::Scripting::Bind::Doc("Replaces this vector with its cross product against another vector.").Param("other", "Second vector in the cross product.").Returns("This mutated perpendicular vector for chaining."))
                .Method("normalize", &Vector3::normalize, Framework::Scripting::Bind::Doc("Normalizes this vector in place; a zero vector remains unchanged.").Returns("This mutated vector for chaining."))
                .Method("lerp", &Vector3::lerp,
                    Framework::Scripting::Bind::Doc("Linearly interpolates this vector toward a target in place.").Param("target", "Destination vector.").Param("t", "Interpolation factor; 0 keeps the current value and 1 reaches target.").Returns("This mutated vector for chaining."))
                .Method("set", &Vector3::set, Framework::Scripting::Bind::Doc("Replaces all components in place.").Param("x", "Replacement X component.").Param("y", "Replacement Y component.").Param("z", "Replacement Z component.").Returns("This mutated vector for chaining."))
                .Method("distance", &Vector3::distance, Framework::Scripting::Bind::Doc("Computes Euclidean distance to another vector.").Param("other", "Vector to measure from this vector.").Returns("Distance between the two vectors."))
                .Method("clone", &Vector3::clone, Framework::Scripting::Bind::Doc("Creates an independent copy of this vector.").Returns("New vector with the same components."))
                .Method("toString", &Vector3::toString, Framework::Scripting::Bind::Doc("Formats this vector for logging and debugging.").Returns("Text in Vector3(x, y, z) form."))
                .Property("x", &Vector3::getX, &Vector3::setX, "Mutable X component.")
                .Property("y", &Vector3::getY, &Vector3::setY, "Mutable Y component.")
                .Property("z", &Vector3::getZ, &Vector3::setZ, "Mutable Z component.")
                .Property("length", &Vector3::getLength, "Read-only Euclidean magnitude of this vector.")
                .Property("lengthSquared", &Vector3::getLengthSquared, "Read-only squared magnitude, avoiding a square-root calculation.")
                // Static factory methods
                .Static("zero", &Vector3::zero, Framework::Scripting::Bind::Doc("Creates a zero vector.").Returns("New Vector3(0, 0, 0)."))
                .Static("one", &Vector3::one, Framework::Scripting::Bind::Doc("Creates a vector whose components are one.").Returns("New Vector3(1, 1, 1)."))
                .Static("up", &Vector3::up, Framework::Scripting::Bind::Doc("Creates the framework's positive-Y unit direction.").Returns("New Vector3(0, 1, 0)."))
                .Static("forward", &Vector3::forward, Framework::Scripting::Bind::Doc("Creates the framework's positive-Z unit direction.").Returns("New Vector3(0, 0, 1)."))
                .Static("right", &Vector3::right, Framework::Scripting::Bind::Doc("Creates the framework's positive-X unit direction.").Returns("New Vector3(1, 0, 0)."))
                // What JSON.stringify asks a JavaScript object for. Lua writes the same object from JsonProperties.
                .V8Hook([](v8pp::class_<Vector3> &cls, v8::Isolate *) {
                    cls.prototype_function("toJSON", &ToJson, v8pp::metadata::docs("{ x: number; y: number; z: number }", {}, "Converts this vector to a plain object for JSON.stringify.", "Object containing the current components."));
                })
                .JsonProperties({"x", "y", "z"})
                // Crosses between runtimes as its glm value.
                .Transfer(&Vector3::vec));
        return definition;
    }

    v8pp::class_<Vector3> &Vector3::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            return Definition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), ScriptingSide(isolate));
        });
    }

    void Vector3::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<Vector3> &cls = GetClass(isolate);
        auto ctx                   = isolate->GetCurrentContext();
        global->Set(ctx, v8pp::to_v8(isolate, "Vector3"), cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
    }

    v8::Local<v8::Object> Vector3::NewInstance(v8::Isolate *isolate, const glm::vec3 &value) {
        v8pp::class_<Vector3> &cls = GetClass(isolate);
        return cls.import_external(isolate, new Vector3(value));
    }

    void Vector3::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.Unregister(isolate);
    }

    void Vector3::RegisterLua(lua_State *L, Bind::Side side) {
        Definition().RegisterLuaGlobal(L, side);
    }

} // namespace Framework::Scripting::Builtins
