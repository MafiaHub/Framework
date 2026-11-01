/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "external/epic/manifest.h"

#include <cstdlib>
#include <filesystem>
#include <string>

MODULE(epic_manifest, {
    using namespace Framework::External::Epic;

    // GetManifestsDir resolves under %PROGRAMDATA%, so point that at a scratch folder.
    const char *previous           = std::getenv("PROGRAMDATA");
    const std::string savedProgram = previous ? previous : "";
    const auto programData         = std::filesystem::temp_directory_path() / "fw_epic_manifest_ut";
    const auto launcherData        = programData / "Epic" / "EpicGamesLauncher" / "Data";
    std::filesystem::remove_all(programData);
    std::filesystem::create_directories(programData);
    _putenv_s("PROGRAMDATA", programData.string().c_str());

    IT("leaves no manifests folder behind when Epic isn't installed", {
        EQUALS(EnsureManifestsDir() == ManifestsDirStatus::NoLauncher, true);
        EQUALS(std::filesystem::exists(launcherData), false);
    });

    IT("creates the manifests folder the Epic launcher dropped", {
        std::filesystem::create_directories(launcherData);
        EQUALS(EnsureManifestsDir() == ManifestsDirStatus::Created, true);
        EQUALS(std::filesystem::is_directory(GetManifestsDir()), true);
        EQUALS(EnsureManifestsDir() == ManifestsDirStatus::Present, true);
    });

    _putenv_s("PROGRAMDATA", savedProgram.c_str());
    std::filesystem::remove_all(programData);
});
