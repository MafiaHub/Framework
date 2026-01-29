#include "color.h"
#include "../v8_helpers.h"

#include <sstream>
#include <iomanip>
#include <cmath>

namespace Framework::Scripting::JS::Builtins {

    v8::Global<v8::FunctionTemplate> Color::_template;

    void Color::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8::Local<v8::FunctionTemplate> tmpl = GetTemplate(isolate);
        v8::Local<v8::Function> constructor = tmpl->GetFunction(isolate->GetCurrentContext()).ToLocalChecked();

        global
            ->Set(isolate->GetCurrentContext(), v8::String::NewFromUtf8(isolate, "Color").ToLocalChecked(), constructor)
            .Check();
    }

    v8::Local<v8::FunctionTemplate> Color::GetTemplate(v8::Isolate *isolate) {
        if (!_template.IsEmpty()) {
            return _template.Get(isolate);
        }

        v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate, New);
        tmpl->SetClassName(v8::String::NewFromUtf8(isolate, "Color").ToLocalChecked());
        tmpl->InstanceTemplate()->SetInternalFieldCount(1);

        v8::Local<v8::ObjectTemplate> instanceTmpl = tmpl->InstanceTemplate();
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "r").ToLocalChecked(), GetR, SetR);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "g").ToLocalChecked(), GetG, SetG);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "b").ToLocalChecked(), GetB, SetB);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "a").ToLocalChecked(), GetA, SetA);

        v8::Local<v8::ObjectTemplate> protoTmpl = tmpl->PrototypeTemplate();
        protoTmpl->Set(isolate, "lerp", v8::FunctionTemplate::New(isolate, Lerp));
        protoTmpl->Set(isolate, "clone", v8::FunctionTemplate::New(isolate, Clone));
        protoTmpl->Set(isolate, "toHex", v8::FunctionTemplate::New(isolate, ToHex));
        protoTmpl->Set(isolate, "toArray", v8::FunctionTemplate::New(isolate, ToArray));
        protoTmpl->Set(isolate, "toString", v8::FunctionTemplate::New(isolate, ToString));

        tmpl->Set(isolate, "fromHex", v8::FunctionTemplate::New(isolate, FromHex));
        tmpl->Set(isolate, "fromRGB", v8::FunctionTemplate::New(isolate, FromRGB));
        tmpl->Set(isolate, "white", v8::FunctionTemplate::New(isolate, White));
        tmpl->Set(isolate, "black", v8::FunctionTemplate::New(isolate, Black));
        tmpl->Set(isolate, "red", v8::FunctionTemplate::New(isolate, Red));
        tmpl->Set(isolate, "green", v8::FunctionTemplate::New(isolate, Green));
        tmpl->Set(isolate, "blue", v8::FunctionTemplate::New(isolate, Blue));
        tmpl->Set(isolate, "yellow", v8::FunctionTemplate::New(isolate, Yellow));
        tmpl->Set(isolate, "cyan", v8::FunctionTemplate::New(isolate, Cyan));
        tmpl->Set(isolate, "magenta", v8::FunctionTemplate::New(isolate, Magenta));
        tmpl->Set(isolate, "transparent", v8::FunctionTemplate::New(isolate, Transparent));

        _template.Reset(isolate, tmpl);
        return tmpl;
    }

    v8::Local<v8::Object> Color::NewInstance(v8::Isolate *isolate, float r, float g, float b, float a) {
        v8::Local<v8::FunctionTemplate> tmpl = GetTemplate(isolate);
        v8::Local<v8::Function> constructor = tmpl->GetFunction(isolate->GetCurrentContext()).ToLocalChecked();

        v8::Local<v8::Value> argv[] = {v8::Number::New(isolate, r), v8::Number::New(isolate, g),
                                       v8::Number::New(isolate, b), v8::Number::New(isolate, a)};

        return constructor->NewInstance(isolate->GetCurrentContext(), 4, argv).ToLocalChecked();
    }

    v8::Local<v8::Object> Color::NewInstance(v8::Isolate *isolate, const glm::vec4 &color) {
        return NewInstance(isolate, color.r, color.g, color.b, color.a);
    }

    glm::vec4 *Color::Unwrap(v8::Local<v8::Object> obj) {
        v8::Local<v8::Value> field = obj->GetInternalField(0);
        if (field.IsEmpty() || !field->IsExternal())
            return nullptr;
        return static_cast<glm::vec4 *>(field.As<v8::External>()->Value());
    }

    bool Color::IsInstance(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        if (_template.IsEmpty() || !value->IsObject())
            return false;
        return _template.Get(isolate)->HasInstance(value);
    }

    void Color::New(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        if (!args.IsConstructCall()) {
            V8Helpers::ThrowTypeError(isolate, "Color must be called with new");
            return;
        }

        float r = V8Helpers::GetFloat(args, 0, 1.0f);
        float g = V8Helpers::GetFloat(args, 1, 1.0f);
        float b = V8Helpers::GetFloat(args, 2, 1.0f);
        float a = V8Helpers::GetFloat(args, 3, 1.0f);

        glm::vec4 *color = new glm::vec4(r, g, b, a);
        args.This()->SetInternalField(0, v8::External::New(isolate, color));
        args.GetReturnValue().Set(args.This());
    }

    void Color::GetR(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec4 *color = Unwrap(info.Holder());
        if (color)
            info.GetReturnValue().Set(color->r);
    }

    void Color::SetR(v8::Local<v8::String>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
        glm::vec4 *color = Unwrap(info.Holder());
        if (color && value->IsNumber())
            color->r = static_cast<float>(value.As<v8::Number>()->Value());
    }

    void Color::GetG(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec4 *color = Unwrap(info.Holder());
        if (color)
            info.GetReturnValue().Set(color->g);
    }

    void Color::SetG(v8::Local<v8::String>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
        glm::vec4 *color = Unwrap(info.Holder());
        if (color && value->IsNumber())
            color->g = static_cast<float>(value.As<v8::Number>()->Value());
    }

    void Color::GetB(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec4 *color = Unwrap(info.Holder());
        if (color)
            info.GetReturnValue().Set(color->b);
    }

    void Color::SetB(v8::Local<v8::String>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
        glm::vec4 *color = Unwrap(info.Holder());
        if (color && value->IsNumber())
            color->b = static_cast<float>(value.As<v8::Number>()->Value());
    }

    void Color::GetA(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec4 *color = Unwrap(info.Holder());
        if (color)
            info.GetReturnValue().Set(color->a);
    }

    void Color::SetA(v8::Local<v8::String>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
        glm::vec4 *color = Unwrap(info.Holder());
        if (color && value->IsNumber())
            color->a = static_cast<float>(value.As<v8::Number>()->Value());
    }

    void Color::Lerp(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *color = Unwrap(args.Holder());
        if (!color)
            return;

        glm::vec4 target(1.0f);
        float t = 0.5f;

        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::vec4 *targetColor = Unwrap(args[0].As<v8::Object>());
            if (targetColor)
                target = *targetColor;
            t = V8Helpers::GetFloat(args, 1, 0.5f);
        } else {
            target.r = V8Helpers::GetFloat(args, 0, 1.0f);
            target.g = V8Helpers::GetFloat(args, 1, 1.0f);
            target.b = V8Helpers::GetFloat(args, 2, 1.0f);
            target.a = V8Helpers::GetFloat(args, 3, 1.0f);
            t = V8Helpers::GetFloat(args, 4, 0.5f);
        }

        args.GetReturnValue().Set(NewInstance(isolate, glm::mix(*color, target, t)));
    }

    void Color::Clone(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *color = Unwrap(args.Holder());
        if (color)
            args.GetReturnValue().Set(NewInstance(isolate, *color));
    }

    void Color::ToHex(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *color = Unwrap(args.Holder());
        if (!color)
            return;

        bool includeAlpha = V8Helpers::GetBool(args, 0, false);

        int r = static_cast<int>(std::round(color->r * 255.0f));
        int g = static_cast<int>(std::round(color->g * 255.0f));
        int b = static_cast<int>(std::round(color->b * 255.0f));
        int a = static_cast<int>(std::round(color->a * 255.0f));

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

        args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, ss.str().c_str()).ToLocalChecked());
    }

    void Color::ToArray(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *color = Unwrap(args.Holder());
        if (!color)
            return;

        v8::Local<v8::Array> arr = v8::Array::New(isolate, 4);
        arr->Set(isolate->GetCurrentContext(), 0, v8::Number::New(isolate, color->r)).Check();
        arr->Set(isolate->GetCurrentContext(), 1, v8::Number::New(isolate, color->g)).Check();
        arr->Set(isolate->GetCurrentContext(), 2, v8::Number::New(isolate, color->b)).Check();
        arr->Set(isolate->GetCurrentContext(), 3, v8::Number::New(isolate, color->a)).Check();
        args.GetReturnValue().Set(arr);
    }

    void Color::ToString(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *color = Unwrap(args.Holder());
        if (!color)
            return;

        std::ostringstream ss;
        ss << "Color(" << color->r << ", " << color->g << ", " << color->b << ", " << color->a << ")";
        args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, ss.str().c_str()).ToLocalChecked());
    }

    void Color::FromHex(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        std::string hex = V8Helpers::GetString(isolate, args, 0, "#FFFFFF");

        // Remove # prefix if present
        if (!hex.empty() && hex[0] == '#') {
            hex = hex.substr(1);
        }

        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

        try {
            if (hex.length() >= 6) {
                r = static_cast<float>(std::stoi(hex.substr(0, 2), nullptr, 16)) / 255.0f;
                g = static_cast<float>(std::stoi(hex.substr(2, 2), nullptr, 16)) / 255.0f;
                b = static_cast<float>(std::stoi(hex.substr(4, 2), nullptr, 16)) / 255.0f;
            }
            if (hex.length() >= 8) {
                a = static_cast<float>(std::stoi(hex.substr(6, 2), nullptr, 16)) / 255.0f;
            }
        } catch (...) {
            // Invalid hex, use defaults
        }

        args.GetReturnValue().Set(NewInstance(isolate, r, g, b, a));
    }

    void Color::FromRGB(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        // Expect 0-255 values
        float r = V8Helpers::GetFloat(args, 0, 255.0f) / 255.0f;
        float g = V8Helpers::GetFloat(args, 1, 255.0f) / 255.0f;
        float b = V8Helpers::GetFloat(args, 2, 255.0f) / 255.0f;
        float a = V8Helpers::GetFloat(args, 3, 255.0f) / 255.0f;

        args.GetReturnValue().Set(NewInstance(isolate, r, g, b, a));
    }

    void Color::White(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 1.0f, 1.0f, 1.0f, 1.0f));
    }

    void Color::Black(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 0.0f, 0.0f, 0.0f, 1.0f));
    }

    void Color::Red(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 1.0f, 0.0f, 0.0f, 1.0f));
    }

    void Color::Green(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 0.0f, 1.0f, 0.0f, 1.0f));
    }

    void Color::Blue(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 0.0f, 0.0f, 1.0f, 1.0f));
    }

    void Color::Yellow(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 1.0f, 1.0f, 0.0f, 1.0f));
    }

    void Color::Cyan(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 0.0f, 1.0f, 1.0f, 1.0f));
    }

    void Color::Magenta(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 1.0f, 0.0f, 1.0f, 1.0f));
    }

    void Color::Transparent(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 0.0f, 0.0f, 0.0f, 0.0f));
    }

} // namespace Framework::Scripting::JS::Builtins
