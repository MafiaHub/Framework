/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/path.h"

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

MODULE(path, {
    IT("PathForNarrowApi keeps an ASCII path as it is, existing or not", {
        EQUALS(Framework::Utils::PathForNarrowApi("C:\\Some\\ascii path") == "C:\\Some\\ascii path", true);
    });

    IT("PathForNarrowApi gives an existing non-ASCII path an ASCII spelling a narrow API opens", {
        // Cyrillic plus a CJK character no single-byte code page holds: means the same on every system locale.
        // A name of its own, created here: another test process cannot pick the same folder.
        std::filesystem::path base;
        std::random_device random;
        do {
            base = std::filesystem::temp_directory_path() / (std::u8string(u8"fw_\u041F\u041A_\u4E2D_") + std::filesystem::path(std::to_string(random())).u8string());
        } while (!std::filesystem::create_directory(base));
        const std::string root = Framework::Utils::PathToUtf8(base);
        std::FILE *written     = Framework::Utils::OpenFileUtf8(root + "/x.sds", "wb");
        EQUALS(written != nullptr, true);
        if (written) {
            std::fputs("payload", written);
            std::fclose(written);
        }

        // On Windows the ASCII short name; elsewhere narrow APIs take UTF-8 and the path is kept. A Windows
        // volume with short names off has none to give: nothing to open then.
        const std::string narrow = Framework::Utils::PathForNarrowApi(root);
        const bool windows       = std::filesystem::path::preferred_separator == L'\\';
        if (!windows) {
            EQUALS(narrow == root, true);
        }
        if (!narrow.empty()) {
            std::FILE *read = std::fopen((narrow + "/x.sds").c_str(), "rb");
            EQUALS(read != nullptr, true);
            if (read) {
                std::fclose(read);
            }
        }
        std::filesystem::remove_all(base);
    });

    IT("PathForNarrowApi gives nothing for an absent non-ASCII path on Windows", {
        const std::string absent = Framework::Utils::PathToUtf8(std::filesystem::temp_directory_path() / std::u8string(u8"fw_\u4E2D_absent"));
        const bool windows       = std::filesystem::path::preferred_separator == L'\\';
        EQUALS(Framework::Utils::PathForNarrowApi(absent).empty(), windows);
    });
});
