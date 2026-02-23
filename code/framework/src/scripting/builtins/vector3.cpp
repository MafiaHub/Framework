#include "vector3.h"

#include <sstream>

namespace Framework::Scripting::Builtins {

std::unordered_map<v8::Isolate*, std::unique_ptr<v8pp::class_<Vector3>>> Vector3::_classes;

Vector3& Vector3::normalize() {
    float len = glm::length(_vec);
    if (len > 0.0f) _vec /= len;
    return *this;
}

std::string Vector3::toString() const {
    std::ostringstream ss;
    ss << "Vector3(" << _vec.x << ", " << _vec.y << ", " << _vec.z << ")";
    return ss.str();
}

v8pp::class_<Vector3>& Vector3::GetClass(v8::Isolate* isolate) {
    auto it = _classes.find(isolate);
    if (it != _classes.end()) {
        return *it->second;
    }

    auto& cls = _classes[isolate];
    cls = std::make_unique<v8pp::class_<Vector3>>(isolate);
    cls->auto_wrap_objects(true);  // Enable auto-wrapping for return values
    cls->ctor<float, float, float>()
        // Instance methods
        .function("add", &Vector3::add)
        .function("sub", &Vector3::sub)
        .function("mul", &Vector3::mul)
        .function("div", &Vector3::div)
        .function("dot", &Vector3::dot)
        .function("cross", &Vector3::cross)
        .function("normalize", &Vector3::normalize)
        .function("lerp", &Vector3::lerp)
        .function("set", &Vector3::set)
        .function("distance", &Vector3::distance)
        .function("clone", &Vector3::clone)
        .function("toString", &Vector3::toString);

    // Add properties manually using v8's SetNativeDataProperty with correct signature
    auto protoTemplate = cls->class_function_template()->PrototypeTemplate();

    // Property: x
    protoTemplate->SetNativeDataProperty(
        v8pp::to_v8(isolate, "x").As<v8::Name>(),
        [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
            if (self) info.GetReturnValue().Set(self->getX());
        },
        [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            auto* self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
            if (self && value->IsNumber()) {
                self->setX(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
            }
        });

    // Property: y
    protoTemplate->SetNativeDataProperty(
        v8pp::to_v8(isolate, "y").As<v8::Name>(),
        [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
            if (self) info.GetReturnValue().Set(self->getY());
        },
        [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            auto* self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
            if (self && value->IsNumber()) {
                self->setY(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
            }
        });

    // Property: z
    protoTemplate->SetNativeDataProperty(
        v8pp::to_v8(isolate, "z").As<v8::Name>(),
        [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
            if (self) info.GetReturnValue().Set(self->getZ());
        },
        [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            auto* self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
            if (self && value->IsNumber()) {
                self->setZ(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
            }
        });

    // Read-only property: length
    protoTemplate->SetNativeDataProperty(
        v8pp::to_v8(isolate, "length").As<v8::Name>(),
        [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
            if (self) info.GetReturnValue().Set(self->getLength());
        });

    // Read-only property: lengthSquared
    protoTemplate->SetNativeDataProperty(
        v8pp::to_v8(isolate, "lengthSquared").As<v8::Name>(),
        [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
            if (self) info.GetReturnValue().Set(self->getLengthSquared());
        });

    // toJSON method for JSON.stringify support - returns plain JS object
    protoTemplate->Set(
        v8pp::to_v8(isolate, "toJSON"),
        v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            auto* self = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info.This());
            if (!self) return;

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

    return *cls;
}

void Vector3::Register(v8::Isolate* isolate, v8::Local<v8::Object> global) {
    v8pp::class_<Vector3>& cls = GetClass(isolate);
    auto ctx = isolate->GetCurrentContext();
    global->Set(ctx, v8pp::to_v8(isolate, "Vector3"),
                cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
}

v8::Local<v8::Object> Vector3::NewInstance(v8::Isolate* isolate, const glm::vec3& value) {
    v8pp::class_<Vector3>& cls = GetClass(isolate);
    return cls.import_external(isolate, new Vector3(value));
}

} // namespace Framework::Scripting::Builtins
