/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "text_label.h"
#include "../engine_helpers.h"
#include "../scripting_catalog.h"
#include "color.h"
#include "vector3.h"

#include <core_modules.h>
#include <external/imgui/widgets/world_text_markup.h>
#include <utils/string_utils.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <string_view>

namespace Framework::Scripting::Builtins {
    namespace {
        using Networking::Replication::Entities::TextLabelAlign;
        using Networking::Replication::Entities::TextLabelAnchor;
        using Networking::Replication::Entities::TextLabelEntity;
        using Networking::Replication::Entities::TextLabelSizeMode;
        using Networking::Replication::Entities::TextLabelStyle;

        // The world text widget's own limits (world_text_layout.h), which the server can't include.
        constexpr double kMaxOutlineWidth = 4.0;
        constexpr double kMaxShadowBlur   = 8.0;
        constexpr double kMaxBlockPixels  = 64.0;

        // Everything an options object can carry, validated in full before any of it is applied, so
        // a bad key leaves the label as it was.
        struct Options {
            struct Outline {
                float width    = 1.0f;
                uint32_t color = 0xFF000000;
            };
            struct Shadow {
                uint32_t color = 0xFF000000;
                float offsetX  = 1.0f;
                float offsetY  = 1.0f;
                float blur     = 0.0f;
            };
            struct Background {
                uint32_t color = 0x99000000;
                float padding  = 4.0f;
                float radius   = 4.0f;
            };

            std::optional<std::string> text;
            std::optional<bool> markup;
            std::optional<glm::vec3> position;
            std::optional<uint64_t> attachTo; // 0 detaches
            std::optional<glm::vec3> offset;
            std::optional<float> fontSize;
            std::optional<TextLabelSizeMode> sizeMode;
            std::optional<float> minSize;
            std::optional<float> maxSize;
            std::optional<uint32_t> color;
            std::optional<float> opacity;
            std::optional<TextLabelStyle> style;
            std::optional<std::optional<Outline>> outline; // set but empty = turned off
            std::optional<std::optional<Shadow>> shadow;
            std::optional<std::optional<Background>> background;
            std::optional<TextLabelAlign> align;
            std::optional<TextLabelAnchor> anchor;
            std::optional<float> lineSpacing;
            std::optional<float> maxWidth;
            std::optional<float> drawDistance;
            std::optional<float> fadeDistance;
            std::optional<bool> occlusion;
            std::optional<uint32_t> virtualWorld;
        };

        constexpr std::string_view kOptionKeys[] {"text", "markup", "position", "attachTo", "offset", "fontSize", "sizeMode", "minSize", "maxSize", "color", "opacity", "style", "outline", "shadow", "background", "align", "anchor", "lineSpacing", "maxWidth", "drawDistance",
            "fadeDistance", "occlusion", "virtualWorld"};

        constexpr std::pair<std::string_view, TextLabelStyle> kStyles[] {{"none", TextLabelStyle::None}, {"shadow", TextLabelStyle::Shadow}, {"outline", TextLabelStyle::Outline}, {"box", TextLabelStyle::Box}};
        constexpr std::pair<std::string_view, TextLabelSizeMode> kSizeModes[] {{"screen", TextLabelSizeMode::Screen}, {"world", TextLabelSizeMode::World}};
        constexpr std::pair<std::string_view, TextLabelAlign> kAligns[] {{"left", TextLabelAlign::Left}, {"center", TextLabelAlign::Center}, {"right", TextLabelAlign::Right}};
        constexpr std::pair<std::string_view, TextLabelAnchor> kAnchors[] {{"center", TextLabelAnchor::Center}, {"bottom", TextLabelAnchor::Bottom}, {"top", TextLabelAnchor::Top}};

        v8::Local<v8::String> Str(v8::Isolate *isolate, std::string_view text) {
            return v8::String::NewFromUtf8(isolate, text.data(), v8::NewStringType::kNormal, static_cast<int>(text.size())).ToLocalChecked();
        }

        bool Fail(v8::Isolate *isolate, const std::string &message) {
            ThrowTypeError(isolate, ("TextLabel: " + message).c_str());
            return false;
        }

        bool Get(v8::Isolate *isolate, v8::Local<v8::Object> object, std::string_view key, v8::Local<v8::Value> &value) {
            return object->Get(isolate->GetCurrentContext(), Str(isolate, key)).ToLocal(&value);
        }

        std::optional<float> Number(v8::Isolate *isolate, v8::Local<v8::Value> value, const std::string &name, double min, double max) {
            const double number = value->IsNumber() ? value->NumberValue(isolate->GetCurrentContext()).FromMaybe(NAN) : NAN;
            if (!std::isfinite(number) || number < min || number > max) {
                std::ostringstream ss;
                ss << name << " must be a number from " << min << " to " << max;
                Fail(isolate, ss.str());
                return std::nullopt;
            }
            return static_cast<float>(number);
        }

        std::optional<bool> Bool(v8::Isolate *isolate, v8::Local<v8::Value> value, const std::string &name) {
            if (!value->IsBoolean()) {
                Fail(isolate, name + " must be a boolean");
                return std::nullopt;
            }
            return value->BooleanValue(isolate);
        }

