/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "text_label.h"
#include "../scripting_catalog.h"

#include <core_modules.h>

#include <algorithm>
#include <sstream>

namespace Framework::Scripting::Builtins {
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
        }
    }

    uint32_t TextLabel::GetColor() const {
        auto *label = ResolveLabel();
        return label ? label->color : 0xFFFFFFFF;
    }

    void TextLabel::SetColor(int r, int g, int b, int a) {
        if (auto *label = ResolveLabel()) {
            const auto clamp = [](int c) {
                return static_cast<uint32_t>(std::clamp(c, 0, 255));
            };
            label->color = (clamp(a) << 24) | (clamp(r) << 16) | (clamp(g) << 8) | clamp(b);
        }
    }

    float TextLabel::GetFontSize() const {
        auto *label = ResolveLabel();
        return label ? label->fontSize : 0.0f;
    }

    void TextLabel::SetFontSize(float size) {
        if (auto *label = ResolveLabel()) {
            label->fontSize = std::clamp(size, 6.0f, 128.0f);
        }
    }

    float TextLabel::GetDrawDistance() const {
        auto *label = ResolveLabel();
        return label ? label->drawDistance : 0.0f;
    }

    void TextLabel::SetDrawDistance(float distance) {
        if (auto *label = ResolveLabel()) {
            label->drawDistance = std::clamp(distance, 1.0f, 1000.0f);
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
                label->style = static_cast<LabelStyle>(style);
            }
        }
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
        auto it = _classes.find(isolate);
        if (it != _classes.end()) {
            return *it->second;
        }

        Entity::GetClass(isolate);

        auto &cls = _classes[isolate];
        cls       = std::make_unique<v8pp::class_<TextLabel>>(isolate, GetScriptingCatalog(isolate), "TextLabel", "Server-created replicated world-space text label.");
        cls->auto_wrap_objects(true);
        cls->document_base("Entity");
        cls->inherit<Entity>()
            .ctor<uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Existing text-label network entity identifier.")}, "Creates a wrapper for an existing text label; use TextLabel.create to spawn one."))
            .function("toString", &TextLabel::ToString, v8pp::metadata::docs("string", {}, "Formats this label for logging and debugging.", "Text containing the label ID and current text."))
            .function("destroy", &TextLabel::Destroy, v8pp::metadata::docs("void", {}, "Destroys this replicated label; stale wrappers no longer resolve afterward."))
            .function("setText", &TextLabel::SetText, v8pp::metadata::docs("void", {v8pp::metadata::param("text", "string", false, "Replacement text replicated to clients.")}, "Changes the label's displayed text."))
            .function("setColor", &TextLabel::SetColor,
                v8pp::metadata::docs("void",
                    {v8pp::metadata::param("r", "number", false, "Red byte, clamped from 0 to 255."), v8pp::metadata::param("g", "number", false, "Green byte, clamped from 0 to 255."), v8pp::metadata::param("b", "number", false, "Blue byte, clamped from 0 to 255."),
                        v8pp::metadata::param("a", "number", false, "Alpha byte, clamped from 0 to 255.")},
                    "Changes the label's packed ARGB color."))
            .function("setFontSize", &TextLabel::SetFontSize, v8pp::metadata::docs("void", {v8pp::metadata::param("size", "number", false, "Font size clamped from 6 to 128.")}, "Changes the rendered font size."))
            .function("setDrawDistance", &TextLabel::SetDrawDistance, v8pp::metadata::docs("void", {v8pp::metadata::param("distance", "number", false, "Maximum rendering distance, clamped from 1 to 1000 world units.")}, "Changes how far away clients can render the label."))
            .function("setFadeDistance", &TextLabel::SetFadeDistance,
                v8pp::metadata::docs("void", {v8pp::metadata::param("distance", "number", false, "Distance over which the label fades out; negative values become zero.")}, "Changes the fade range approaching the draw-distance limit."))
            .function("setStyle", &TextLabel::SetStyle, v8pp::metadata::docs("void", {v8pp::metadata::param("style", "number", false, "Framework text-label style enum value; unsupported values are ignored.")}, "Changes the label's rendering style."))
            .property("text", &TextLabel::GetText, v8pp::metadata::property_docs("string", "Current replicated label text."))
            .property("color", &TextLabel::GetColor, v8pp::metadata::property_docs("number", "Current packed ARGB color."))
            .property("fontSize", &TextLabel::GetFontSize, v8pp::metadata::property_docs("number", "Current font size."))
            .property("drawDistance", &TextLabel::GetDrawDistance, v8pp::metadata::property_docs("number", "Current maximum rendering distance in world units."))
            .property("fadeDistance", &TextLabel::GetFadeDistance, v8pp::metadata::property_docs("number", "Current fade range in world units."))
            .property("style", &TextLabel::GetStyle, v8pp::metadata::property_docs("number", "Current framework text-label style enum value."));

        return *cls;
    }

    void TextLabel::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<TextLabel> &cls = GetClass(isolate);
        auto ctx                     = isolate->GetCurrentContext();

        auto ctorFn = cls.js_function_template()->GetFunction(ctx).ToLocalChecked();

        cls.static_function(ctorFn, "create", &TextLabel::JS_Create,
            v8pp::metadata::docs("TextLabel",
                {
                    v8pp::metadata::param("x", "number", false, "World-space X coordinate."),
                    v8pp::metadata::param("y", "number", false, "World-space Y coordinate."),
                    v8pp::metadata::param("z", "number", false, "World-space Z coordinate."),
                    v8pp::metadata::param("text", "string", false, "Initial displayed text."),
                    v8pp::metadata::param("fontSize", "number", true, "Optional initial font size from 6 to 128."),
                    v8pp::metadata::param("drawDistance", "number", true, "Optional initial draw distance from 1 to 1000 world units."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual-world identifier; defaults to the entity default."),
                },
                "Creates and replicates a world-space text label, up to the server-wide limit of 512.", "Handle for the newly created label."));
        cls.publish(global, ctorFn);
    }

    void TextLabel::JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        if (info.Length() < 4 || !info[0]->IsNumber() || !info[1]->IsNumber() || !info[2]->IsNumber() || !info[3]->IsString()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "TextLabel.create: expected (x, y, z, text, fontSize?, drawDistance?, virtualWorld?)"));
            return;
        }

        auto *replication = CoreModules::GetReplication();
        if (!replication) {
            isolate->ThrowError(v8pp::to_v8(isolate, "TextLabel.create: replication unavailable"));
            return;
        }

        int count = 0;
        replication->ForEach<LabelEntity>([&count](LabelEntity *) {
            ++count;
        });
        if (count >= kMaxLabels) {
            isolate->ThrowError(v8pp::to_v8(isolate, "TextLabel.create: label limit reached"));
            return;
        }

        auto *label = replication->CreateEntity<LabelEntity>();
        if (!label) {
            isolate->ThrowError(v8pp::to_v8(isolate, "TextLabel.create: failed to create entity (type not registered?)"));
            return;
        }

        label->position = glm::vec3(static_cast<float>(v8pp::from_v8<double>(isolate, info[0])), static_cast<float>(v8pp::from_v8<double>(isolate, info[1])), static_cast<float>(v8pp::from_v8<double>(isolate, info[2])));
        label->text     = v8pp::from_v8<std::string>(isolate, info[3]);

        auto wrapped = v8pp::class_<TextLabel>::create_object(isolate, label->GetNetworkID());
        auto *handle = v8pp::class_<TextLabel>::unwrap_object(isolate, wrapped);

        if (info.Length() >= 5 && info[4]->IsNumber()) {
            handle->SetFontSize(static_cast<float>(v8pp::from_v8<double>(isolate, info[4])));
        }
        if (info.Length() >= 6 && info[5]->IsNumber()) {
            handle->SetDrawDistance(static_cast<float>(v8pp::from_v8<double>(isolate, info[5])));
        }
        if (info.Length() >= 7 && info[6]->IsNumber()) {
            handle->SetVirtualWorld(static_cast<uint32_t>(v8pp::from_v8<double>(isolate, info[6])));
        }

        info.GetReturnValue().Set(wrapped);
    }
} // namespace Framework::Scripting::Builtins
