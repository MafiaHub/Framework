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
#include <unordered_set>

namespace Framework::Networking::Replication {
    class ReplicationManager;
} // namespace Framework::Networking::Replication

namespace Framework::Scripting::Builtins {
    // Server-side scripting handle for a replicated world-space text label (TextLabelEntity).
    class TextLabel final : public Entity {
      public:
        using LabelEntity = Networking::Replication::Entities::TextLabelEntity;
        using LabelStyle  = Networking::Replication::Entities::TextLabelStyle;

        static constexpr int kMaxLabels       = 512;
        static constexpr size_t kMaxTextBytes = 1024;
        static constexpr float kMinFontSize   = 1.0f;
        static constexpr float kMaxFontSize   = 512.0f;
        static constexpr float kDefaultMetres = 30.0f;
        static constexpr float kMaxDrawMetres = 1000.0f;

        TextLabel(uint64_t networkId): Entity(networkId) {}

        // World units in one metre, for the game to set before scripts run: draw-distance defaults
        // and limits are metres, scaled by it. 1 for a game in metres, 100 for one in centimetres.
        static void SetUnitsPerMetre(float unitsPerMetre);
        static float GetUnitsPerMetre();

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

        // Follows another live entity; false (and nothing changed) for a missing one or the label
        // itself. The label then streams from where the target is and moves into its virtual world,
        // and is destroyed with it.
        bool AttachTo(uint64_t networkId, const glm::vec3 &offset);
        // Back to the label's own position and virtual world.
        void Detach();
        bool CanAttachTo(uint64_t networkId) const;

        // Server tick, before the interest rebuild: moves attached labels to their targets and
        // destroys those whose target is gone.
        static void FollowTargets(Networking::Replication::ReplicationManager &replication);

        std::string ToString() const override;

        void Destroy();

        static v8pp::class_<TextLabel> &GetClass(v8::Isolate *isolate);

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);

      private:
        // JS: TextLabel.create(x, y, z, text, fontSize? | options?, drawDistance?, virtualWorld?) -> TextLabel
        static void JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Update(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_AttachTo(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetOptions(const v8::FunctionCallbackInfo<v8::Value> &info);

        static void RegisterOptionsType(v8::Isolate *isolate);

        inline static float _unitsPerMetre = 1.0f;
        inline static ClassCache<TextLabel> _classes;
        // Labels that may be attached; FollowTargets drops the ones that no longer are.
        inline static std::unordered_set<uint64_t> _attached;
    };
} // namespace Framework::Scripting::Builtins
