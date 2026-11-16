/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Framework::GUI::CEF {
    // The files CefInitialize needs in the module's folder, as paths relative to it. Chromium aborts
    // the process over a missing one (a fatal breakpoint for the locale pack) instead of failing the
    // call. The locale is CEF's default: CefSettings::locale is left empty, which means en-US
    // whatever the language of Windows.
    inline constexpr const char *kRuntimeFiles[] = {"libcef.dll", "cef_subprocess.exe", "chrome_elf.dll", "icudtl.dat", "resources.pak", "chrome_100_percent.pak", "chrome_200_percent.pak", "v8_context_snapshot.bin", "locales/en-US.pak"};

    inline std::vector<std::string> MissingRuntimeFiles(const std::filesystem::path &moduleDir) {
        std::vector<std::string> missing;
        for (const char *file : kRuntimeFiles) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(moduleDir / file, error)) {
                missing.emplace_back(file);
            }
        }
        return missing;
    }
} // namespace Framework::GUI::CEF