        // 0xAARRGGBB (or its signed int32 form), "#RRGGBB" / "#RRGGBBAA", or a Color.
        std::optional<uint32_t> ColorValue(v8::Isolate *isolate, v8::Local<v8::Value> value, const std::string &name) {
            if (value->IsNumber()) {
                const double n = value->NumberValue(isolate->GetCurrentContext()).FromMaybe(NAN);
                if (std::isfinite(n) && std::floor(n) == n && n >= -2147483648.0 && n <= 4294967295.0) {
                    return n < 0 ? static_cast<uint32_t>(static_cast<int32_t>(n)) : static_cast<uint32_t>(n);
                }
            }
            else if (value->IsString()) {
                if (const auto parsed = External::ImGUI::Widgets::Detail::ParseHexColor(v8pp::from_v8<std::string>(isolate, value))) {
                    return parsed;
                }
            }
            else if (auto *color = v8pp::class_<Color>::unwrap_object(isolate, value)) {
                return color->toARGB();
            }
            Fail(isolate, name + " must be 0xAARRGGBB, \"#RRGGBB[AA]\" or a Color");
            return std::nullopt;
        }

        // A Vector3, {x, y, z} or [x, y, z].
        std::optional<glm::vec3> Vec3(v8::Isolate *isolate, v8::Local<v8::Value> value, const std::string &name) {
            if (auto *vector = v8pp::class_<Vector3>::unwrap_object(isolate, value)) {
                return glm::vec3(vector->getX(), vector->getY(), vector->getZ());
            }
            v8::Local<v8::Value> parts[3];
            if (value->IsArray() && value.As<v8::Array>()->Length() == 3) {
                for (uint32_t i = 0; i < 3; ++i) {
                    if (!value.As<v8::Array>()->Get(isolate->GetCurrentContext(), i).ToLocal(&parts[i])) {
                        return std::nullopt;
                    }
                }
            }
            else if (value->IsObject()) {
                constexpr std::string_view keys[] {"x", "y", "z"};
                for (int i = 0; i < 3; ++i) {
                    if (!Get(isolate, value.As<v8::Object>(), keys[i], parts[i])) {
                        return std::nullopt;
                    }
                }
            }
            else {
                Fail(isolate, name + " must be a Vector3, {x, y, z} or [x, y, z]");
                return std::nullopt;
            }
            glm::vec3 out(0.0f);
            for (int i = 0; i < 3; ++i) {
                const auto component = Number(isolate, parts[i], name, -1.0e9, 1.0e9);
                if (!component) {
                    return std::nullopt;
                }
                out[i] = *component;
            }
            return out;
        }

        template <typename T, size_t N>
        std::optional<T> Enum(v8::Isolate *isolate, v8::Local<v8::Value> value, const std::string &name, const std::pair<std::string_view, T> (&entries)[N]) {
            if (value->IsString()) {
                const auto text = v8pp::from_v8<std::string>(isolate, value);
                for (const auto &[label, entry] : entries) {
                    if (text == label) {
                        return entry;
                    }
                }
            }
            std::string accepted;
            for (const auto &[label, entry] : entries) {
                accepted += (accepted.empty() ? "'" : ", '") + std::string(label) + "'";
            }
            Fail(isolate, name + " must be one of " + accepted);
            return std::nullopt;
        }

        // An entity handle (anything with a numeric id) or a network id; null or 0 detaches.
        std::optional<uint64_t> Target(v8::Isolate *isolate, v8::Local<v8::Value> value, const std::string &name) {
            if (value->IsNull()) {
                return 0;
            }
            v8::Local<v8::Value> id = value;
            if (value->IsObject() && !Get(isolate, value.As<v8::Object>(), "id", id)) {
                return std::nullopt;
            }
            const double n = id->IsNumber() ? id->NumberValue(isolate->GetCurrentContext()).FromMaybe(NAN) : NAN;
            if (!std::isfinite(n) || n < 0.0 || n > 9007199254740991.0 || std::floor(n) != n) {
                Fail(isolate, name + " must be an entity or its network id");
                return std::nullopt;
            }
            return static_cast<uint64_t>(n);
        }

        // false/null turns a block off, true takes its defaults, an object sets it whole.
        enum class BlockState {
            Invalid,
            Off,
            Defaults,
            Object,
        };
        BlockState Block(v8::Isolate *isolate, v8::Local<v8::Value> value, const std::string &name) {
            if (value->IsNull() || (value->IsBoolean() && !value->BooleanValue(isolate))) {
                return BlockState::Off;
            }
            if (value->IsBoolean()) {
                return BlockState::Defaults;
            }
            if (value->IsObject() && !value->IsArray()) {
                return BlockState::Object;
            }
            Fail(isolate, name + " must be an object, true or false");
            return BlockState::Invalid;
        }

        // Reads `key` off a block when present; absent keeps `out` at its default.
        template <typename T, typename Reader>
        bool BlockField(v8::Isolate *isolate, v8::Local<v8::Object> block, std::string_view key, T &out, const Reader &read) {
            v8::Local<v8::Value> value;
            if (!Get(isolate, block, key, value)) {
                return false;
            }
            if (value->IsUndefined()) {
                return true;
            }
            const auto parsed = read(value);
            if (!parsed) {
                return false;
            }
            out = *parsed;
            return true;
        }

