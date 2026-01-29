#include "vector4.h"

#include <sstream>

namespace Framework::Scripting::Builtins {

std::unique_ptr<v8pp::class_<Vector4>> Vector4::_class;

Vector4 Vector4::normalize() const {
    float len = glm::length(_vec);
    return len > 0.0f ? Vector4(_vec / len) : Vector4();
}

std::string Vector4::toString() const {
    std::ostringstream ss;
    ss << "Vector4(" << _vec.x << ", " << _vec.y << ", " << _vec.z << ", " << _vec.w << ")";
    return ss.str();
}

v8pp::class_<Vector4>& Vector4::GetClass(v8::Isolate* isolate) {
    if (!_class) {
        _class = std::make_unique<v8pp::class_<Vector4>>(isolate);
        _class->auto_wrap_objects(true);  // Enable auto-wrapping for return values
        _class->ctor<float, float, float, float>()
            // Instance methods
            .set("add", &Vector4::add)
            .set("sub", &Vector4::sub)
            .set("mul", &Vector4::mul)
            .set("div", &Vector4::div)
            .set("dot", &Vector4::dot)
            .set("normalize", &Vector4::normalize)
            .set("lerp", &Vector4::lerp)
            .set("clone", &Vector4::clone)
            .set("toString", &Vector4::toString);

        // Add properties manually using v8's SetAccessor with correct signature
        auto protoTemplate = _class->class_function_template()->PrototypeTemplate();

        // Property: x
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "x").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Vector4>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getX());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* self = v8pp::class_<Vector4>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setX(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });

        // Property: y
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "y").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Vector4>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getY());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* self = v8pp::class_<Vector4>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setY(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });

        // Property: z
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "z").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Vector4>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getZ());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* self = v8pp::class_<Vector4>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setZ(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });

        // Property: w
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "w").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Vector4>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getW());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* self = v8pp::class_<Vector4>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setW(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });

        // Read-only property: length
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "length").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Vector4>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getLength());
            });

        // Read-only property: lengthSquared
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "lengthSquared").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Vector4>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getLengthSquared());
            });

        // toJSON method for JSON.stringify support - returns plain JS object
        protoTemplate->Set(
            v8pp::to_v8(isolate, "toJSON"),
            v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Vector4>::unwrap_object(info.GetIsolate(), info.This());
                if (!self) return;

                auto iso = info.GetIsolate();
                auto ctx = iso->GetCurrentContext();
                auto obj = v8::Object::New(iso);
                obj->Set(ctx, v8pp::to_v8(iso, "x"), v8::Number::New(iso, self->getX())).Check();
                obj->Set(ctx, v8pp::to_v8(iso, "y"), v8::Number::New(iso, self->getY())).Check();
                obj->Set(ctx, v8pp::to_v8(iso, "z"), v8::Number::New(iso, self->getZ())).Check();
                obj->Set(ctx, v8pp::to_v8(iso, "w"), v8::Number::New(iso, self->getW())).Check();
                info.GetReturnValue().Set(obj);
            }));

        // Static methods need to be added to the js_function_template
        auto func = _class->js_function_template();
        func->Set(isolate, "zero", v8pp::wrap_function_template(isolate, &Vector4::zero));
        func->Set(isolate, "one", v8pp::wrap_function_template(isolate, &Vector4::one));
    }
    return *_class;
}

void Vector4::Register(v8::Isolate* isolate, v8::Local<v8::Object> global) {
    v8pp::class_<Vector4>& cls = GetClass(isolate);
    auto ctx = isolate->GetCurrentContext();
    global->Set(ctx, v8pp::to_v8(isolate, "Vector4"),
                cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
}

} // namespace Framework::Scripting::Builtins
