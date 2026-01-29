#pragma once

#include <v8pp/convert.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Forward declare wrapper class
namespace Framework::Scripting::JS::Builtins {
    class Vector3;
}

namespace v8pp {

// glm::vec2 converter
template<>
struct convert<glm::vec2> {
    using from_type = glm::vec2;
    using to_type = v8::Local<v8::Object>;

    static bool is_valid(v8::Isolate*, v8::Local<v8::Value> value) {
        return value->IsObject();
    }

    static glm::vec2 from_v8(v8::Isolate* isolate, v8::Local<v8::Value> value) {
        if (!value->IsObject()) {
            throw std::invalid_argument("expected object for vec2");
        }
        auto obj = value.As<v8::Object>();
        auto ctx = isolate->GetCurrentContext();

        float x = 0, y = 0;
        v8::Local<v8::Value> xVal, yVal;
        if (obj->Get(ctx, v8pp::to_v8(isolate, "x")).ToLocal(&xVal) && xVal->IsNumber()) {
            x = static_cast<float>(xVal->NumberValue(ctx).FromMaybe(0.0));
        }
        if (obj->Get(ctx, v8pp::to_v8(isolate, "y")).ToLocal(&yVal) && yVal->IsNumber()) {
            y = static_cast<float>(yVal->NumberValue(ctx).FromMaybe(0.0));
        }
        return glm::vec2(x, y);
    }

    static v8::Local<v8::Object> to_v8(v8::Isolate* isolate, const glm::vec2& value);
};

// glm::vec3 converter
template<>
struct convert<glm::vec3> {
    using from_type = glm::vec3;
    using to_type = v8::Local<v8::Object>;

    static bool is_valid(v8::Isolate*, v8::Local<v8::Value> value) {
        return value->IsObject();
    }

    static glm::vec3 from_v8(v8::Isolate* isolate, v8::Local<v8::Value> value) {
        if (!value->IsObject()) {
            throw std::invalid_argument("expected object for vec3");
        }
        auto obj = value.As<v8::Object>();
        auto ctx = isolate->GetCurrentContext();

        float x = 0, y = 0, z = 0;
        v8::Local<v8::Value> xVal, yVal, zVal;
        if (obj->Get(ctx, v8pp::to_v8(isolate, "x")).ToLocal(&xVal) && xVal->IsNumber()) {
            x = static_cast<float>(xVal->NumberValue(ctx).FromMaybe(0.0));
        }
        if (obj->Get(ctx, v8pp::to_v8(isolate, "y")).ToLocal(&yVal) && yVal->IsNumber()) {
            y = static_cast<float>(yVal->NumberValue(ctx).FromMaybe(0.0));
        }
        if (obj->Get(ctx, v8pp::to_v8(isolate, "z")).ToLocal(&zVal) && zVal->IsNumber()) {
            z = static_cast<float>(zVal->NumberValue(ctx).FromMaybe(0.0));
        }
        return glm::vec3(x, y, z);
    }

    static v8::Local<v8::Object> to_v8(v8::Isolate* isolate, const glm::vec3& value);
};

// glm::vec4 converter
template<>
struct convert<glm::vec4> {
    using from_type = glm::vec4;
    using to_type = v8::Local<v8::Object>;

    static bool is_valid(v8::Isolate*, v8::Local<v8::Value> value) {
        return value->IsObject();
    }

    static glm::vec4 from_v8(v8::Isolate* isolate, v8::Local<v8::Value> value) {
        if (!value->IsObject()) {
            throw std::invalid_argument("expected object for vec4");
        }
        auto obj = value.As<v8::Object>();
        auto ctx = isolate->GetCurrentContext();

        float x = 0, y = 0, z = 0, w = 0;
        v8::Local<v8::Value> xVal, yVal, zVal, wVal;
        if (obj->Get(ctx, v8pp::to_v8(isolate, "x")).ToLocal(&xVal) && xVal->IsNumber()) {
            x = static_cast<float>(xVal->NumberValue(ctx).FromMaybe(0.0));
        }
        if (obj->Get(ctx, v8pp::to_v8(isolate, "y")).ToLocal(&yVal) && yVal->IsNumber()) {
            y = static_cast<float>(yVal->NumberValue(ctx).FromMaybe(0.0));
        }
        if (obj->Get(ctx, v8pp::to_v8(isolate, "z")).ToLocal(&zVal) && zVal->IsNumber()) {
            z = static_cast<float>(zVal->NumberValue(ctx).FromMaybe(0.0));
        }
        if (obj->Get(ctx, v8pp::to_v8(isolate, "w")).ToLocal(&wVal) && wVal->IsNumber()) {
            w = static_cast<float>(wVal->NumberValue(ctx).FromMaybe(0.0));
        }
        return glm::vec4(x, y, z, w);
    }

    static v8::Local<v8::Object> to_v8(v8::Isolate* isolate, const glm::vec4& value);
};

// glm::quat converter
template<>
struct convert<glm::quat> {
    using from_type = glm::quat;
    using to_type = v8::Local<v8::Object>;

    static bool is_valid(v8::Isolate*, v8::Local<v8::Value> value) {
        return value->IsObject();
    }

    static glm::quat from_v8(v8::Isolate* isolate, v8::Local<v8::Value> value) {
        if (!value->IsObject()) {
            throw std::invalid_argument("expected object for quat");
        }
        auto obj = value.As<v8::Object>();
        auto ctx = isolate->GetCurrentContext();

        float w = 1, x = 0, y = 0, z = 0;
        v8::Local<v8::Value> wVal, xVal, yVal, zVal;
        if (obj->Get(ctx, v8pp::to_v8(isolate, "w")).ToLocal(&wVal) && wVal->IsNumber()) {
            w = static_cast<float>(wVal->NumberValue(ctx).FromMaybe(1.0));
        }
        if (obj->Get(ctx, v8pp::to_v8(isolate, "x")).ToLocal(&xVal) && xVal->IsNumber()) {
            x = static_cast<float>(xVal->NumberValue(ctx).FromMaybe(0.0));
        }
        if (obj->Get(ctx, v8pp::to_v8(isolate, "y")).ToLocal(&yVal) && yVal->IsNumber()) {
            y = static_cast<float>(yVal->NumberValue(ctx).FromMaybe(0.0));
        }
        if (obj->Get(ctx, v8pp::to_v8(isolate, "z")).ToLocal(&zVal) && zVal->IsNumber()) {
            z = static_cast<float>(zVal->NumberValue(ctx).FromMaybe(0.0));
        }
        return glm::quat(w, x, y, z);
    }

    static v8::Local<v8::Object> to_v8(v8::Isolate* isolate, const glm::quat& value);
};

} // namespace v8pp

// Include Vector3 header for to_v8 implementation
#include "builtins/vector3.h"

namespace v8pp {

// Implementation of glm::vec3 to_v8 using Vector3 wrapper class
inline v8::Local<v8::Object> convert<glm::vec3>::to_v8(v8::Isolate* isolate, const glm::vec3& value) {
    return Framework::Scripting::JS::Builtins::Vector3::NewInstance(isolate, value);
}

} // namespace v8pp
