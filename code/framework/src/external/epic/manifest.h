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

namespace Framework::External::Epic {
    // One installed title as described by an Epic Games Launcher manifest (.item file).
    struct InstalledApp {
        std::string appName;          // Epic catalog id ("AppName")
        std::string displayName;      // human-readable ("DisplayName")
        std::string installLocation;  // install root ("InstallLocation")
        std::string launchExecutable; // exe path relative to installLocation ("LaunchExecutable")
        std::string catalogNamespace; // EOS sandbox id ("CatalogNamespace")
        std::string catalogItemId;    // EOS item id ("CatalogItemId")

        bool IsValid() const {
            return !installLocation.empty();
        }
    };

    // Directory holding the Epic Games Launcher manifests
    // (%PROGRAMDATA%\Epic\EpicGamesLauncher\Data\Manifests). Never empty (falls back to the
    // conventional location), but the directory may not exist if Epic isn't installed or has
    // nothing installed.
    std::string GetManifestsDir();

    enum class ManifestsDirStatus {
        Present,      // the directory exists
        Created,      // it was missing and has just been created
        CreateFailed, // it was missing and couldn't be created, or the launcher's data dir couldn't be read
        NoLauncher,   // the Epic launcher's data directory is absent: Epic isn't installed
    };

    // The Epic launcher writes each install's .item into the manifests directory but never creates
    // it, so once the directory is gone no installed title gets recorded until it exists again.
    // Creates it when the launcher's data directory is there but the manifests directory isn't.
    ManifestsDirStatus EnsureManifestsDir();

    // Every installed title the Epic launcher knows about. Empty if Epic isn't installed or
    // no manifests could be read.
    std::vector<InstalledApp> EnumerateInstalledApps();

    // Find an installed title. When `appName` is non-empty, matches on the Epic catalog id;
    // otherwise matches the first title whose launch executable file name equals `exeFileName`
    // (case-insensitive). Returns an invalid InstalledApp when nothing matches.
    InstalledApp FindInstalledApp(const std::string &exeFileName, const std::string &appName = {});
} // namespace Framework::External::Epic
