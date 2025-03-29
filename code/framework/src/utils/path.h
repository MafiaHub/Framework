/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

namespace Framework::Utils {
    std::wstring GetAbsolutePathW(const std::wstring &);
    std::string GetAbsolutePathA(const std::string &);
    std::wstring GetAppDataPathW();
    std::string GetAppDataPathA();
    std::wstring GetFileExtensionW(const std::wstring &path);
    std::string GetFileExtensionA(const std::string &path);
} // namespace Framework::Utils
