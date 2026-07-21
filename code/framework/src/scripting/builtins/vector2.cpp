/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "vector2.h"
#include "../scripting_catalog.h"

#include <sstream>

namespace Framework::Scripting::Builtins {

    std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Vector2>>> Vector2::_classes;

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

    v8pp::class_<Vector2> &Vector2::GetClass(v8::Isolate *isolate) {
        auto it = _classes.find(isolate);
        if (it != _classes.end()) {
            return *it->second;
        }

        auto &cls = _classes[isolate];
        cls       = std::make_unique<v8pp::class_<Vector2>>(isolate, GetScriptingCatalog(isolate), "Vector2", "Mutable two-dimensional vector exposed as Core.Vector2.");
        cls->auto_wrap_objects(true); // Enable auto-wrapping for return values
        cls->ctor<float, float>(v8pp::metadata::docs("void",
                                    {
                                        v8pp::metadata::param("x", "number", false, "Initial X component."),
                                        v8pp::metadata::param("y", "number", false, "Initial Y component."),
                                    },
                                    "Creates a vector from two numeric components."))
            // Instance methods
            .function("add", &Vector2::add, v8pp::metadata::docs("this", {v8pp::metadata::param("other", "Vector2", false, "Vector to add component-wise.")}, "Adds another vector to this vector in place.", "This mutated vector for chaining."))
            .function("sub", &Vector2::sub, v8pp::metadata::docs("this", {v8pp::metadata::param("other", "Vector2", false, "Vector to subtract component-wise.")}, "Subtracts another vector from this vector in place.", "This mutated vector for chaining."))
            .function("mul", &Vector2::mul, v8pp::metadata::docs("this", {v8pp::metadata::param("scalar", "number", false, "Multiplier applied to both components.")}, "Multiplies this vector by a scalar in place.", "This mutated vector for chaining."))
            .function("div", &Vector2::div, v8pp::metadata::docs("this", {v8pp::metadata::param("scalar", "number", false, "Divisor applied to both components; must be non-zero.")}, "Divides this vector by a scalar in place.", "This mutated vector for chaining."))
            .function("dot", &Vector2::dot, v8pp::metadata::docs("number", {v8pp::metadata::param("other", "Vector2", false, "Vector used for the dot product.")}, "Computes the dot product without changing either vector.", "Scalar dot product."))
            .function("normalize", &Vector2::normalize, v8pp::metadata::docs("this", {}, "Normalizes this vector in place; a zero vector remains unchanged.", "This mutated vector for chaining."))
            .function("lerp", &Vector2::lerp,
                v8pp::metadata::docs("this",
                    {
                        v8pp::metadata::param("target", "Vector2", false, "Destination vector."),
                        v8pp::metadata::param("t", "number", false, "Interpolation factor; 0 keeps the current value and 1 reaches target."),
                    },
                    "Linearly interpolates this vector toward a target in place.", "This mutated vector for chaining."))
            .function("set", &Vector2::set,
                v8pp::metadata::docs("this",
                    {
                        v8pp::metadata::param("x", "number", false, "Replacement X component."),
                        v8pp::metadata::param("y", "number", false, "Replacement Y component."),
                    },
                    "Replaces both components in place.", "This mutated vector for chaining."))
            .function("distance", &Vector2::distance, v8pp::metadata::docs("number", {v8pp::metadata::param("other", "Vector2", false, "Vector to measure from this vector.")}, "Computes Euclidean distance to another vector.", "Distance between the two vectors."))
            .function("clone", &Vector2::clone, v8pp::metadata::docs("Vector2", {}, "Creates an independent copy of this vector.", "New vector with the same components."))
            .function("toString", &Vector2::toString, v8pp::metadata::docs("string", {}, "Formats this vector for logging and debugging.", "Text in Vector2(x, y) form."));

        // Add properties manually using v8's SetNativeDataProperty with correct signature
        auto protoTemplate = cls->class_function_template()->PrototypeTemplate();

        // Property: x
        protoTemplate->SetNativeDataProperty(
            v8pp::to_v8(isolate, "x").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
                if (self)
                    info.GetReturnValue().Set(self->getX());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
                auto *self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setX(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });
        cls->document_property("x", v8pp::metadata::property_docs("number", "Mutable X component."));

        // Property: y
        protoTemplate->SetNativeDataProperty(
            v8pp::to_v8(isolate, "y").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
                if (self)
                    info.GetReturnValue().Set(self->getY());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
                auto *self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setY(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });
        cls->document_property("y", v8pp::metadata::property_docs("number", "Mutable Y component."));

        // Read-only property: length
        protoTemplate->SetNativeDataProperty(v8pp::to_v8(isolate, "length").As<v8::Name>(), [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
            if (self)
                info.GetReturnValue().Set(self->getLength());
        });
        cls->document_property("length", {"Read-only Euclidean magnitude of this vector.", "number", true, false});

        // Read-only property: lengthSquared
        protoTemplate->SetNativeDataProperty(v8pp::to_v8(isolate, "lengthSquared").As<v8::Name>(), [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
            if (self)
                info.GetReturnValue().Set(self->getLengthSquared());
        });
        cls->document_property("lengthSquared", {"Read-only squared magnitude, avoiding a square-root calculation.", "number", true, false});

        // toJSON method for JSON.stringify support - returns plain JS object
        protoTemplate->Set(v8pp::to_v8(isolate, "toJSON"), v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
            if (!self)
                return;

            auto iso = info.GetIsolate();
            auto ctx = iso->GetCurrentContext();
            auto obj = v8::Object::New(iso);
            obj->Set(ctx, v8pp::to_v8(iso, "x"), v8::Number::New(iso, self->getX())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "y"), v8::Number::New(iso, self->getY())).Check();
            info.GetReturnValue().Set(obj);
        }));

        // Static methods need to be added to the js_function_template
        auto func = cls->js_function_template();
        func->Set(isolate, "zero", v8pp::wrap_function_template(isolate, &Vector2::zero));
        func->Set(isolate, "one", v8pp::wrap_function_template(isolate, &Vector2::one));
        auto &metadata = GetScriptingCatalog(isolate).constructor("Vector2");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("toJSON", v8pp::metadata::docs("{ x: number; y: number }", {}, "Converts this vector to a plain object for JSON.stringify.", "Object containing the current components.")));
        metadata.record(v8pp::metadata::function_of<decltype(&Vector2::zero)>("zero", v8pp::metadata::docs("Vector2", {}, "Creates a vector whose components are zero.", "New Vector2(0, 0)."), true));
        metadata.record(v8pp::metadata::function_of<decltype(&Vector2::one)>("one", v8pp::metadata::docs("Vector2", {}, "Creates a vector whose components are one.", "New Vector2(1, 1)."), true));

        return *cls;
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
        _classes.erase(isolate);
    }

} // namespace Framework::Scripting::Builtins
