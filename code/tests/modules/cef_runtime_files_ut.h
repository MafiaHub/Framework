/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "gui/cef/runtime_files.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

MODULE(cef_runtime_files, {
    using Framework::GUI::CEF::kRuntimeFiles;
    using Framework::GUI::CEF::MissingRuntimeFiles;
    namespace fs = std::filesystem;

    const fs::path root = fs::temp_directory_path() / "fw_cef_runtime_files_ut";
    std::error_code ec;
    fs::remove_all(root, ec);

    const auto touch = [](const fs::path &path) {
        std::error_code dirEc;
        fs::create_directories(path.parent_path(), dirEc);
        std::ofstream(path, std::ios::binary) << 'x';
    };

    for (const char *file : kRuntimeFiles) {
        touch(root / "whole" / file);
        if (std::string(file) != "locales/en-US.pak") {
            touch(root / "nolocale" / file);
        }
    }
    // Another language's pack does not stand in: CEF starts with en-US
    touch(root / "nolocale" / "locales" / "fr.pak");

    IT("passes a folder that has every file", {
        EQUALS(MissingRuntimeFiles(root / "whole").empty(), true);
    });

    IT("names what an incomplete folder lacks", {
        const auto noLocale = MissingRuntimeFiles(root / "nolocale");
        EQUALS(noLocale.size() == 1 && noLocale[0] == "locales/en-US.pak", true);
        EQUALS(MissingRuntimeFiles(root / "absent").size() == std::size(kRuntimeFiles), true);
    });

    fs::remove_all(root, ec);
});
