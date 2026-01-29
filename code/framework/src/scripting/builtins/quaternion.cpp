#include "quaternion.h"
#include "vector3.h"

#include <sstream>

namespace Framework::Scripting::Builtins {

std::unique_ptr<v8pp::class_<Quaternion>> Quaternion::_class;

Vector3 Quaternion::rotateVector(const Vector3& v) const {
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

Quaternion Quaternion::fromAxisAngle(const Vector3& axis, float angle) {
    glm::vec3 normalizedAxis = glm::normalize(axis.vec());
    return Quaternion(glm::angleAxis(angle, normalizedAxis));
}

v8pp::class_<Quaternion>& Quaternion::GetClass(v8::Isolate* isolate) {
    if (!_class) {
        _class = std::make_unique<v8pp::class_<Quaternion>>(isolate);
        _class->ctor<float, float, float, float>()
            // Instance methods
            .set("multiply", &Quaternion::multiply)
            .set("normalize", &Quaternion::normalize)
            .set("conjugate", &Quaternion::conjugate)
            .set("inverse", &Quaternion::inverse)
            .set("slerp", &Quaternion::slerp)
            .set("dot", &Quaternion::dot)
            .set("rotateVector", &Quaternion::rotateVector)
            .set("toEuler", &Quaternion::toEuler)
            .set("clone", &Quaternion::clone)
            .set("toString", &Quaternion::toString)
            // Getter/setter methods (workaround for v8pp property issues with modern V8)
            .set("getW", &Quaternion::getW)
            .set("setW", &Quaternion::setW)
            .set("getX", &Quaternion::getX)
            .set("setX", &Quaternion::setX)
            .set("getY", &Quaternion::getY)
            .set("setY", &Quaternion::setY)
            .set("getZ", &Quaternion::getZ)
            .set("setZ", &Quaternion::setZ);

        // Add properties manually using v8's SetAccessor with correct signature
        auto protoTemplate = _class->class_function_template()->PrototypeTemplate();

        // Property: w
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "w").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getW());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setW(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });

        // Property: x
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "x").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getX());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setX(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });

        // Property: y
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "y").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getY());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setY(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });

        // Property: z
        protoTemplate->SetAccessor(
            v8pp::to_v8(isolate, "z").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(self->getZ());
            },
            [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* self = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info.This());
                if (self && value->IsNumber()) {
                    self->setZ(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
                }
            });

        // Static methods need to be added to the js_function_template
        auto func = _class->js_function_template();
        func->Set(isolate, "identity", v8pp::wrap_function_template(isolate, &Quaternion::identity));
        func->Set(isolate, "fromEuler", v8pp::wrap_function_template(isolate, &Quaternion::fromEuler));
        func->Set(isolate, "fromAxisAngle", v8pp::wrap_function_template(isolate, &Quaternion::fromAxisAngle));
    }
    return *_class;
}

void Quaternion::Register(v8::Isolate* isolate, v8::Local<v8::Object> global) {
    v8pp::class_<Quaternion>& cls = GetClass(isolate);
    auto ctx = isolate->GetCurrentContext();
    global->Set(ctx, v8pp::to_v8(isolate, "Quaternion"),
                cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
}

} // namespace Framework::Scripting::Builtins
