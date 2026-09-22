/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>
#include <string_view>

namespace Framework::Utils {
    std::wstring GetAbsolutePathW(const std::wstring &);
    std::string GetAbsolutePathA(std::string_view relative);
    std::wstring GetAppDataPathW();
    std::string GetAppDataPathA();
    // Directory of the module this code is linked into, for the files shipped beside it. Unlike
    // GetAbsolutePath*, which resolves against the PROCESS EXECUTABLE - the game's exe for an
    // injected client. Empty on failure and on non-Windows.
    std::wstring GetModuleDirW();
    std::string GetModuleDirA();
    std::wstring GetFileExtensionW(const std::wstring &path);
    std::string GetFileExtensionA(std::string_view path);
} // namespace Framework::Utils
