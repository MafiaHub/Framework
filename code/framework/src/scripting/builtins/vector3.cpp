/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "vector3.h"
#include "../scripting_catalog.h"

#include <sstream>

namespace Framework::Scripting::Builtins {

    std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Vector3>>> Vector3::_classes;

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

    v8pp::class_<Vector3> &Vector3::GetClass(v8::Isolate *isolate) {
        auto it = _classes.find(isolate);
        if (it != _classes.end()) {
            return *it->second;
        }

        auto &cls = _classes[isolate];
        cls       = std::make_unique<v8pp::class_<Vector3>>(isolate, GetScriptingCatalog(isolate), "Vector3", "Mutable three-dimensional vector exposed as Core.Vector3 for positions, directions, and Euler angles.");
        cls->auto_wrap_objects(true); // Enable auto-wrapping for return values
        cls->ctor<float, float, float>(v8pp::metadata::docs("void",
                                           {
                                               v8pp::metadata::param("x", "number", false, "Initial X component."),
                                               v8pp::metadata::param("y", "number", false, "Initial Y component."),
                                               v8pp::metadata::param("z", "number", false, "Initial Z component."),
                                           },
                                           "Creates a vector from three numeric components."))
            // Instance methods
            .function("add", &Vector3::add, v8pp::metadata::docs("this", {v8pp::metadata::param("other", "Vector3", false, "Vector to add component-wise.")}, "Adds another vector to this vector in place.", "This mutated vector for chaining."))
            .function("sub", &Vector3::sub, v8pp::metadata::docs("this", {v8pp::metadata::param("other", "Vector3", false, "Vector to subtract component-wise.")}, "Subtracts another vector from this vector in place.", "This mutated vector for chaining."))
            .function("mul", &Vector3::mul, v8pp::metadata::docs("this", {v8pp::metadata::param("scalar", "number", false, "Multiplier applied to every component.")}, "Multiplies this vector by a scalar in place.", "This mutated vector for chaining."))
            .function("div", &Vector3::div, v8pp::metadata::docs("this", {v8pp::metadata::param("scalar", "number", false, "Divisor applied to every component; must be non-zero.")}, "Divides this vector by a scalar in place.", "This mutated vector for chaining."))
            .function("dot", &Vector3::dot, v8pp::metadata::docs("number", {v8pp::metadata::param("other", "Vector3", false, "Vector used for the dot product.")}, "Computes the dot product without changing either vector.", "Scalar dot product."))
            .function("cross", &Vector3::cross,
                v8pp::metadata::docs("this", {v8pp::metadata::param("other", "Vector3", false, "Second vector in the cross product.")}, "Replaces this vector with its cross product against another vector.", "This mutated perpendicular vector for chaining."))
            .function("normalize", &Vector3::normalize, v8pp::metadata::docs("this", {}, "Normalizes this vector in place; a zero vector remains unchanged.", "This mutated vector for chaining."))
            .function("lerp", &Vector3::lerp,
                v8pp::metadata::docs("this",
                    {
                        v8pp::metadata::param("target", "Vector3", false, "Destination vector."),
                        v8pp::metadata::param("t", "number", false, "Interpolation factor; 0 keeps the current value and 1 reaches target."),
                    },
                    "Linearly interpolates this vector toward a target in place.", "This mutated vector for chaining."))
            .function("set", &Vector3::set,
                v8pp::metadata::docs("this",
                    {
                        v8pp::metadata::param("x", "number", false, "Replacement X component."),
                        v8pp::metadata::param("y", "number", false, "Replacement Y component."),
                        v8pp::metadata::param("z", "number", false, "Replacement Z component."),
                    },
                    "Replaces all components in place.", "This mutated vector for chaining."))
            .function("distance", &Vector3::distance, v8pp::metadata::docs("number", {v8pp::metadata::param("other", "Vector3", false, "Vector to measure from this vector.")}, "Computes Euclidean distance to another vector.", "Distance between the two vectors."))
            .function("clone", &Vector3::clone, v8pp::metadata::docs("Vector3", {}, "Creates an independent copy of this vector.", "New vector with the same components."))
            .function("toString", &Vector3::toString, v8pp::metadata::docs("string", {}, "Formats this vector for logging and debugging.", "Text in Vector3(x, y, z) form."));

        // Add properties manually using v8's SetNativeDataProperty with correct signature
        auto protoTemplate = cls->class_function_template()->PrototypeTemplate();

        // Property: x
        protoTemplate->SetNativeDataProperty(
            v8pp::to_v8(isolate, "x").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
                if (self)
                    info.GetReturnValue().Set(self->getX());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
                auto *self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setX(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });
        cls->document_property("x", v8pp::metadata::property_docs("number", "Mutable X component."));

        // Property: y
        protoTemplate->SetNativeDataProperty(
            v8pp::to_v8(isolate, "y").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
                if (self)
                    info.GetReturnValue().Set(self->getY());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
                auto *self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setY(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });
        cls->document_property("y", v8pp::metadata::property_docs("number", "Mutable Y component."));

        // Property: z
        protoTemplate->SetNativeDataProperty(
            v8pp::to_v8(isolate, "z").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
                if (self)
                    info.GetReturnValue().Set(self->getZ());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
                auto *self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setZ(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });
        cls->document_property("z", v8pp::metadata::property_docs("number", "Mutable Z component."));

        // Read-only property: length
        protoTemplate->SetNativeDataProperty(v8pp::to_v8(isolate, "length").As<v8::Name>(), [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
            if (self)
                info.GetReturnValue().Set(self->getLength());
        });
        cls->document_property("length", {"Read-only Euclidean magnitude of this vector.", "number", true, false});

        // Read-only property: lengthSquared
        protoTemplate->SetNativeDataProperty(v8pp::to_v8(isolate, "lengthSquared").As<v8::Name>(), [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
            if (self)
                info.GetReturnValue().Set(self->getLengthSquared());
        });
        cls->document_property("lengthSquared", {"Read-only squared magnitude, avoiding a square-root calculation.", "number", true, false});

        // toJSON method for JSON.stringify support - returns plain JS object
        protoTemplate->Set(v8pp::to_v8(isolate, "toJSON"), v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
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
        }));

        // Static methods need to be added to the js_function_template
        auto func = cls->js_function_template();
        func->Set(isolate, "zero", v8pp::wrap_function_template(isolate, &Vector3::zero));
        func->Set(isolate, "one", v8pp::wrap_function_template(isolate, &Vector3::one));
        func->Set(isolate, "up", v8pp::wrap_function_template(isolate, &Vector3::up));
        func->Set(isolate, "forward", v8pp::wrap_function_template(isolate, &Vector3::forward));
        func->Set(isolate, "right", v8pp::wrap_function_template(isolate, &Vector3::right));

        auto &metadata = GetScriptingCatalog(isolate).constructor("Vector3");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("toJSON", v8pp::metadata::docs("{ x: number; y: number; z: number }", {}, "Converts this vector to a plain object for JSON.stringify.", "Object containing the current components.")));
        metadata.record(v8pp::metadata::function_of<decltype(&Vector3::zero)>("zero", v8pp::metadata::docs("Vector3", {}, "Creates a zero vector.", "New Vector3(0, 0, 0)."), true));
        metadata.record(v8pp::metadata::function_of<decltype(&Vector3::one)>("one", v8pp::metadata::docs("Vector3", {}, "Creates a vector whose components are one.", "New Vector3(1, 1, 1)."), true));
        metadata.record(v8pp::metadata::function_of<decltype(&Vector3::up)>("up", v8pp::metadata::docs("Vector3", {}, "Creates the framework's positive-Y unit direction.", "New Vector3(0, 1, 0)."), true));
        metadata.record(v8pp::metadata::function_of<decltype(&Vector3::forward)>("forward", v8pp::metadata::docs("Vector3", {}, "Creates the framework's positive-Z unit direction.", "New Vector3(0, 0, 1)."), true));
        metadata.record(v8pp::metadata::function_of<decltype(&Vector3::right)>("right", v8pp::metadata::docs("Vector3", {}, "Creates the framework's positive-X unit direction.", "New Vector3(1, 0, 0)."), true));

        return *cls;
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

} // namespace Framework::Scripting::Builtins
