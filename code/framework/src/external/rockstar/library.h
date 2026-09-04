/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>
#include <vector>

namespace Framework::External::Rockstar {
    // One title the Rockstar Games Launcher has installed, as described by its registry key under
    // HKLM\SOFTWARE\Rockstar Games (WOW6432Node on a 64-bit host).
    struct InstalledTitle {
        std::string titleKey;      // registry sub-key name, e.g. "GTA: San Andreas"
        std::string installFolder; // install root ("InstallFolder")
        std::string version;       // launcher build of the title ("Version"), may be empty

        bool IsValid() const {
            return !installFolder.empty();
        }
    };

    // Install root of the Rockstar Games Launcher itself, empty when it was never installed.
    std::string GetLauncherPath();

    // Every title the launcher knows about. Empty when the launcher is not installed.
    std::vector<InstalledTitle> EnumerateInstalledTitles();

    // Find an installed title. When `titleKey` is non-empty it matches the registry sub-key name
    // (case-insensitive); otherwise the first title holding `exeFileName` wins. Returns an invalid
    // InstalledTitle when nothing matches.
    InstalledTitle FindInstalledTitle(const std::string &exeFileName, const std::string &titleKey = {});
} // namespace Framework::External::Rockstar
