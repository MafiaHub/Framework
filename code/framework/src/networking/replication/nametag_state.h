/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "network_entity.h"

#include <cstdint>
#include <string>

namespace Framework::Networking::Replication {
    // Wire values are shared with External::ImGUI::Widgets::NameTagComponent; keep in sync.
    enum class NametagComponent : uint8_t {
        Name   = 1 << 0,
        Health = 1 << 1,
    };

    // A player avatar's nametag state. Games embed one, return it from NetworkEntity::GetNametag and
    // serialize it in their own SerializeFields. Scripted changes route through the owner (SetNametagState).
    struct NametagState {
        static constexpr uint8_t kAllComponents = static_cast<uint8_t>(NametagComponent::Name) | static_cast<uint8_t>(NametagComponent::Health);

        uint8_t components = kAllComponents;
        uint32_t color     = 0xFFFFFFFF; // packed 0xAARRGGBB
        std::string text;                // empty = the game's own label

        bool Has(NametagComponent component) const {
            return (components & static_cast<uint8_t>(component)) != 0;
        }

        void Set(NametagComponent component, bool enabled) {
            const uint8_t bit = static_cast<uint8_t>(component);
            components        = static_cast<uint8_t>(enabled ? (components | bit) : (components & static_cast<uint8_t>(~bit)));
        }

        void Serialize(FieldSerializer &fields) {
            fields.Field(components);
            fields.Field(color);
            fields.Field(text);
        }
    };
} // namespace Framework::Networking::Replication
