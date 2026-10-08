/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <Windows.h>

#include <string>

namespace Framework::External::MicrosoftStore {
    // One package the Microsoft Store (or the Xbox app) installed for the current Windows user.
    struct InstalledPackage {
        std::wstring fullName;    // e.g. Publisher.Game_1.2.3.0_x64__hash
        std::wstring installPath; // the package root, under WindowsApps or XboxGames

        bool IsValid() const {
            return !installPath.empty();
        }
    };

    // The package of `packageFamilyName` installed for the current user, invalid when there is none.
    InstalledPackage FindInstalledPackage(const std::wstring &packageFamilyName);

    // True when this process already runs with the identity of `packageFamilyName`.
    bool IsRunningInPackage(const std::wstring &packageFamilyName);

    // Starts `executable` (any full-trust exe, not one of the package's own) with the package's
    // identity, as Invoke-CommandInDesktopPackage does. That identity is what a GDK title's runtime,
    // licensing and saves are tied to, and what lets the package's licence-protected executable be
    // read. Returns the new process' handle, or null with the HRESULT in `error`.
    HANDLE StartInPackage(const std::wstring &packageFamilyName, const std::wstring &appId, const std::wstring &executable, const std::wstring &arguments, HRESULT &error);
} // namespace Framework::External::MicrosoftStore
