/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <array>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>

namespace Framework::Launcher {
    // The graphics DLLs a tool replaces by dropping its own next to the game's executable (ReShade,
    // OptiScaler, DXVK). See ProjectConfiguration::useSystemGraphicsLibraries.
    inline constexpr std::array<std::wstring_view, 5> kSystemGraphicsLibraries = {L"dxgi.dll", L"d3d11.dll", L"d3d12.dll", L"d3d9.dll", L"opengl32.dll"};

    // The System32 name to load for a LoadLibrary argument, whatever folder it names; nullopt for any
    // other DLL.
    inline std::optional<std::wstring_view> SystemGraphicsLibraryName(std::wstring_view path) {
        if (const auto slash = path.find_last_of(L"\\/"); slash != std::wstring_view::npos) {
            path.remove_prefix(slash + 1);
        }
        std::wstring name(path);
        for (auto &c : name) {
            c = static_cast<wchar_t>(std::towlower(c));
        }
        // LoadLibrary appends .dll to a name without an extension
        if (name.find(L'.') == std::wstring::npos) {
            name += L".dll";
        }
        for (const auto library : kSystemGraphicsLibraries) {
            if (name == library) {
                return library;
            }
        }
        return std::nullopt;
    }

    // Maps the System32 copies, so every later load by name, the game's imports included, gets them.
    // Must run before the game's folders join the DLL search path.
    void PreloadSystemGraphicsLibraries();

    // The System32 module for a load of one of the libraries above (an HMODULE, with a reference the
    // caller owns, as LoadLibrary gives). Null for any other name, for a load that maps no code
    // (flags are LoadLibraryEx's) and when System32 has no such DLL: the caller then loads as asked.
    void *LoadSystemGraphicsLibrary(const wchar_t *name, unsigned long flags);
} // namespace Framework::Launcher
