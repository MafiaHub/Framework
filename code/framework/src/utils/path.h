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
#include <string_view>

namespace Framework::Utils {
    std::wstring GetAbsolutePathW(const std::wstring &);
    std::string GetAbsolutePathA(std::string_view relative);
    std::wstring GetAppDataPathW();
    std::string GetAppDataPathA();
    std::wstring GetFileExtensionW(const std::wstring &path);
    std::string GetFileExtensionA(std::string_view path);

    // Resolves a URL-style path ("" and "/" map to "/index.html") against a
    // root directory, for safely serving files from a directory tree. Returns
    // an empty path when the input escapes the root (".." traversal, drive or
    // absolute-path injection). Purely lexical — existence is not checked.
    std::filesystem::path ResolvePathUnderRoot(const std::filesystem::path &root, std::string urlPath);
} // namespace Framework::Utils
