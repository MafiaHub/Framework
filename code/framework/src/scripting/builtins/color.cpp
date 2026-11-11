/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "color.h"
#include "../scripting_catalog.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace Framework::Scripting::Builtins {

    std::string Color::toHex(std::optional<bool> includeAlpha) const {
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
        if (includeAlpha.value_or(false)) {
            ss << std::setw(2) << a;
        }
        return ss.str();
    }

    namespace {
        uint32_t colorByte(float v) {
            return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
        }

        // The constructor JavaScript has always had, more lenient than the declared one: a component that is
        // missing or is not a number is 0, and alpha 1.
        Color *ConstructLenient(const v8::FunctionCallbackInfo<v8::Value> &args) {
            v8::Local<v8::Context> ctx = args.GetIsolate()->GetCurrentContext();
            const auto num             = [&](int i, double fallback) {
                return (args.Length() > i && args[i]->IsNumber()) ? args[i]->NumberValue(ctx).FromMaybe(fallback) : fallback;
            };
            return new Color(static_cast<float>(num(0, 0.0)), static_cast<float>(num(1, 0.0)), static_cast<float>(num(2, 0.0)), static_cast<float>(num(3, 1.0)));
        }

        // toJSON method for JSON.stringify support - returns plain JS object
        void ToJson(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Color>::unwrap_object(info.GetIsolate(), info.This());
            if (!self)
                return;

            auto iso = info.GetIsolate();
            auto ctx = iso->GetCurrentContext();
            auto obj = v8::Object::New(iso);
            obj->Set(ctx, v8pp::to_v8(iso, "r"), v8::Number::New(iso, self->getR())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "g"), v8::Number::New(iso, self->getG())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "b"), v8::Number::New(iso, self->getB())).Check();
            obj->Set(ctx, v8pp::to_v8(iso, "a"), v8::Number::New(iso, self->getA())).Check();
            info.GetReturnValue().Set(obj);
        }
    } // namespace

    uint32_t Color::toRGBA() const {
        return (colorByte(_color.r) << 24) | (colorByte(_color.g) << 16) | (colorByte(_color.b) << 8) | colorByte(_color.a);
    }

    uint32_t Color::toARGB() const {
        return (colorByte(_color.a) << 24) | (colorByte(_color.r) << 16) | (colorByte(_color.g) << 8) | colorByte(_color.b);
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
        }
        catch (...) {
            // Invalid hex, use defaults
        }

        return Color(r, g, b, a);
    }

    Color Color::fromRGB(float r, float g, float b, std::optional<float> a) {
        return Color(r / 255.0f, g / 255.0f, b / 255.0f, a.value_or(255.0f) / 255.0f);
    }

    const Bind::Class<Color> &Color::Definition() {
        using Bind::Doc;
        using Bind::LuaMetaEvent;
        static const auto definition =
            Bind::Class<Color>("Color", "Mutable RGBA color, with components stored from 0 to 1.")
                .Constructor<float, float, float, std::optional<float>>(Doc("Creates a color from normalized RGBA components.")
                        .Param("r", "Initial red component from 0 to 1.")
                        .Param("g", "Initial green component from 0 to 1.")
                        .Param("b", "Initial blue component from 0 to 1.")
                        .Param("a", "Optional alpha component from 0 to 1; defaults to 1."))
                // Lua has the declared constructor.
                .V8Constructor(&ConstructLenient)
                // Instance methods
                .Method("lerp", &Color::lerp, Doc("Linearly interpolates every component toward a target in place.").Param("target", "Destination color.").Param("t", "Interpolation factor; 0 keeps this color and 1 reaches target.").Returns("This mutated color for chaining."))
                .Method("set", &Color::set,
                    Doc("Replaces this color's normalized components in place.")
                        .Param("r", "Replacement red component from 0 to 1.")
                        .Param("g", "Replacement green component from 0 to 1.")
                        .Param("b", "Replacement blue component from 0 to 1.")
                        .Param("a", "Optional replacement alpha; defaults to 1 when omitted.")
                        .Returns("This mutated color for chaining."))
                .Method("clone", &Color::clone, Doc("Creates an independent copy of this color.").Returns("New color with the same components."))
                .Method("toHex", &Color::toHex, Doc("Converts normalized components to a hexadecimal CSS-style string.").Param("includeAlpha", "Whether to append the alpha byte; defaults to false.").Returns("Lowercase #rrggbb or #rrggbbaa string."))
                .Method("toString", &Color::toString, Doc("Formats this color for logging and debugging.").Returns("Text in Color(r, g, b, a) form."))
                .Property("r", &Color::getR, &Color::setR, "Mutable normalized red component.")
                .Property("g", &Color::getG, &Color::setG, "Mutable normalized green component.")
                .Property("b", &Color::getB, &Color::setB, "Mutable normalized blue component.")
                .Property("a", &Color::getA, &Color::setA, "Mutable normalized alpha component.")
                // Static factory methods
                .Static("fromHex", &Color::fromHex,
                    Doc("Parses a hexadecimal color string.").Param("hex", "Hexadecimal color in #RRGGBB or #RRGGBBAA form; the leading # is optional. Three- and four-digit shorthands are not supported.").Returns("Parsed color, or opaque white when the input is invalid."))
                .Static("fromRGB", &Color::fromRGB,
                    Doc("Creates a normalized color from byte components.")
                        .Param("r", "Red byte from 0 to 255.")
                        .Param("g", "Green byte from 0 to 255.")
                        .Param("b", "Blue byte from 0 to 255.")
                        .Param("a", "Optional alpha byte from 0 to 255; defaults to 255.")
                        .Returns("New normalized color."))
                .Static("white", &Color::white, Doc("Creates opaque white.").Returns("New Color(1, 1, 1, 1)."))
                .Static("black", &Color::black, Doc("Creates opaque black.").Returns("New Color(0, 0, 0, 1)."))
                .Static("red", &Color::red, Doc("Creates opaque red.").Returns("New Color(1, 0, 0, 1)."))
                .Static("green", &Color::green, Doc("Creates opaque green.").Returns("New Color(0, 1, 0, 1)."))
                .Static("blue", &Color::blue, Doc("Creates opaque blue.").Returns("New Color(0, 0, 1, 1)."))
                .Static("yellow", &Color::yellow, Doc("Creates opaque yellow.").Returns("New Color(1, 1, 0, 1)."))
                .Static("cyan", &Color::cyan, Doc("Creates opaque cyan.").Returns("New Color(0, 1, 1, 1)."))
                .Static("magenta", &Color::magenta, Doc("Creates opaque magenta.").Returns("New Color(1, 0, 1, 1)."))
                .Static("transparent", &Color::transparent, Doc("Creates fully transparent black.").Returns("New Color(0, 0, 0, 0)."))
                // JavaScript only, until Lua has JSON.
                .V8Hook([](v8pp::class_<Color> &cls, v8::Isolate *) {
                    cls.prototype_function("toJSON", &ToJson, v8pp::metadata::docs("{ r: number; g: number; b: number; a: number }", {}, "Converts this color to a plain object for JSON.stringify.", "Object containing the current normalized components."));
                })
                // Lua operator. Two colors are equal when their components are.
                .LuaMeta(LuaMetaEvent::Eq, [](const Color &left, const Color &right) {
                    return left.vec() == right.vec();
                });
        return definition;
    }

    v8pp::class_<Color> &Color::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            return Definition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), ScriptingSide(isolate));
        });
    }

    void Color::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<Color> &cls = GetClass(isolate);
        auto ctx                 = isolate->GetCurrentContext();
        global->Set(ctx, v8pp::to_v8(isolate, "Color"), cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
    }

    v8::Local<v8::Object> Color::NewInstance(v8::Isolate *isolate, const glm::vec4 &value) {
        v8pp::class_<Color> &cls = GetClass(isolate);
        return cls.import_external(isolate, new Color(value));
    }

    void Color::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.Unregister(isolate);
    }

    void Color::RegisterLua(lua_State *L, Bind::Side side) {
        Definition().MaterialiseLua(L, side);
        lua_pushglobaltable(L);
        Definition().PublishLua(L, -1);
        lua_pop(L, 1);
    }

} // namespace Framework::Scripting::Builtins
