/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>

// The parts of the nametag widget that do not draw, apart from nametag.h so that what drives them builds without
// the headers of ImGui.
namespace Framework::External::ImGUI::Widgets {
    // Wire values are shared with Networking::Replication::NametagComponent; keep in sync.
    enum class NameTagComponent : uint8_t {
        Name   = 1 << 0,
        Health = 1 << 1,
    };

    // The viewer's own switches over every tag they see, driven by the client Nametags builtin.
    // showSelf is read by the mod's draw pass, which owns the choice of which avatars get a tag.
    struct NameTagView {
        inline static bool showTags   = true;
        inline static bool showHealth = true;
        inline static bool showSelf   = false;
    };
} // namespace Framework::External::ImGUI::Widgets