        bool ParseOptions(v8::Isolate *isolate, v8::Local<v8::Object> object, float unitsPerMetre, Options &options) {
            auto context = isolate->GetCurrentContext();
            v8::Local<v8::Array> names;
            if (!object->GetOwnPropertyNames(context).ToLocal(&names)) {
                return false;
            }
            for (uint32_t i = 0; i < names->Length(); ++i) {
                v8::Local<v8::Value> name;
                if (!names->Get(context, i).ToLocal(&name)) {
                    return false;
                }
                const auto key = v8pp::from_v8<std::string>(isolate, name);
                if (std::find(std::begin(kOptionKeys), std::end(kOptionKeys), key) == std::end(kOptionKeys)) {
                    return Fail(isolate, "unknown option '" + key + "'");
                }
            }

            const double maxDistance = TextLabel::kMaxDrawMetres * unitsPerMetre;
            for (const std::string_view key : kOptionKeys) {
                v8::Local<v8::Value> value;
                if (!Get(isolate, object, key, value)) {
                    return false;
                }
                if (value->IsUndefined()) {
                    continue;
                }
                const std::string name(key);
                const auto number = [&](double min, double max, std::optional<float> &out) {
                    out = Number(isolate, value, name, min, max);
                    return out.has_value();
                };

                bool ok = true;
                if (key == "text") {
                    ok = value->IsString() || Fail(isolate, "text must be a string");
                    if (ok) {
                        options.text = v8pp::from_v8<std::string>(isolate, value);
                    }
                }
                else if (key == "markup") {
                    ok = (options.markup = Bool(isolate, value, name)).has_value();
                }
                else if (key == "position") {
                    ok = (options.position = Vec3(isolate, value, name)).has_value();
                }
                else if (key == "attachTo") {
                    ok = (options.attachTo = Target(isolate, value, name)).has_value();
                }
                else if (key == "offset") {
                    ok = (options.offset = Vec3(isolate, value, name)).has_value();
                }
                else if (key == "fontSize") {
                    ok = number(TextLabel::kMinFontSize, TextLabel::kMaxFontSize, options.fontSize);
                }
                else if (key == "sizeMode") {
                    ok = (options.sizeMode = Enum(isolate, value, name, kSizeModes)).has_value();
                }
                else if (key == "minSize") {
                    ok = number(0.0, TextLabel::kMaxFontSize, options.minSize);
                }
                else if (key == "maxSize") {
                    ok = number(0.0, TextLabel::kMaxFontSize, options.maxSize);
                }
                else if (key == "color") {
                    ok = (options.color = ColorValue(isolate, value, name)).has_value();
                }
                else if (key == "opacity") {
                    ok = number(0.0, 1.0, options.opacity);
                }
                else if (key == "style") {
                    if (value->IsNumber()) {
                        const double raw = value->NumberValue(context).FromMaybe(NAN);
                        ok               = std::isfinite(raw) && raw >= 0.0 && raw <= static_cast<double>(TextLabelStyle::Box) && std::floor(raw) == raw;
                        if (ok) {
                            options.style = static_cast<TextLabelStyle>(static_cast<uint8_t>(raw));
                        }
                        else {
                            Fail(isolate, "style must be 'none', 'shadow', 'outline', 'box' or 0-3");
                        }
                    }
                    else {
                        ok = (options.style = Enum(isolate, value, name, kStyles)).has_value();
                    }
                }
                else if (key == "outline") {
                    const auto state = Block(isolate, value, name);
                    ok               = state != BlockState::Invalid;
                    Options::Outline outline;
                    if (state == BlockState::Object) {
                        const auto block = value.As<v8::Object>();
                        ok               = BlockField(isolate, block, "color", outline.color, [&](auto v) {
                            return ColorValue(isolate, v, "outline.color");
                        }) && BlockField(isolate, block, "width", outline.width, [&](auto v) {
                            return Number(isolate, v, "outline.width", 0.0, kMaxOutlineWidth);
                        });
                    }
                    if (ok) {
                        options.outline = state == BlockState::Off ? std::optional<Options::Outline> {} : std::optional<Options::Outline> {outline};
                    }
                }
                else if (key == "shadow") {
                    const auto state = Block(isolate, value, name);
                    ok               = state != BlockState::Invalid;
                    Options::Shadow shadow;
                    if (state == BlockState::Object) {
                        const auto block = value.As<v8::Object>();
                        ok               = BlockField(isolate, block, "color", shadow.color, [&](auto v) {
                            return ColorValue(isolate, v, "shadow.color");
                        }) && BlockField(isolate, block, "offsetX", shadow.offsetX, [&](auto v) {
                            return Number(isolate, v, "shadow.offsetX", -32.0, 32.0);
                        }) && BlockField(isolate, block, "offsetY", shadow.offsetY, [&](auto v) {
                            return Number(isolate, v, "shadow.offsetY", -32.0, 32.0);
                        }) && BlockField(isolate, block, "blur", shadow.blur, [&](auto v) {
                            return Number(isolate, v, "shadow.blur", 0.0, kMaxShadowBlur);
                        });
                    }
                    if (ok) {
                        options.shadow = state == BlockState::Off ? std::optional<Options::Shadow> {} : std::optional<Options::Shadow> {shadow};
                    }
                }
                else if (key == "background") {
                    const auto state = Block(isolate, value, name);
                    ok               = state != BlockState::Invalid;
                    Options::Background background;
                    if (state == BlockState::Object) {
                        const auto block = value.As<v8::Object>();
                        ok               = BlockField(isolate, block, "color", background.color, [&](auto v) {
                            return ColorValue(isolate, v, "background.color");
                        }) && BlockField(isolate, block, "padding", background.padding, [&](auto v) {
                            return Number(isolate, v, "background.padding", 0.0, kMaxBlockPixels);
                        }) && BlockField(isolate, block, "radius", background.radius, [&](auto v) {
                            return Number(isolate, v, "background.radius", 0.0, kMaxBlockPixels);
                        });
                    }
                    if (ok) {
                        options.background = state == BlockState::Off ? std::optional<Options::Background> {} : std::optional<Options::Background> {background};
                    }
                }
                else if (key == "align") {
                    ok = (options.align = Enum(isolate, value, name, kAligns)).has_value();
                }
                else if (key == "anchor") {
                    ok = (options.anchor = Enum(isolate, value, name, kAnchors)).has_value();
                }
                else if (key == "lineSpacing") {
                    ok = number(0.5, 4.0, options.lineSpacing);
                }
                else if (key == "maxWidth") {
                    ok = number(0.0, 4096.0, options.maxWidth);
                }
                else if (key == "drawDistance") {
                    ok = number(1.0, maxDistance, options.drawDistance);
                }
                else if (key == "fadeDistance") {
                    ok = number(0.0, maxDistance, options.fadeDistance);
                }
                else if (key == "occlusion") {
                    ok = (options.occlusion = Bool(isolate, value, name)).has_value();
                }
                else if (key == "virtualWorld") {
                    const double world = value->IsNumber() ? value->NumberValue(context).FromMaybe(NAN) : NAN;
                    ok                 = std::isfinite(world) && world >= 0.0 && world <= 4294967295.0 && std::floor(world) == world;
                    if (ok) {
                        options.virtualWorld = static_cast<uint32_t>(world);
                    }
                    else {
                        Fail(isolate, "virtualWorld must be a whole number from 0 to 4294967295");
                    }
                }
                if (!ok) {
                    return false;
                }
            }
            return true;
        }

