/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace Framework::Utils {
    // A path held in a std::string is UTF-8 everywhere in the Framework. On Windows a narrow string
    // handed to std::filesystem::path, a file stream or fopen is read in the process code page
    // instead, so a profile named in another script (C:\Users\Иван) comes out as "????", or as
    // bytes that name no file. Every narrow path reaches the file system through these.
    std::filesystem::path PathFromUtf8(std::string_view utf8);
    std::string PathToUtf8(const std::filesystem::path &path);
    // fopen for a UTF-8 path (_wfopen on Windows).
    std::FILE *OpenFileUtf8(std::string_view utf8, const char *mode);

    std::wstring GetAbsolutePathW(const std::wstring &);
    std::string GetAbsolutePathA(std::string_view relative);
    std::wstring GetAppDataPathW();
    // UTF-8, like every narrow path (see PathFromUtf8).
    std::string GetAppDataPathA();
    // Directory of the module this code is linked into, for the files shipped beside it. Unlike
    // GetAbsolutePath*, which resolves against the PROCESS EXECUTABLE - the game's exe for an
    // injected client. Empty on failure and on non-Windows.
    std::wstring GetModuleDirW();
    std::string GetModuleDirA();
    std::wstring GetFileExtensionW(const std::wstring &path);
    std::string GetFileExtensionA(std::string_view path);
} // namespace Framework::Utils
