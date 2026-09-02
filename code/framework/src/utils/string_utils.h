/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <locale>
#include <regex>
#include <string>
#include <string_view>

namespace Framework::Utils::StringUtils {
#ifdef _WIN32
    // A narrow string on Windows carries one of two encodings. UTF-8 is what cppfs,
    // std::filesystem, nlohmann and CEF read; the ACP is what the CRT's narrow file
    // APIs and the ANSI Win32 calls read. Name the one you mean at every call site.
    std::string WideToUTF8(std::wstring_view wide);
    std::wstring UTF8ToWide(std::string_view utf8);
    std::string WideToACP(std::wstring_view wide);
    std::wstring ACPToWide(std::string_view acp);
#endif

    inline std::string LeftTrim(std::string_view s) {
        return std::regex_replace(std::string(s), std::regex("^\\s+"), std::string(""));
    }

    inline std::string RightTrim(std::string_view s) {
        return std::regex_replace(std::string(s), std::regex("\\s+$"), std::string(""));
    }

    inline std::string Trim(std::string_view s) {
        return LeftTrim(RightTrim(s));
    }

    inline std::string ToLower(std::string s) {
        std::ranges::transform(s, s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    }

    // File name (portion after the last / or \) of a possibly-relative path.
    inline std::string FileName(std::string_view path) {
        const auto pos = path.find_last_of("/\\");
        return std::string(pos == std::string_view::npos ? path : path.substr(pos + 1));
    }
} // namespace Framework::Utils::StringUtils