        void ApplyOptions(TextLabel &handle, TextLabelEntity &label, const Options &options) {
            if (options.text) {
                handle.SetText(*options.text);
            }
            if (options.markup) {
                label.markup = *options.markup;
            }
            if (options.position) {
                label.position = *options.position;
            }
            if (options.attachTo) {
                if (*options.attachTo == 0) {
                    handle.Detach();
                }
                else {
                    handle.AttachTo(*options.attachTo, options.offset.value_or(label.attachOffset));
                }
            }
            else if (options.offset) {
                label.attachOffset = *options.offset;
            }
            if (options.fontSize) {
                handle.SetFontSize(*options.fontSize);
            }
            if (options.sizeMode) {
                label.sizeMode = *options.sizeMode;
            }
            if (options.minSize) {
                label.minSize = *options.minSize;
            }
            if (options.maxSize) {
                label.maxSize = *options.maxSize;
            }
            if (options.color) {
                label.color = *options.color;
            }
            if (options.opacity) {
                label.opacity = *options.opacity;
            }

            // A preset first, so explicit blocks in the same call refine it.
            if (options.style) {
                Networking::Replication::Entities::ApplyTextLabelStylePreset(label, *options.style);
            }
            if (options.outline) {
                const Options::Outline outline = options.outline->value_or(Options::Outline {0.0f});
                label.outlineWidth             = outline.width;
                label.outlineColor             = outline.color;
            }
            if (options.shadow) {
                const Options::Shadow shadow = options.shadow->value_or(Options::Shadow {0});
                label.shadowColor            = shadow.color;
                label.shadowOffsetX          = shadow.offsetX;
                label.shadowOffsetY          = shadow.offsetY;
                label.shadowBlur             = shadow.blur;
            }
            if (options.background) {
                const Options::Background background = options.background->value_or(Options::Background {0});
                label.backgroundColor                = background.color;
                label.backgroundPadding              = background.padding;
                label.backgroundRadius               = background.radius;
            }
            if (options.style || options.outline || options.shadow || options.background) {
                label.style = Networking::Replication::Entities::NearestTextLabelStylePreset(label);
            }

            if (options.align) {
                label.align = *options.align;
            }
            if (options.anchor) {
                label.anchor = *options.anchor;
            }
            if (options.lineSpacing) {
                label.lineSpacing = *options.lineSpacing;
            }
            if (options.maxWidth) {
                label.maxWidth = *options.maxWidth;
            }
            if (options.drawDistance) {
                handle.SetDrawDistance(*options.drawDistance);
            }
            if (options.fadeDistance) {
                handle.SetFadeDistance(*options.fadeDistance);
            }
            if (options.occlusion) {
                label.occlusion = *options.occlusion;
            }
            if (options.virtualWorld) {
                handle.SetVirtualWorld(*options.virtualWorld);
            }
        }

        template <typename T, size_t N>
        std::string EnumName(T value, const std::pair<std::string_view, T> (&entries)[N]) {
            for (const auto &[label, entry] : entries) {
                if (entry == value) {
                    return std::string(label);
                }
            }
            return std::string(entries[0].first);
        }

