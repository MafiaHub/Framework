/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

struct ImFont;

namespace Framework::External::ImGUI {
    // A family's faces as TTF/OTF file contents, as a script registers them; an empty one is left out.
    struct FontFamilySource {
        std::string regular;
        std::string bold;
        std::string italic;
        std::string boldItalic;
    };

    // A family's faces in the atlas. A null one is the drawer's to synthesize, or to fall back on.
    struct FontFamily {
        ImFont *regular    = nullptr;
        ImFont *bold       = nullptr;
        ImFont *italic     = nullptr;
        ImFont *boldItalic = nullptr;
    };
} // namespace Framework::External::ImGUI
