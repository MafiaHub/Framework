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
    // One title the Rockstar Games Launcher installed, from its key under HKLM\SOFTWARE\Rockstar Games.
    struct InstalledTitle {
        std::string titleKey;      // registry sub-key name, e.g. "GTA: San Andreas"
        std::string installFolder;
        std::string version;       // launcher build of the title, may be empty

        bool IsValid() const {
            return !installFolder.empty();
        }
    };

    // Empty when the launcher was never installed.
    std::string GetLauncherPath();

    std::vector<InstalledTitle> EnumerateInstalledTitles();

    // Matches `titleKey` when given, else the first title holding `exeFileName`.
    InstalledTitle FindInstalledTitle(const std::string &exeFileName, const std::string &titleKey = {});
} // namespace Framework::External::Rockstar