        // The label as an options object: what update() takes, so one label's look can seed another's.
        v8::Local<v8::Object> OptionsObject(v8::Isolate *isolate, const TextLabelEntity &label, uint32_t virtualWorld) {
            auto context   = isolate->GetCurrentContext();
            const auto put = [&](v8::Local<v8::Object> object, std::string_view key, v8::Local<v8::Value> value) {
                object->Set(context, Str(isolate, key), value).Check();
            };
            const auto set = [&](v8::Local<v8::Object> object, std::string_view key, auto value) {
                put(object, key, v8pp::to_v8(isolate, value));
            };
            const auto vec = [&](const glm::vec3 &v) {
                auto object = v8::Object::New(isolate);
                set(object, "x", v.x);
                set(object, "y", v.y);
                set(object, "z", v.z);
                return object;
            };

            auto out = v8::Object::New(isolate);
            set(out, "text", label.text);
            set(out, "markup", label.markup);
            put(out, "position", vec(label.position));
            if (label.attachNetworkId != 0) {
                set(out, "attachTo", static_cast<double>(label.attachNetworkId));
            }
            else {
                put(out, "attachTo", v8::Null(isolate));
            }
            put(out, "offset", vec(label.attachOffset));
            set(out, "fontSize", label.fontSize);
            set(out, "sizeMode", EnumName(label.sizeMode, kSizeModes));
            set(out, "minSize", label.minSize);
            set(out, "maxSize", label.maxSize);
            set(out, "color", label.color);
            set(out, "opacity", label.opacity);
            set(out, "style", EnumName(label.style, kStyles));

            if (label.outlineWidth > 0.0f) {
                auto outline = v8::Object::New(isolate);
                set(outline, "width", label.outlineWidth);
                set(outline, "color", label.outlineColor);
                put(out, "outline", outline);
            }
            else {
                set(out, "outline", false);
            }
            if ((label.shadowColor >> 24) != 0) {
                auto shadow = v8::Object::New(isolate);
                set(shadow, "color", label.shadowColor);
                set(shadow, "offsetX", label.shadowOffsetX);
                set(shadow, "offsetY", label.shadowOffsetY);
                set(shadow, "blur", label.shadowBlur);
                put(out, "shadow", shadow);
            }
            else {
                set(out, "shadow", false);
            }
            if ((label.backgroundColor >> 24) != 0) {
                auto background = v8::Object::New(isolate);
                set(background, "color", label.backgroundColor);
                set(background, "padding", label.backgroundPadding);
                set(background, "radius", label.backgroundRadius);
                put(out, "background", background);
            }
            else {
                set(out, "background", false);
            }

            set(out, "align", EnumName(label.align, kAligns));
            set(out, "anchor", EnumName(label.anchor, kAnchors));
            set(out, "lineSpacing", label.lineSpacing);
            set(out, "maxWidth", label.maxWidth);
            set(out, "drawDistance", label.drawDistance);
            set(out, "fadeDistance", label.fadeDistance);
            set(out, "occlusion", label.occlusion);
            set(out, "virtualWorld", virtualWorld);
            return out;
        }
    } // namespace

    void TextLabel::SetUnitsPerMetre(float unitsPerMetre) {
        if (std::isfinite(unitsPerMetre) && unitsPerMetre > 0.0f) {
            _unitsPerMetre = unitsPerMetre;
        }
    }

    float TextLabel::GetUnitsPerMetre() {
        return _unitsPerMetre;
    }

    TextLabel::LabelEntity *TextLabel::ResolveLabel() const {
        return dynamic_cast<LabelEntity *>(Resolve());
    }

    std::string TextLabel::GetText() const {
        auto *label = ResolveLabel();
        return label ? label->text : std::string();
    }

    void TextLabel::SetText(const std::string &text) {
        if (auto *label = ResolveLabel()) {
            label->text = text;
            Utils::StringUtils::TruncateUtf8(label->text, kMaxTextBytes);
        }
    }

    uint32_t TextLabel::GetColor() const {
        auto *label = ResolveLabel();
        return label ? label->color : 0xFFFFFFFF;
    }

    void TextLabel::SetColor(int r, int g, int b, int a) {
        const auto clamp = [](int c) {
            return static_cast<uint32_t>(std::clamp(c, 0, 255));
        };
        SetColorPacked((clamp(a) << 24) | (clamp(r) << 16) | (clamp(g) << 8) | clamp(b));
    }

    void TextLabel::SetColorPacked(uint32_t argb) {
        if (auto *label = ResolveLabel()) {
            label->color = argb;
        }
    }

    float TextLabel::GetFontSize() const {
        auto *label = ResolveLabel();
        return label ? label->fontSize : 0.0f;
    }

    void TextLabel::SetFontSize(float size) {
        if (auto *label = ResolveLabel()) {
            label->fontSize = std::clamp(size, kMinFontSize, kMaxFontSize);
        }
    }

    float TextLabel::GetDrawDistance() const {
        auto *label = ResolveLabel();
        return label ? label->drawDistance : 0.0f;
    }

    // The draw distance is also the label's streaming radius, so a game that holds the type to its
    // entities' own range sends a label only to players who could see it.
    void TextLabel::SetDrawDistance(float distance) {
        if (auto *label = ResolveLabel()) {
            label->drawDistance    = std::clamp(distance, 1.0f, kMaxDrawMetres * _unitsPerMetre);
            label->streaming.range = label->drawDistance;
        }
    }

    float TextLabel::GetFadeDistance() const {
        auto *label = ResolveLabel();
        return label ? label->fadeDistance : 0.0f;
    }

    void TextLabel::SetFadeDistance(float distance) {
        if (auto *label = ResolveLabel()) {
            label->fadeDistance = std::max(distance, 0.0f);
        }
    }

