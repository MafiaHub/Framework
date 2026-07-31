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
    inline std::wstring NormalToWide(const std::string &str) {
        std::wstring wstr(str.length(), 0);
        std::transform(str.begin(), str.end(), wstr.begin(), [](char c) {
            return (wchar_t)c;
        });
        return wstr;
    }

    inline std::string WideToNormal(const std::wstring &wstr) {
        std::string str(wstr.length(), 0);
        std::transform(wstr.begin(), wstr.end(), str.begin(), [](wchar_t c) {
            return (char)c;
        });
        return str;
    }

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
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
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
