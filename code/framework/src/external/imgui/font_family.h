/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>
#include <string_view>

namespace Framework::External::ImGUI {
    // A family's faces as TTF/OTF file contents, as a script registers them; an empty one is left out.
    struct FontFamilySource {
        std::string regular;
        std::string bold;
        std::string italic;
        std::string boldItalic;
    };

    // By the file's own tag, not its name.
    inline bool IsWebFont(std::string_view bytes) {
        const std::string_view tag = bytes.substr(0, 4);
        return tag == "wOFF" || tag == "wOF2";
    }

    inline bool IsFontFile(std::string_view bytes) {
        const std::string_view tag = bytes.substr(0, 4);
        return tag == std::string_view("\0\1\0\0", 4) || tag == "true" || tag == "OTTO" || tag == "ttcf";
    }
} // namespace Framework::External::ImGUI
