#include "color.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace Framework::Scripting::Builtins {

std::unordered_map<v8::Isolate*, std::unique_ptr<v8pp::class_<Color>>> Color::_classes;

std::string Color::toHex(bool includeAlpha) const {
    int r = static_cast<int>(std::round(_color.r * 255.0f));
    int g = static_cast<int>(std::round(_color.g * 255.0f));
    int b = static_cast<int>(std::round(_color.b * 255.0f));
    int a = static_cast<int>(std::round(_color.a * 255.0f));

    r = std::max(0, std::min(255, r));
    g = std::max(0, std::min(255, g));
    b = std::max(0, std::min(255, b));
    a = std::max(0, std::min(255, a));

    std::ostringstream ss;
    ss << "#" << std::hex << std::setfill('0');
    ss << std::setw(2) << r << std::setw(2) << g << std::setw(2) << b;
    if (includeAlpha) {
        ss << std::setw(2) << a;
    }
    return ss.str();
}

std::string Color::toString() const {
    std::ostringstream ss;
    ss << "Color(" << _color.r << ", " << _color.g << ", " << _color.b << ", " << _color.a << ")";
    return ss.str();
}

Color Color::fromHex(std::string_view hexInput) {
    std::string hex(hexInput);

    // Remove # prefix if present
    if (!hex.empty() && hex[0] == '#') {
        hex = hex.substr(1);
    }

    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    if (hex.length() != 6 && hex.length() != 8) {
        return Color(r, g, b, a);
    }
    try {
        r = static_cast<float>(std::stoi(hex.substr(0, 2), nullptr, 16)) / 255.0f;
        g = static_cast<float>(std::stoi(hex.substr(2, 2), nullptr, 16)) / 255.0f;
        b = static_cast<float>(std::stoi(hex.substr(4, 2), nullptr, 16)) / 255.0f;
        if (hex.length() == 8) {
            a = static_cast<float>(std::stoi(hex.substr(6, 2), nullptr, 16)) / 255.0f;
        }
    } catch (...) {
        // Invalid hex, use defaults
    }

    return Color(r, g, b, a);
}

Color Color::fromRGB(float r, float g, float b, float a) {
    return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

v8pp::class_<Color>& Color::GetClass(v8::Isolate* isolate) {
    auto it = _classes.find(isolate);
    if (it != _classes.end()) {
        return *it->second;
    }

    auto& cls = _classes[isolate];
    cls = std::make_unique<v8pp::class_<Color>>(isolate);
    cls->auto_wrap_objects(true);  // Enable auto-wrapping for return values
    cls->ctor<float, float, float, float>()
        // Instance methods
        .function("lerp", &Color::lerp)
        .function("set", &Color::set)
        .function("clone", &Color::clone)
        .function("toHex", &Color::toHex)
        .function("toString", &Color::toString);

    // Add properties manually using v8's SetNativeDataProperty with correct signature
    auto protoTemplate = cls->class_function_template()->PrototypeTemplate();

    // Property: r
    protoTemplate->SetNativeDataProperty(
        v8pp::to_v8(isolate, "r").As<v8::Name>(),
        [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* self = v8pp::class_<Color>::unwrap_object(info.GetIsolate(), info.This());
            if (self) info.GetReturnValue().Set(self->getR());
        },
        [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            auto* self = v8pp::class_<Color>::unwrap_object(info.GetIsolate(), info.This());
            if (self && value->IsNumber()) {
                self->setR(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
            }
        });

    // Property: g
    protoTemplate->SetNativeDataProperty(
        v8pp::to_v8(isolate, "g").As<v8::Name>(),
        [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* self = v8pp::class_<Color>::unwrap_object(info.GetIsolate(), info.This());
            if (self) info.GetReturnValue().Set(self->getG());
        },
        [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            auto* self = v8pp::class_<Color>::unwrap_object(info.GetIsolate(), info.This());
            if (self && value->IsNumber()) {
                self->setG(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
            }
        });

    // Property: b
    protoTemplate->SetNativeDataProperty(
        v8pp::to_v8(isolate, "b").As<v8::Name>(),
        [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* self = v8pp::class_<Color>::unwrap_object(info.GetIsolate(), info.This());
            if (self) info.GetReturnValue().Set(self->getB());
        },
        [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            auto* self = v8pp::class_<Color>::unwrap_object(info.GetIsolate(), info.This());
            if (self && value->IsNumber()) {
                self->setB(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
            }
        });

    // Property: a
    protoTemplate->SetNativeDataProperty(
        v8pp::to_v8(isolate, "a").As<v8::Name>(),
        [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* self = v8pp::class_<Color>::unwrap_object(info.GetIsolate(), info.This());
            if (self) info.GetReturnValue().Set(self->getA());
        },
        [](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            auto* self = v8pp::class_<Color>::unwrap_object(info.GetIsolate(), info.This());
            if (self && value->IsNumber()) {
                self->setA(static_cast<float>(value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0)));
            }
        });

    // toJSON method for JSON.stringify support - returns plain JS object
    protoTemplate->Set(
        v8pp::to_v8(isolate, "toJSON"),
        v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            auto* self = v8pp::class_<Color>::unwrap_object(info.GetIsolate(), info.This());
            if (!self) return;

            auto iso = info.GetIsolate();
            auto ctx = iso->GetCurrentContext();
            auto obj = v8::Object::New(iso);
            obj->Set(ctx, v8pp::to_v8(iso, "r"), v8::Number::New(iso, self->getR())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "g"), v8::Number::New(iso, self->getG())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "b"), v8::Number::New(iso, self->getB())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "a"), v8::Number::New(iso, self->getA())).Check();
            info.GetReturnValue().Set(obj);
        }));

    // Static methods need to be added to the js_function_template
    auto func = cls->js_function_template();
    func->Set(isolate, "fromHex", v8pp::wrap_function_template(isolate, &Color::fromHex));
    func->Set(isolate, "fromRGB", v8pp::wrap_function_template(isolate, &Color::fromRGB));
    func->Set(isolate, "white", v8pp::wrap_function_template(isolate, &Color::white));
    func->Set(isolate, "black", v8pp::wrap_function_template(isolate, &Color::black));
    func->Set(isolate, "red", v8pp::wrap_function_template(isolate, &Color::red));
    func->Set(isolate, "green", v8pp::wrap_function_template(isolate, &Color::green));
    func->Set(isolate, "blue", v8pp::wrap_function_template(isolate, &Color::blue));
    func->Set(isolate, "yellow", v8pp::wrap_function_template(isolate, &Color::yellow));
    func->Set(isolate, "cyan", v8pp::wrap_function_template(isolate, &Color::cyan));
    func->Set(isolate, "magenta", v8pp::wrap_function_template(isolate, &Color::magenta));
    func->Set(isolate, "transparent", v8pp::wrap_function_template(isolate, &Color::transparent));

    return *cls;
}

void Color::Register(v8::Isolate* isolate, v8::Local<v8::Object> global) {
    v8pp::class_<Color>& cls = GetClass(isolate);
    auto ctx = isolate->GetCurrentContext();
    global->Set(ctx, v8pp::to_v8(isolate, "Color"),
                cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
}

} // namespace Framework::Scripting::Builtins
