/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "quaternion.h"
#include "../scripting_catalog.h"
#include "vector3.h"

#include <sstream>

namespace Framework::Scripting::Builtins {

    std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Quaternion>>> Quaternion::_classes;

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

    v8pp::class_<Quaternion> &Quaternion::GetClass(v8::Isolate *isolate) {
        auto it = _classes.find(isolate);
        if (it != _classes.end()) {
            return *it->second;
        }

        auto &cls = _classes[isolate];
        cls       = std::make_unique<v8pp::class_<Quaternion>>(isolate, GetScriptingCatalog(isolate), "Quaternion", "Mutable quaternion exposed as Core.Quaternion for three-dimensional rotations.");
        cls->auto_wrap_objects(true); // Enable auto-wrapping for return values
        cls->ctor<float, float, float, float>(v8pp::metadata::docs("void",
                                                  {
                                                      v8pp::metadata::param("w", "number", false, "Initial scalar component."),
                                                      v8pp::metadata::param("x", "number", false, "Initial X imaginary component."),
                                                      v8pp::metadata::param("y", "number", false, "Initial Y imaginary component."),
                                                      v8pp::metadata::param("z", "number", false, "Initial Z imaginary component."),
                                                  },
                                                  "Creates a quaternion in w, x, y, z component order."))
            // Instance methods
            // Named mul for parity with Vector.mul (was multiply).
            .function("mul", &Quaternion::multiply,
                v8pp::metadata::docs("this", {v8pp::metadata::param("other", "Quaternion", false, "Rotation composed after this quaternion.")}, "Composes this rotation with another quaternion in place.", "This mutated quaternion for chaining."))
            .function("normalize", &Quaternion::normalize, v8pp::metadata::docs("this", {}, "Normalizes this quaternion in place.", "This unit quaternion for chaining."))
            .function("conjugate", &Quaternion::conjugate, v8pp::metadata::docs("Quaternion", {}, "Computes the conjugate without changing this quaternion.", "New conjugated quaternion."))
            .function("inverse", &Quaternion::inverse, v8pp::metadata::docs("Quaternion", {}, "Computes the inverse rotation without changing this quaternion.", "New inverse quaternion."))
            .function("slerp", &Quaternion::slerp,
                v8pp::metadata::docs("this", {v8pp::metadata::param("target", "Quaternion", false, "Destination rotation."), v8pp::metadata::param("t", "number", false, "Interpolation factor; 0 keeps the current rotation and 1 reaches target.")},
                    "Spherically interpolates this quaternion toward a target in place.", "This mutated quaternion for chaining."))
            .function("dot", &Quaternion::dot, v8pp::metadata::docs("number", {v8pp::metadata::param("other", "Quaternion", false, "Quaternion used for the dot product.")}, "Computes the quaternion dot product.", "Scalar dot product."))
            .function("rotateVector", &Quaternion::rotateVector, v8pp::metadata::docs("Vector3", {v8pp::metadata::param("vector", "Vector3", false, "Vector to rotate.")}, "Applies this rotation to a vector without mutating either value.", "New rotated vector."))
            .function("toEuler", &Quaternion::toEuler, v8pp::metadata::docs("Vector3", {}, "Converts this rotation to Euler angles in radians.", "Pitch, yaw, and roll as a Vector3."))
            .function("set", &Quaternion::set,
                v8pp::metadata::docs("this",
                    {v8pp::metadata::param("w", "number", false, "Replacement scalar component."), v8pp::metadata::param("x", "number", false, "Replacement X imaginary component."), v8pp::metadata::param("y", "number", false, "Replacement Y imaginary component."),
                        v8pp::metadata::param("z", "number", false, "Replacement Z imaginary component.")},
                    "Replaces all quaternion components in place.", "This mutated quaternion for chaining."))
            .function("clone", &Quaternion::clone, v8pp::metadata::docs("Quaternion", {}, "Creates an independent copy of this quaternion.", "New quaternion with the same components."))
            .function("toString", &Quaternion::toString, v8pp::metadata::docs("string", {}, "Formats this quaternion for logging and debugging.", "Text in Quaternion(w, x, y, z) form."));

        // Add properties manually using v8's SetNativeDataProperty with correct signature
        auto protoTemplate = cls->class_function_template()->PrototypeTemplate();

        // Property: w
        protoTemplate->SetNativeDataProperty(
            v8pp::to_v8(isolate, "w").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self)
                    info.GetReturnValue().Set(self->getW());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
                auto *self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setW(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });
        cls->document_property("w", v8pp::metadata::property_docs("number", "Mutable scalar component."));

