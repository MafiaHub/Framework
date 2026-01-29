#include "vector2.h"

#include <sstream>

namespace Framework::Scripting::Builtins {

std::unique_ptr<v8pp::class_<Vector2>> Vector2::_class;

Vector2 Vector2::normalize() const {
    float len = glm::length(_vec);
    return len > 0.0f ? Vector2(_vec / len) : Vector2();
}

std::string Vector2::toString() const {
    std::ostringstream ss;
    ss << "Vector2(" << _vec.x << ", " << _vec.y << ")";
    return ss.str();
}

v8pp::class_<Vector2>& Vector2::GetClass(v8::Isolate* isolate) {
    if (!_class) {
        _class = std::make_unique<v8pp::class_<Vector2>>(isolate);
        _class->ctor<float, float>()
            // Instance methods
            .set("add", &Vector2::add)
            .set("sub", &Vector2::sub)
            .set("mul", &Vector2::mul)
            .set("div", &Vector2::div)
            .set("dot", &Vector2::dot)
            .set("normalize", &Vector2::normalize)
            .set("lerp", &Vector2::lerp)
            .set("distance", &Vector2::distance)
            .set("clone", &Vector2::clone)
            .set("toString", &Vector2::toString)
            // Getter methods (workaround for v8pp property issues with modern V8)
            .set("getX", &Vector2::getX)
            .set("getY", &Vector2::getY)
            .set("setX", &Vector2::setX)
            .set("setY", &Vector2::setY)
            .set("getLength", &Vector2::getLength)
            .set("getLengthSquared", &Vector2::getLengthSquared);

        // Add properties manually using v8's SetAccessor with correct signature
        auto protoTemplate = _class->class_function_template()->PrototypeTemplate();

        // Property: x
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "x").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getX());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setX(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });

        // Property: y
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "y").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getY());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setY(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });

        // Read-only property: length
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "length").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getLength());
            });

        // Read-only property: lengthSquared
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "lengthSquared").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Vector2>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getLengthSquared());
            });

        // Static methods need to be added to the js_function_template
        auto func = _class->js_function_template();
        func->Set(isolate, "zero", v8pp::wrap_function_template(isolate, &Vector2::zero));
        func->Set(isolate, "one", v8pp::wrap_function_template(isolate, &Vector2::one));
    }
    return *_class;
}

void Vector2::Register(v8::Isolate* isolate, v8::Local<v8::Object> global) {
    v8pp::class_<Vector2>& cls = GetClass(isolate);
    auto ctx = isolate->GetCurrentContext();
    global->Set(ctx, v8pp::to_v8(isolate, "Vector2"),
                cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
}

} // namespace Framework::Scripting::Builtins
