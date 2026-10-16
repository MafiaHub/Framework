/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../network_entity.h"

#include <cstdint>
#include <string>

namespace Framework::Networking::Replication::Entities {
    // Wire values are shared with External::ImGUI::Widgets::WorldTextStyle; keep them in sync.
    enum class TextLabelStyle : uint8_t {
        None    = 0,
        Shadow  = 1,
        Outline = 2,
        Box     = 3,
    };

    enum class TextLabelSizeMode : uint8_t {
        Screen = 0, // fontSize pixels at any distance
        World  = 1, // fontSize pixels at one metre, shrinking with distance
    };

    // Wire values are shared with External::ImGUI::Widgets::WorldTextAlign / WorldTextAnchor.
    enum class TextLabelAlign : uint8_t {
        Left   = 0,
        Center = 1,
        Right  = 2,
    };

    enum class TextLabelAnchor : uint8_t {
        Center = 0,
        Bottom = 1,
        Top    = 2,
    };

    // Limits on a label's look: the scripting API enforces them and the client renderer assumes them.
    inline constexpr float kTextLabelMaxOutlineWidth = 4.0f;
    inline constexpr float kTextLabelMaxShadowBlur   = 8.0f;

    // A server-owned piece of world-space text, interest-streamed and drawn client-side as a screen
    // overlay. Mods register the type on both peers.
    class TextLabelEntity : public NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Framework::TextLabel";

        std::string text;                // UTF-8; '\n' splits lines
        uint32_t color     = 0xFFFFFFFF; // 0xAARRGGBB
        float fontSize     = 16.0f;      // pixels
        float drawDistance = 30.0f;      // hard cull (world units)
        float fadeDistance = 0.0f;       // fade start; <=0 derives from drawDistance

        // The preset nearest the look below, for renderers that only draw presets.
        TextLabelStyle style = TextLabelStyle::Shadow;

        // Follow another entity at an offset; 0 keeps the label at its own position.
        uint64_t attachNetworkId = 0;
        glm::vec3 attachOffset   = glm::vec3(0.0f);

        // Server only: the label's own position and virtual world while it is attached. The server
        // moves it with the target, so it streams from there, but peers draw at the target and are
        // sent ownPosition instead; detaching puts both back.
        glm::vec3 ownPosition    = glm::vec3(0.0f);
        uint32_t ownVirtualWorld = 0;

        // The full look. Colours are 0xAARRGGBB; a colour with zero alpha turns its part off.
        bool markup                = false; // {#RRGGBB[AA]} {#} {b}{/b} {i}{/i}, {{ for a brace
        TextLabelSizeMode sizeMode = TextLabelSizeMode::Screen;
        float minSize              = 0.0f; // world mode clamps; 0 = none
        float maxSize              = 0.0f;
        float opacity              = 1.0f;
        float outlineWidth         = 0.0f;
        uint32_t outlineColor      = 0xFF000000;
        uint32_t shadowColor       = 0xFF000000;
        float shadowOffsetX        = 1.0f;
        float shadowOffsetY        = 1.0f;
        float shadowBlur           = 0.0f;
        uint32_t backgroundColor   = 0;
        float backgroundPadding    = 4.0f;
        float backgroundRadius     = 4.0f;
        TextLabelAlign align       = TextLabelAlign::Center;
        TextLabelAnchor anchor     = TextLabelAnchor::Center;
        float lineSpacing          = 1.0f;
        float maxWidth             = 0.0f; // wrap width in fontSize pixels; 0 = never
        bool occlusion             = true; // hidden behind world geometry

        // Not replicated: bumped whenever the label is constructed or updated, so a renderer can
        // keep its layout until something changes.
        uint32_t revision = 0;

        void OnConstructed() override {
            ++revision;
        }

        void OnDeserialized(bool transformUpdated) override {
            (void)transformUpdated;
            ++revision;
        }

        void SerializeTransform(FieldSerializer &fields) override {
            fields.Field(Placed(fields));
            fields.Field(velocity);
            fields.Field(rotation);
        }

        void SerializeFields(FieldSerializer &fields) override {
            // position lives in the NetworkEntity base; serialize it on the reliable channel.
            glm::vec3 &placed = Placed(fields);
            fields.Field(placed.x);
            fields.Field(placed.y);
            fields.Field(placed.z);

            fields.Field(text);
            fields.Field(color);
            fields.Field(fontSize);
            fields.Field(drawDistance);
            fields.Field(fadeDistance);

            uint8_t styleRaw = static_cast<uint8_t>(style);
            fields.Field(styleRaw);

            fields.Field(attachNetworkId);
            fields.Field(attachOffset.x);
            fields.Field(attachOffset.y);
            fields.Field(attachOffset.z);

            uint8_t sizeModeRaw = static_cast<uint8_t>(sizeMode);
            uint8_t alignRaw    = static_cast<uint8_t>(align);
            uint8_t anchorRaw   = static_cast<uint8_t>(anchor);
            fields.Field(markup);
            fields.Field(sizeModeRaw);
            fields.Field(minSize);
            fields.Field(maxSize);
            fields.Field(opacity);
            fields.Field(outlineWidth);
            fields.Field(outlineColor);
            fields.Field(shadowColor);
            fields.Field(shadowOffsetX);
            fields.Field(shadowOffsetY);
            fields.Field(shadowBlur);
            fields.Field(backgroundColor);
            fields.Field(backgroundPadding);
            fields.Field(backgroundRadius);
            fields.Field(alignRaw);
            fields.Field(anchorRaw);
            fields.Field(lineSpacing);
            fields.Field(maxWidth);
            fields.Field(occlusion);

            if (!fields.Writing()) {
                style    = static_cast<TextLabelStyle>(styleRaw);
                sizeMode = static_cast<TextLabelSizeMode>(sizeModeRaw);
                align    = static_cast<TextLabelAlign>(alignRaw);
                anchor   = static_cast<TextLabelAnchor>(anchorRaw);
            }
        }

      private:
        // What peers are sent as the label's position: an attached label's own, not its target's.
        glm::vec3 &Placed(const FieldSerializer &fields) {
            return fields.Writing() && attachNetworkId != 0 ? ownPosition : position;
        }
    };

    // Sets the look a preset stands for: the parts DrawWorldText draws for it, the rest off.
    inline void ApplyTextLabelStylePreset(TextLabelEntity &label, TextLabelStyle preset) {
        label.style             = preset;
        label.outlineWidth      = preset == TextLabelStyle::Outline ? 1.0f : 0.0f;
        label.outlineColor      = 0xFF000000;
        label.shadowColor       = preset == TextLabelStyle::Shadow ? 0xFF000000 : 0;
        label.shadowOffsetX     = 1.0f;
        label.shadowOffsetY     = 1.0f;
        label.shadowBlur        = 0.0f;
        label.backgroundColor   = preset == TextLabelStyle::Box ? 0x99000000 : 0;
        label.backgroundPadding = 4.0f;
        label.backgroundRadius  = 4.0f;
    }

    // The preset a look reads closest to: a plate dominates, then an outline, then a shadow.
    inline TextLabelStyle NearestTextLabelStylePreset(const TextLabelEntity &label) {
        if ((label.backgroundColor >> 24) != 0) {
            return TextLabelStyle::Box;
        }
        if (label.outlineWidth > 0.0f && (label.outlineColor >> 24) != 0) {
            return TextLabelStyle::Outline;
        }
        if ((label.shadowColor >> 24) != 0) {
            return TextLabelStyle::Shadow;
        }
        return TextLabelStyle::None;
    }
} // namespace Framework::Networking::Replication::Entities
