/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include <algorithm>
#include <codecvt>
#include <locale>
#include <regex>
#include <string>

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

    inline std::string LeftTrim(const std::string &s) {
        return std::regex_replace(s, std::regex("^\\s+"), std::string(""));
    }

    inline std::string RightTrim(const std::string &s) {
        return std::regex_replace(s, std::regex("\\s+$"), std::string(""));
    }

    inline std::string Trim(const std::string &s) {
        return LeftTrim(RightTrim(s));
    }
} // namespace Framework::Utils::StringUtils
