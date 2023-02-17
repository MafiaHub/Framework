/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <algorithm>
#include <codecvt>
#include <locale>
#include <regex>
#include <string>

namespace Framework::Utils::StringUtils {
    inline std::wstring NormalToWide(const std::string &str) {
        using conversionType = std::codecvt_utf8<wchar_t>;
        return std::wstring_convert<conversionType, wchar_t>().from_bytes(str);
    }

    inline std::string WideToNormal(const std::wstring &str) {
        using conversionType = std::codecvt_utf8<wchar_t>;
        return std::wstring_convert<conversionType, wchar_t>().to_bytes(str);
    }

    inline std::wstring NormalToWideDirect(const std::string &str) {
        std::wstring wstr(str.length(), 0);
        std::transform(str.begin(), str.end(), wstr.begin(), [](char c) {
            return (wchar_t)c;
        });
        return wstr;
    }

    inline std::string WideToNormalDirect(const std::wstring &wstr) {
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
