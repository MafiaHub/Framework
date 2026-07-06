/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "entity.h"
#include "property.h"

#include <networking/replication/entities/text_label_entity.h>

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

namespace Framework::Scripting::Builtins {
    // Server-side scripting handle for a replicated world-space text label (TextLabelEntity).
    // Header-only so it compiles against whichever V8 the including target links.
    class TextLabel final : public Entity {
      public:
        using LabelEntity = Networking::Replication::Entities::TextLabelEntity;
        using LabelStyle  = Networking::Replication::Entities::TextLabelStyle;

        static constexpr int kMaxLabels = 512;

        TextLabel(uint64_t networkId): Entity(networkId) {}

        LabelEntity *ResolveLabel() const {
            return dynamic_cast<LabelEntity *>(Resolve());
        }

        std::string GetText() const {
            auto *label = ResolveLabel();
            return label ? label->text : std::string();
        }

        void SetText(const std::string &text) {
            if (auto *label = ResolveLabel()) {
                label->text = text;
            }
        }

        uint32_t GetColor() const {
            auto *label = ResolveLabel();
            return label ? label->color : 0xFFFFFFFF;
        }

        void SetColor(int r, int g, int b, int a) {
            if (auto *label = ResolveLabel()) {
                const auto clamp = [](int c) { return static_cast<uint32_t>(std::clamp(c, 0, 255)); };
                label->color = (clamp(a) << 24) | (clamp(r) << 16) | (clamp(g) << 8) | clamp(b);
            }
        }

        float GetFontSize() const {
            auto *label = ResolveLabel();
            return label ? label->fontSize : 0.0f;
        }

        void SetFontSize(float size) {
            if (auto *label = ResolveLabel()) {
                label->fontSize = std::clamp(size, 6.0f, 128.0f);
            }
        }

        float GetDrawDistance() const {
            auto *label = ResolveLabel();
            return label ? label->drawDistance : 0.0f;
        }

        void SetDrawDistance(float distance) {
            if (auto *label = ResolveLabel()) {
                label->drawDistance = std::clamp(distance, 1.0f, 1000.0f);
            }
        }

        float GetFadeDistance() const {
            auto *label = ResolveLabel();
            return label ? label->fadeDistance : 0.0f;
        }

        void SetFadeDistance(float distance) {
            if (auto *label = ResolveLabel()) {
                label->fadeDistance = std::max(distance, 0.0f);
            }
        }

        uint32_t GetStyle() const {
            auto *label = ResolveLabel();
            return label ? static_cast<uint32_t>(label->style) : 0;
        }

        void SetStyle(uint32_t style) {
            if (auto *label = ResolveLabel()) {
                if (style <= static_cast<uint32_t>(LabelStyle::Box)) {
                    label->style = static_cast<LabelStyle>(style);
                }
            }
        }

        std::string ToString() const override {
            std::ostringstream ss;
            ss << "TextLabel{ id: " << _id << ", text: \"" << GetText() << "\" }";
            return ss.str();
        }

        void Destroy() {
            auto *label = ResolveLabel();
            if (!label) {
                return;
            }
            if (auto *replication = CoreModules::GetReplication()) {
                replication->DestroyEntity(label);
            }
        }

        static v8pp::class_<TextLabel> &GetClass(v8::Isolate *isolate) {
            auto it = _classes.find(isolate);
            if (it != _classes.end()) {
                return *it->second;
            }

            Entity::GetClass(isolate);

            auto &cls = _classes[isolate];
            cls = std::make_unique<v8pp::class_<TextLabel>>(isolate);
            cls->auto_wrap_objects(true);
            cls->inherit<Entity>()
                .ctor<uint64_t>()
                .function("toString", &TextLabel::ToString)
                .function("destroy", &TextLabel::Destroy)
                .function("setText", &TextLabel::SetText)
                .function("setColor", &TextLabel::SetColor)
                .function("setFontSize", &TextLabel::SetFontSize)
                .function("setDrawDistance", &TextLabel::SetDrawDistance)
                .function("setFadeDistance", &TextLabel::SetFadeDistance)
                .function("setStyle", &TextLabel::SetStyle);

            auto protoTemplate = cls->class_function_template()->PrototypeTemplate();
            RegisterReadonlyProperty<TextLabel, &TextLabel::GetText>(isolate, protoTemplate, "text");
            RegisterReadonlyProperty<TextLabel, &TextLabel::GetColor>(isolate, protoTemplate, "color");
            RegisterReadonlyProperty<TextLabel, &TextLabel::GetFontSize>(isolate, protoTemplate, "fontSize");
            RegisterReadonlyProperty<TextLabel, &TextLabel::GetDrawDistance>(isolate, protoTemplate, "drawDistance");
            RegisterReadonlyProperty<TextLabel, &TextLabel::GetFadeDistance>(isolate, protoTemplate, "fadeDistance");
            RegisterReadonlyProperty<TextLabel, &TextLabel::GetStyle>(isolate, protoTemplate, "style");

            return *cls;
        }

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
            v8pp::class_<TextLabel> &cls = GetClass(isolate);
            auto ctx                     = isolate->GetCurrentContext();

            auto ctorFn = cls.js_function_template()->GetFunction(ctx).ToLocalChecked();

            auto createFn = v8::Function::New(ctx, &TextLabel::JS_Create).ToLocalChecked();
            ctorFn->Set(ctx, v8pp::to_v8(isolate, "create"), createFn).Check();

            global->Set(ctx, v8pp::to_v8(isolate, "TextLabel"), ctorFn).Check();
        }

      private:
        // JS: TextLabel.create(x, y, z, text, fontSize?, drawDistance?, virtualWorld?) -> TextLabel
        static void JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
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

            label->position = glm::vec3(
                static_cast<float>(v8pp::from_v8<double>(isolate, info[0])),
                static_cast<float>(v8pp::from_v8<double>(isolate, info[1])),
                static_cast<float>(v8pp::from_v8<double>(isolate, info[2])));
            label->text = v8pp::from_v8<std::string>(isolate, info[3]);

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

        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<TextLabel>>> _classes;
    };
} // namespace Framework::Scripting::Builtins