    uint32_t TextLabel::GetStyle() const {
        auto *label = ResolveLabel();
        return label ? static_cast<uint32_t>(label->style) : 0;
    }

    void TextLabel::SetStyle(uint32_t style) {
        if (auto *label = ResolveLabel()) {
            if (style <= static_cast<uint32_t>(LabelStyle::Box)) {
                Networking::Replication::Entities::ApplyTextLabelStylePreset(*label, static_cast<LabelStyle>(style));
            }
        }
    }

    void TextLabel::AttachTo(uint64_t networkId, const glm::vec3 &offset) {
        if (auto *label = ResolveLabel()) {
            label->attachNetworkId         = networkId;
            label->attachOffset            = offset;
            label->streaming.alwaysVisible = networkId != 0;
        }
    }

    void TextLabel::Detach() {
        AttachTo(0, glm::vec3(0.0f));
    }

    std::string TextLabel::ToString() const {
        std::ostringstream ss;
        ss << "TextLabel{ id: " << _id << ", text: \"" << GetText() << "\" }";
        return ss.str();
    }

    void TextLabel::Destroy() {
        auto *label = ResolveLabel();
        if (!label) {
            return;
        }
        if (auto *replication = CoreModules::GetReplication()) {
            replication->DestroyEntity(label);
        }
    }

    v8pp::class_<TextLabel> &TextLabel::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            Entity::GetClass(isolate);

            auto cls = std::make_unique<v8pp::class_<TextLabel>>(isolate, GetScriptingCatalog(isolate), "TextLabel", "Server-created replicated world-space text label.");
            cls->auto_wrap_objects(true);
            cls->inherit<Entity>()
                .ctor<uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Existing text-label network entity identifier.")}, "Creates a wrapper for an existing text label; use TextLabel.create to spawn one."))
                .function("toString", &TextLabel::ToString, v8pp::metadata::docs("string", {}, "Formats this label for logging and debugging.", "Text containing the label ID and current text."))
                .function("destroy", &TextLabel::Destroy, v8pp::metadata::docs("void", {}, "Destroys this replicated label; stale wrappers no longer resolve afterward."))
                .function("setText", &TextLabel::SetText, v8pp::metadata::docs("void", {v8pp::metadata::param("text", "string", false, "Replacement text replicated to clients; at most 1024 bytes.")}, "Changes the label's displayed text."))
                .function("setFontSize", &TextLabel::SetFontSize, v8pp::metadata::docs("void", {v8pp::metadata::param("size", "number", false, "Font size in pixels, clamped from 1 to 512.")}, "Changes the rendered font size."))
                .function("setDrawDistance", &TextLabel::SetDrawDistance,
                    v8pp::metadata::docs("void", {v8pp::metadata::param("distance", "number", false, "Maximum rendering distance in world units, clamped from 1 to 1000 metres' worth.")}, "Changes how far away clients can render the label."))
                .function("setFadeDistance", &TextLabel::SetFadeDistance,
                    v8pp::metadata::docs("void", {v8pp::metadata::param("distance", "number", false, "Distance in world units where fading starts; 0 starts it at 75% of the draw distance.")}, "Changes where the label starts fading out."))
                .function("setStyle", &TextLabel::SetStyle, v8pp::metadata::docs("void", {v8pp::metadata::param("style", "number", false, "0 none, 1 shadow, 2 outline, 3 box; unsupported values are ignored.")}, "Resets the outline, shadow and background to a preset."))
                .function("detach", &TextLabel::Detach, v8pp::metadata::docs("void", {}, "Stops following an entity; the label returns to its own position."))
                .property("text", &TextLabel::GetText, v8pp::metadata::property_docs("string", "Current replicated label text."))
                .property("color", &TextLabel::GetColor, v8pp::metadata::property_docs("number", "Current packed ARGB color."))
                .property("fontSize", &TextLabel::GetFontSize, v8pp::metadata::property_docs("number", "Current font size."))
                .property("drawDistance", &TextLabel::GetDrawDistance, v8pp::metadata::property_docs("number", "Current maximum rendering distance in world units."))
                .property("fadeDistance", &TextLabel::GetFadeDistance, v8pp::metadata::property_docs("number", "Distance in world units where fading starts."))
                .property("style", &TextLabel::GetStyle, v8pp::metadata::property_docs("number", "The preset nearest the current look: 0 none, 1 shadow, 2 outline, 3 box."));

            // setColor accepts either a Color or byte components (r, g, b, a from 0-255), so scripts
            // don't have to hand-pack this API's ARGB layout. Registered as a raw prototype function
            // because v8pp's typed binding can't express the Color|number overload.
            cls->prototype_function(
                "setColor",
                [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                    v8::Isolate *iso = info.GetIsolate();
                    auto *self       = v8pp::class_<TextLabel>::unwrap_object(iso, info.This());
                    if (!self) {
                        return;
                    }
                    if (info.Length() >= 1) {
                        if (auto *col = v8pp::class_<Color>::unwrap_object(iso, info[0])) {
                            self->SetColorPacked(col->toARGB());
                            return;
                        }
                    }
                    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                    const auto num             = [&](int i, int fallback) {
                        return (info.Length() > i && info[i]->IsNumber()) ? info[i]->Int32Value(ctx).FromMaybe(fallback) : fallback;
                    };
                    self->SetColor(num(0, 0), num(1, 0), num(2, 0), num(3, 255));
                },
                v8pp::metadata::docs("void",
                    {v8pp::metadata::param("colorOrR", "Color | number", false, "A Color, or the red byte (0-255) when passing components."), v8pp::metadata::param("g", "number", true, "Green byte (0-255) when passing components."),
                        v8pp::metadata::param("b", "number", true, "Blue byte (0-255) when passing components."), v8pp::metadata::param("a", "number", true, "Alpha byte (0-255) when passing components; defaults to 255.")},
                    "Changes the label's packed ARGB color from a Color or byte components."));

