/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "system_graphics.h"

#include "logging/logger.h"
#include "utils/string_utils.h"

#include <Windows.h>

#include <atomic>

namespace Framework::Launcher {
    namespace {
        // Loads that map no code: the caller wants to read that very file
        constexpr DWORD kDataLoad = LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE;

        std::array<std::atomic<bool>, kSystemGraphicsLibraries.size()> gReported {};

        HMODULE LoadFromSystem32(std::wstring_view library) {
            return LoadLibraryExW(std::wstring(library).c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        }

        // Once per library: a load that named an existing file by path, which is a stand-in kept out
        void ReportStandIn(std::wstring_view library, const wchar_t *asked) {
            const std::wstring_view view(asked);
            if (view.find_first_of(L"\\/") == view.npos || GetFileAttributesW(asked) == INVALID_FILE_ATTRIBUTES) {
                return;
            }
            for (size_t i = 0; i < kSystemGraphicsLibraries.size(); ++i) {
                if (kSystemGraphicsLibraries[i] == library && !gReported[i].exchange(true)) {
                    Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("The game asked for {}; loaded the System32 {} instead", Utils::StringUtils::WideToNormal(asked), Utils::StringUtils::WideToNormal(std::wstring(library)));
                }
            }
        }
    } // namespace

    void PreloadSystemGraphicsLibraries() {
        for (const auto library : kSystemGraphicsLibraries) {
            LoadFromSystem32(library);
        }
    }

    void *LoadSystemGraphicsLibrary(const wchar_t *name, unsigned long flags) {
        if (!name || (flags & kDataLoad)) {
            return nullptr;
        }
        const auto library = SystemGraphicsLibraryName(name);
        if (!library) {
            return nullptr;
        }
        const HMODULE module = LoadFromSystem32(*library);
        if (module) {
            ReportStandIn(*library, name);
        }
        return module;
    }
} // namespace Framework::Launcher
