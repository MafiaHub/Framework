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

    // Decodes UTF-8, unlike NormalToWide which widens byte by byte. Invalid sequences are dropped.
    inline std::wstring Utf8ToWide(std::string_view str) {
        std::wstring out;
        out.reserve(str.size());
        for (size_t i = 0; i < str.size();) {
            const unsigned char lead = static_cast<unsigned char>(str[i]);
            char32_t codepoint       = 0;
            size_t continuations     = 0;
            if (lead < 0x80) {
                codepoint = lead;
            }
            else if ((lead & 0xE0) == 0xC0) {
                codepoint     = lead & 0x1F;
                continuations = 1;
            }
            else if ((lead & 0xF0) == 0xE0) {
                codepoint     = lead & 0x0F;
                continuations = 2;
            }
            else if ((lead & 0xF8) == 0xF0) {
                codepoint     = lead & 0x07;
                continuations = 3;
            }
            else {
                ++i;
                continue;
            }
            if (i + continuations >= str.size()) {
                break;
            }
            bool valid = true;
            for (size_t n = 1; n <= continuations; ++n) {
                const unsigned char continuation = static_cast<unsigned char>(str[i + n]);
                if ((continuation & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
                codepoint = (codepoint << 6) | (continuation & 0x3F);
            }
            i += continuations + 1;
            if (!valid || codepoint > 0x10FFFF) {
                continue;
            }
            if constexpr (sizeof(wchar_t) >= 4) {
                out.push_back(static_cast<wchar_t>(codepoint));
            }
            else if (codepoint > 0xFFFF) {
                codepoint -= 0x10000;
                out.push_back(static_cast<wchar_t>(0xD800 + (codepoint >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF)));
            }
            else {
                out.push_back(static_cast<wchar_t>(codepoint));
            }
        }
        return out;
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
        std::ranges::transform(s, s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    }

    // ASCII case-insensitive character comparison, the one fold the helpers below share.
    inline bool EqualsFolded(char lhs, char rhs) {
        return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
    }

    // ASCII case-insensitive equality, without allocating a lowered copy of either side.
    inline bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) {
        return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin(), EqualsFolded);
    }

    inline bool StartsWithIgnoreCase(std::string_view value, std::string_view prefix) {
        return value.size() >= prefix.size() && EqualsIgnoreCase(value.substr(0, prefix.size()), prefix);
    }

    // ASCII case-insensitive substring test; an empty needle matches everything (std::search misses it in an empty haystack).
    inline bool ContainsIgnoreCase(std::string_view haystack, std::string_view needle) {
        return needle.empty() || std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), EqualsFolded) != haystack.end();
    }

    // File name (portion after the last / or \) of a possibly-relative path.
    inline std::string FileName(std::string_view path) {
        const auto pos = path.find_last_of("/\\");
        return std::string(pos == std::string_view::npos ? path : path.substr(pos + 1));
    }

    // Standard base64 (RFC 4648, '+'/'/' alphabet, '=' padded).
    inline std::string Base64Encode(const std::string &in) {
        static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        unsigned val = 0;
        int bits     = -6;
        for (unsigned char c : in) {
            val = (val << 8) + c;
            bits += 8;
            while (bits >= 0) {
                out.push_back(T[(val >> bits) & 0x3F]);
                bits -= 6;
            }
        }
        if (bits > -6) {
            out.push_back(T[((val << 8) >> (bits + 8)) & 0x3F]);
        }
        while (out.size() % 4) {
            out.push_back('=');
        }
        return out;
    }
} // namespace Framework::Utils::StringUtils
