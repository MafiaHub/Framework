/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "entity.h"

#include <networking/replication/entities/text_label_entity.h>

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Framework::Scripting::Builtins {
    // Server-side scripting handle for a replicated world-space text label (TextLabelEntity).
    class TextLabel final : public Entity {
      public:
        using LabelEntity = Networking::Replication::Entities::TextLabelEntity;
        using LabelStyle  = Networking::Replication::Entities::TextLabelStyle;

        static constexpr int kMaxLabels = 512;

        TextLabel(uint64_t networkId): Entity(networkId) {}

        LabelEntity *ResolveLabel() const;

        std::string GetText() const;
        void SetText(const std::string &text);

        uint32_t GetColor() const;
        void SetColor(int r, int g, int b, int a);
        // Write an already-packed ARGB value (e.g. from Color::toARGB()).
        void SetColorPacked(uint32_t argb);

        float GetFontSize() const;
        void SetFontSize(float size);

        float GetDrawDistance() const;
        void SetDrawDistance(float distance);

        float GetFadeDistance() const;
        void SetFadeDistance(float distance);

        uint32_t GetStyle() const;
        void SetStyle(uint32_t style);

        std::string ToString() const override;

        void Destroy();

        static v8pp::class_<TextLabel> &GetClass(v8::Isolate *isolate);

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);

      private:
        // JS: TextLabel.create(x, y, z, text, fontSize?, drawDistance?, virtualWorld?) -> TextLabel
        static void JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info);

        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<TextLabel>>> _classes;
    };
} // namespace Framework::Scripting::Builtins
