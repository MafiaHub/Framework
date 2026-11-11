/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "quaternion.h"
#include "../scripting_catalog.h"
#include "vector3.h"

#include <sstream>

namespace Framework::Scripting::Builtins {

    namespace {
        // toJSON method for JSON.stringify support - returns plain JS object
        void ToJson(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
            if (!self)
                return;

            auto iso = info.GetIsolate();
            auto ctx = iso->GetCurrentContext();
            auto obj = v8::Object::New(iso);
            obj->Set(ctx, v8pp::to_v8(iso, "w"), v8::Number::New(iso, self->getW())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "x"), v8::Number::New(iso, self->getX())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "y"), v8::Number::New(iso, self->getY())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "z"), v8::Number::New(iso, self->getZ())).Check();
            info.GetReturnValue().Set(obj);
        }
    } // namespace

    Vector3 Quaternion::rotateVector(const Vector3 &v) const {
        glm::vec3 result = _quat * v.vec();
        return Vector3(result);
    }

    Vector3 Quaternion::toEuler() const {
        glm::vec3 euler = glm::eulerAngles(_quat);
        return Vector3(euler.x, euler.y, euler.z);
    }

    std::string Quaternion::toString() const {
        std::ostringstream ss;
        ss << "Quaternion(" << _quat.w << ", " << _quat.x << ", " << _quat.y << ", " << _quat.z << ")";
        return ss.str();
    }

    Quaternion Quaternion::fromEuler(float pitch, float yaw, float roll) {
        return Quaternion(glm::quat(glm::vec3(pitch, yaw, roll)));
    }

    Quaternion Quaternion::fromAxisAngle(const Vector3 &axis, float angle) {
        float lengthSq = glm::dot(axis.vec(), axis.vec());
        if (lengthSq < 1e-12f) {
            return Quaternion::identity();
        }
        glm::vec3 normalizedAxis = axis.vec() / std::sqrt(lengthSq);
        return Quaternion(glm::angleAxis(angle, normalizedAxis));
    }

    const Bind::Class<Quaternion> &Quaternion::Definition() {
        using Bind::Doc;
        using Bind::LuaMetaEvent;
        static const auto definition =
            Bind::Class<Quaternion>("Quaternion", "Mutable quaternion for three-dimensional rotations. Components are scalar-first (w, x, y, z), matching GLM — not the x, y, z, w order some libraries use.")
                .Constructor<float, float, float, float>(Doc("Creates a quaternion in scalar-first w, x, y, z component order. Watch the trap: the scalar w is the first argument, unlike the x, y, z, w order used by some other libraries.")
                        .Param("w", "Initial scalar component (comes first — this is not x, y, z, w order).")
                        .Param("x", "Initial X imaginary component.")
                        .Param("y", "Initial Y imaginary component.")
                        .Param("z", "Initial Z imaginary component."))
                // Instance methods
                // Named mul for parity with Vector.mul (was multiply).
                .Method("mul", &Quaternion::multiply, Doc("Composes this rotation with another quaternion in place.").Param("other", "Rotation composed after this quaternion.").Returns("This mutated quaternion for chaining."))
                .Method("normalize", &Quaternion::normalize, Doc("Normalizes this quaternion in place; a zero quaternion becomes the identity rotation instead of NaN.").Returns("This unit quaternion for chaining."))
                .Method("conjugate", &Quaternion::conjugate, Doc("Computes the conjugate without changing this quaternion.").Returns("New conjugated quaternion."))
                .Method("inverse", &Quaternion::inverse, Doc("Computes the inverse rotation without changing this quaternion.").Returns("New inverse quaternion."))
                .Method("slerp", &Quaternion::slerp,
                    Doc("Spherically interpolates this quaternion toward a target in place.").Param("target", "Destination rotation.").Param("t", "Interpolation factor; 0 keeps the current rotation and 1 reaches target.").Returns("This mutated quaternion for chaining."))
                .Method("dot", &Quaternion::dot, Doc("Computes the quaternion dot product.").Param("other", "Quaternion used for the dot product.").Returns("Scalar dot product."))
                .Method("rotateVector", &Quaternion::rotateVector, Doc("Applies this rotation to a vector without mutating either value.").Param("vector", "Vector to rotate.").Returns("New rotated vector."))
                .Method("toEuler", &Quaternion::toEuler, Doc("Converts this rotation to Euler angles in radians.").Returns("Pitch, yaw, and roll as a Vector3."))
                .Method("set", &Quaternion::set,
                    Doc("Replaces all quaternion components in place.")
                        .Param("w", "Replacement scalar component.")
                        .Param("x", "Replacement X imaginary component.")
                        .Param("y", "Replacement Y imaginary component.")
                        .Param("z", "Replacement Z imaginary component.")
                        .Returns("This mutated quaternion for chaining."))
                .Method("clone", &Quaternion::clone, Doc("Creates an independent copy of this quaternion.").Returns("New quaternion with the same components."))
                .Method("toString", &Quaternion::toString, Doc("Formats this quaternion for logging and debugging.").Returns("Text in Quaternion(w, x, y, z) form."))
                .Property("w", &Quaternion::getW, &Quaternion::setW, "Mutable scalar component.")
                .Property("x", &Quaternion::getX, &Quaternion::setX, "Mutable X imaginary component.")
                .Property("y", &Quaternion::getY, &Quaternion::setY, "Mutable Y imaginary component.")
                .Property("z", &Quaternion::getZ, &Quaternion::setZ, "Mutable Z imaginary component.")
                .Property("length", &Quaternion::getLength, "Read-only magnitude (norm) of this quaternion; 1 for a unit rotation.")
                .Property("lengthSquared", &Quaternion::getLengthSquared, "Read-only squared magnitude, avoiding a square-root calculation.")
                // Static factory methods
                .Static("identity", &Quaternion::identity, Doc("Creates the identity rotation.").Returns("New Quaternion(1, 0, 0, 0)."))
                .Static("fromEuler", &Quaternion::fromEuler, Doc("Creates a rotation from Euler angles.").Param("pitch", "Pitch angle in radians.").Param("yaw", "Yaw angle in radians.").Param("roll", "Roll angle in radians.").Returns("New rotation quaternion."))
                .Static("fromAxisAngle", &Quaternion::fromAxisAngle, Doc("Creates a rotation around an axis.").Param("axis", "Rotation axis; it is normalized internally.").Param("angle", "Rotation angle in radians.").Returns("New axis-angle rotation quaternion."))
                // What JSON.stringify asks a JavaScript object for. Lua writes the same object from JsonProperties.
                .V8Hook([](v8pp::class_<Quaternion> &cls, v8::Isolate *) {
                    cls.prototype_function("toJSON", &ToJson, v8pp::metadata::docs("{ w: number; x: number; y: number; z: number }", {}, "Converts this quaternion to a plain object for JSON.stringify.", "Object containing the current components."));
                })
                .JsonProperties({"w", "x", "y", "z"})
                // Crosses between runtimes as its glm value.
                .Transfer(&Quaternion::quat)
                // Lua operators. The product is a new quaternion and leaves its operands as they were.
                .LuaMeta(LuaMetaEvent::Mul,
                    [](const Quaternion &left, const Quaternion &right) {
                        return Quaternion(left.quat() * right.quat());
                    })
                .LuaMeta(LuaMetaEvent::Eq, [](const Quaternion &left, const Quaternion &right) {
                    return left.quat() == right.quat();
                });
        return definition;
    }

    v8pp::class_<Quaternion> &Quaternion::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            return Definition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), ScriptingSide(isolate));
        });
    }

    void Quaternion::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<Quaternion> &cls = GetClass(isolate);
        auto ctx                      = isolate->GetCurrentContext();
        global->Set(ctx, v8pp::to_v8(isolate, "Quaternion"), cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
    }

    v8::Local<v8::Object> Quaternion::NewInstance(v8::Isolate *isolate, const glm::quat &value) {
        v8pp::class_<Quaternion> &cls = GetClass(isolate);
        return cls.import_external(isolate, new Quaternion(value));
    }

    void Quaternion::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.Unregister(isolate);
    }

    void Quaternion::RegisterLua(lua_State *L, Bind::Side side) {
        Definition().RegisterLuaGlobal(L, side);
    }

} // namespace Framework::Scripting::Builtins