        // Property: x
        protoTemplate->SetNativeDataProperty(
            v8pp::to_v8(isolate, "x").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self)
                    info.GetReturnValue().Set(self->getX());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
                auto *self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setX(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });
        cls->document_property("x", v8pp::metadata::property_docs("number", "Mutable X imaginary component."));

        // Property: y
        protoTemplate->SetNativeDataProperty(
            v8pp::to_v8(isolate, "y").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self)
                    info.GetReturnValue().Set(self->getY());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
                auto *self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setY(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });
        cls->document_property("y", v8pp::metadata::property_docs("number", "Mutable Y imaginary component."));

        // Property: z
        protoTemplate->SetNativeDataProperty(
            v8pp::to_v8(isolate, "z").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self)
                    info.GetReturnValue().Set(self->getZ());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
                auto *self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setZ(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });
        cls->document_property("z", v8pp::metadata::property_docs("number", "Mutable Z imaginary component."));

        // Read-only property: length (magnitude / norm)
        protoTemplate->SetNativeDataProperty(v8pp::to_v8(isolate, "length").As<v8::Name>(), [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
            if (self)
                info.GetReturnValue().Set(self->getLength());
        });
        cls->document_property("length", {"Read-only magnitude (norm) of this quaternion; 1 for a unit rotation.", "number", true, false});

        // Read-only property: lengthSquared
        protoTemplate->SetNativeDataProperty(v8pp::to_v8(isolate, "lengthSquared").As<v8::Name>(), [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
            if (self)
                info.GetReturnValue().Set(self->getLengthSquared());
        });
        cls->document_property("lengthSquared", {"Read-only squared magnitude, avoiding a square-root calculation.", "number", true, false});

        // toJSON method for JSON.stringify support - returns plain JS object
        protoTemplate->Set(v8pp::to_v8(isolate, "toJSON"), v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
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
        }));

        // Static methods need to be added to the js_function_template
        auto func = cls->js_function_template();
        func->Set(isolate, "identity", v8pp::wrap_function_template(isolate, &Quaternion::identity));
        func->Set(isolate, "fromEuler", v8pp::wrap_function_template(isolate, &Quaternion::fromEuler));
        func->Set(isolate, "fromAxisAngle", v8pp::wrap_function_template(isolate, &Quaternion::fromAxisAngle));

        auto &metadata = GetScriptingCatalog(isolate).constructor("Quaternion");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("toJSON", v8pp::metadata::docs("{ w: number; x: number; y: number; z: number }", {}, "Converts this quaternion to a plain object for JSON.stringify.", "Object containing the current components.")));
        metadata.record(v8pp::metadata::function_of<decltype(&Quaternion::identity)>("identity", v8pp::metadata::docs("Quaternion", {}, "Creates the identity rotation.", "New Quaternion(1, 0, 0, 0)."), true));
        metadata.record(v8pp::metadata::function_of<decltype(&Quaternion::fromEuler)>("fromEuler",
            v8pp::metadata::docs("Quaternion", {v8pp::metadata::param("pitch", "number", false, "Pitch angle in radians."), v8pp::metadata::param("yaw", "number", false, "Yaw angle in radians."), v8pp::metadata::param("roll", "number", false, "Roll angle in radians.")},
                "Creates a rotation from Euler angles.", "New rotation quaternion."),
            true));
        metadata.record(v8pp::metadata::function_of<decltype(&Quaternion::fromAxisAngle)>("fromAxisAngle",
            v8pp::metadata::docs("Quaternion", {v8pp::metadata::param("axis", "Vector3", false, "Rotation axis; it is normalized internally."), v8pp::metadata::param("angle", "number", false, "Rotation angle in radians.")}, "Creates a rotation around an axis.",
                "New axis-angle rotation quaternion."),
            true));

        return *cls;
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
        _classes.erase(isolate);
    }

} // namespace Framework::Scripting::Builtins
