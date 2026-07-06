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

    // A server-owned piece of world-space text, interest-streamed and drawn client-side as a screen
    // overlay (External::ImGUI::Widgets::DrawWorldText). Mods register the type on both peers.
    class TextLabelEntity : public NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Framework::TextLabel";

        std::string text;                // UTF-8; '\n' splits lines
        uint32_t color     = 0xFFFFFFFF; // 0xAARRGGBB
        float fontSize     = 16.0f;      // pixels
        float drawDistance = 30.0f;      // hard cull (world units)
        float fadeDistance = 0.0f;       // fade start; <=0 derives from drawDistance

        TextLabelStyle style = TextLabelStyle::Shadow;

        // Reserved: follow another entity at an offset (no renderer consumes these yet).
        uint64_t attachNetworkId = 0;
        glm::vec3 attachOffset   = glm::vec3(0.0f);

        void SerializeFields(FieldSerializer &fields) override {
            // position lives in the NetworkEntity base; serialize it on the reliable channel.
            fields.Field(position.x);
            fields.Field(position.y);
            fields.Field(position.z);

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

            if (!fields.Writing()) {
                style = static_cast<TextLabelStyle>(styleRaw);
            }
        }
    };
} // namespace Framework::Networking::Replication::Entities