            cls->prototype_function("update", &TextLabel::JS_Update,
                v8pp::metadata::docs("void", {v8pp::metadata::param("options", "TextLabelOptions", false, "Only the keys given change; outline, shadow and background are replaced whole.")}, "Changes any part of the label at once. A bad key throws and changes nothing."));
            cls->prototype_function("attachTo", &TextLabel::JS_AttachTo,
                v8pp::metadata::docs("void",
                    {v8pp::metadata::param("target", "Entity | number", false, "The entity to follow, or its network id."),
                        v8pp::metadata::param("offset", "Vector3 | { x: number; y: number; z: number }", true, "Where to draw relative to it; the game decides the reference point (a player's head, say).")},
                    "Follows an entity instead of staying at the label's own position. An attached label streams to every player in its virtual world."));
            cls->prototype_function("getOptions", &TextLabel::JS_GetOptions, v8pp::metadata::docs("TextLabelOptions", {}, "The label's current look and placement, in the shape update() takes."));
            return cls;
        });
    }

    void TextLabel::RegisterOptionsType(v8::Isolate *isolate) {
        auto &type          = GetScriptingCatalog(isolate).data_type("TextLabelOptions", "A text label's look and placement. Every key is optional; colours are 0xAARRGGBB numbers, \"#RRGGBB[AA]\" strings or Colors.");
        const auto optional = [](const char *name) {
            return v8pp::metadata::type {name, {}, true};
        };
        type.add_property("text", optional("string"), "UTF-8 text, at most 1024 bytes; '\\n' breaks lines.");
        type.add_property("markup", optional("boolean"), "Parse inline tags: {#RRGGBB} / {#RRGGBBAA} colour, {#} back to `color`, {b}{/b}, {i}{/i}, {{ for a brace. Default false.");
        type.add_property("position", optional("Vector3 | { x: number; y: number; z: number }"), "Where the label stands while it is not attached.");
        type.add_property("attachTo", optional("Entity | number | null"), "An entity to follow, or its network id; null or 0 detaches.");
        type.add_property("offset", optional("Vector3 | { x: number; y: number; z: number }"), "Relative to the attached entity; the game decides the reference point (a player's head, say).");
        type.add_property("fontSize", optional("number"), "Pixels, 1-512. Default 16.");
        type.add_property("sizeMode", optional("\"screen\" | \"world\""), "\"screen\" (default): fontSize pixels at any distance. \"world\": fontSize pixels at one metre on a 1080p view, shrinking with distance.");
        type.add_property("minSize", optional("number"), "World mode: never smaller than this many pixels.");
        type.add_property("maxSize", optional("number"), "World mode: never larger than this many pixels; 0 = no cap.");
        type.add_property("color", optional("number | string | Color"), "Text colour. Default white.");
        type.add_property("opacity", optional("number"), "0-1, multiplies every colour. Default 1.");
        type.add_property("style", optional("\"none\" | \"shadow\" | \"outline\" | \"box\" | number"), "A preset for outline, shadow and background; blocks in the same call refine it.");
        type.add_property("outline", optional("boolean | null | { color?: number | string | Color; width?: number }"), "true is 1 px black; width is at most 4.");
        type.add_property("shadow", optional("boolean | null | { color?: number | string | Color; offsetX?: number; offsetY?: number; blur?: number }"), "A drop shadow; blur (at most 8) is approximated. Default a 1 px shadow.");
        type.add_property("background", optional("boolean | null | { color?: number | string | Color; padding?: number; radius?: number }"), "A plate behind the text; true is translucent black, 4 px padding and radius.");
        type.add_property("align", optional("\"left\" | \"center\" | \"right\""), "How lines align with each other. Default \"center\".");
        type.add_property("anchor", optional("\"center\" | \"bottom\" | \"top\""), "Which part of the text block sits on the point. \"bottom\" grows upward, as a tag over a head.");
        type.add_property("lineSpacing", optional("number"), "Line height as a multiple of the size. Default 1.");
        type.add_property("maxWidth", optional("number"), "Wraps at spaces past this width, in fontSize pixels. 0 (default) never wraps.");
        type.add_property("drawDistance", optional("number"), "World units; the label neither draws nor streams past it. Default 30 metres' worth.");
        type.add_property("fadeDistance", optional("number"), "World units where fading starts; 0 starts at 75% of drawDistance.");
        type.add_property("occlusion", optional("boolean"), "Hidden behind world geometry. Default true.");
        type.add_property("virtualWorld", optional("number"), "The virtual world it lives in.");
    }

    void TextLabel::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<TextLabel> &cls = GetClass(isolate);
        RegisterOptionsType(isolate);
        auto ctx = isolate->GetCurrentContext();

        auto ctorFn = cls.js_function_template()->GetFunction(ctx).ToLocalChecked();

        cls.static_function(ctorFn, "create", &TextLabel::JS_Create,
            v8pp::metadata::docs("TextLabel",
                {
                    v8pp::metadata::param("x", "number", false, "World-space X coordinate."),
                    v8pp::metadata::param("y", "number", false, "World-space Y coordinate."),
                    v8pp::metadata::param("z", "number", false, "World-space Z coordinate."),
                    v8pp::metadata::param("text", "string", false, "Initial displayed text."),
                    v8pp::metadata::param("fontSizeOrOptions", "number | TextLabelOptions", true, "Look and placement, or (the older form) the font size from 1 to 512."),
                    v8pp::metadata::param("drawDistance", "number", true, "Older form only: the draw distance in world units."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Older form only: the virtual-world identifier."),
                },
                "Creates and replicates a world-space text label, up to the server-wide limit of 512. A bad option throws and creates nothing.", "Handle for the newly created label."));
        cls.publish(global, ctorFn);
    }

    void TextLabel::JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        if (info.Length() < 4 || !info[0]->IsNumber() || !info[1]->IsNumber() || !info[2]->IsNumber() || !info[3]->IsString()) {
            ThrowTypeError(isolate, "TextLabel.create: expected (x, y, z, text, fontSize? | options?, drawDistance?, virtualWorld?)");
            return;
        }

        // Options are checked before anything exists, so a bad one leaves nothing half-made.
        Options options;
        const bool withOptions = info.Length() >= 5 && info[4]->IsObject() && !info[4]->IsArray();
        if (withOptions && !ParseOptions(isolate, info[4].As<v8::Object>(), _unitsPerMetre, options)) {
            return;
        }

        auto *replication = CoreModules::GetReplication();
        if (!replication) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "TextLabel.create: replication unavailable")));
            return;
        }

        int count = 0;
        replication->ForEach<LabelEntity>([&count](LabelEntity *) {
            ++count;
        });
        if (count >= kMaxLabels) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "TextLabel.create: label limit reached")));
            return;
        }

        auto *label = replication->CreateEntity<LabelEntity>();
        if (!label) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "TextLabel.create: failed to create entity (type not registered?)")));
            return;
        }

        label->position = glm::vec3(static_cast<float>(v8pp::from_v8<double>(isolate, info[0])), static_cast<float>(v8pp::from_v8<double>(isolate, info[1])), static_cast<float>(v8pp::from_v8<double>(isolate, info[2])));

        auto wrapped = v8pp::class_<TextLabel>::create_object(isolate, label->GetNetworkID());
        auto *handle = v8pp::class_<TextLabel>::unwrap_object(isolate, wrapped);
        handle->SetText(v8pp::from_v8<std::string>(isolate, info[3]));
        handle->SetDrawDistance(kDefaultMetres * _unitsPerMetre);

        if (withOptions) {
            ApplyOptions(*handle, *label, options);
        }
        else {
            if (info.Length() >= 5 && info[4]->IsNumber()) {
                handle->SetFontSize(static_cast<float>(v8pp::from_v8<double>(isolate, info[4])));
            }
            if (info.Length() >= 6 && info[5]->IsNumber()) {
                handle->SetDrawDistance(static_cast<float>(v8pp::from_v8<double>(isolate, info[5])));
            }
            if (info.Length() >= 7 && info[6]->IsNumber()) {
                handle->SetVirtualWorld(static_cast<uint32_t>(v8pp::from_v8<double>(isolate, info[6])));
            }
        }

        info.GetReturnValue().Set(wrapped);
    }

    void TextLabel::JS_Update(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        auto *self           = v8pp::class_<TextLabel>::unwrap_object(isolate, info.This());
        auto *label          = self ? self->ResolveLabel() : nullptr;
        if (!label) {
            return;
        }
        if (info.Length() < 1 || !info[0]->IsObject() || info[0]->IsArray()) {
            Fail(isolate, "update expects an options object");
            return;
        }
        Options options;
        if (ParseOptions(isolate, info[0].As<v8::Object>(), _unitsPerMetre, options)) {
            ApplyOptions(*self, *label, options);
        }
    }

    void TextLabel::JS_AttachTo(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        auto *self           = v8pp::class_<TextLabel>::unwrap_object(isolate, info.This());
        if (!self || !self->ResolveLabel()) {
            return;
        }
        const auto target = Target(isolate, info.Length() > 0 ? info[0] : v8::Undefined(isolate).As<v8::Value>(), "target");
        if (!target) {
            return;
        }
        std::optional<glm::vec3> offset = glm::vec3(0.0f);
        if (info.Length() > 1 && !info[1]->IsUndefined()) {
            offset = Vec3(isolate, info[1], "offset");
            if (!offset) {
                return;
            }
        }
        if (*target == 0) {
            self->Detach();
        }
        else {
            self->AttachTo(*target, *offset);
        }
    }

    void TextLabel::JS_GetOptions(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        auto *self           = v8pp::class_<TextLabel>::unwrap_object(isolate, info.This());
        auto *label          = self ? self->ResolveLabel() : nullptr;
        if (!label) {
            info.GetReturnValue().SetNull();
            return;
        }
        info.GetReturnValue().Set(OptionsObject(isolate, *label, self->GetVirtualWorld()));
    }

    void TextLabel::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.Unregister(isolate);
    }
} // namespace Framework::Scripting::Builtins
