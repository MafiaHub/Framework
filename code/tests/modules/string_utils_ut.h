/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/string_utils.h"

#include <string>

MODULE(string_utils, {
    IT("Utf8ToWide passes ASCII through unchanged", {
        const std::wstring out = Framework::Utils::StringUtils::Utf8ToWide("Police Station");
        UEQUALS(out.size(), 14u);
        UEQUALS(static_cast<unsigned>(out[0]), 'P');
        UEQUALS(static_cast<unsigned>(out[13]), 'n');
    });

    IT("Utf8ToWide decodes a two-byte sequence to one code unit", {
        // "ní" - U+00ED arrives as C3 AD and must not become two characters.
        const std::wstring out = Framework::Utils::StringUtils::Utf8ToWide("n\xC3\xAD");
        UEQUALS(out.size(), 2u);
        UEQUALS(static_cast<unsigned>(out[0]), 'n');
        UEQUALS(static_cast<unsigned>(out[1]), 0x00EDu);
    });

    IT("Utf8ToWide decodes Cyrillic", {
        // "Привет"
        const std::wstring out = Framework::Utils::StringUtils::Utf8ToWide("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
        UEQUALS(out.size(), 6u);
        UEQUALS(static_cast<unsigned>(out[0]), 0x041Fu);
        UEQUALS(static_cast<unsigned>(out[5]), 0x0442u);
    });

    IT("Utf8ToWide decodes a three-byte sequence", {
        // "日本"
        const std::wstring out = Framework::Utils::StringUtils::Utf8ToWide("\xE6\x97\xA5\xE6\x9C\xAC");
        UEQUALS(out.size(), 2u);
        UEQUALS(static_cast<unsigned>(out[0]), 0x65E5u);
        UEQUALS(static_cast<unsigned>(out[1]), 0x672Cu);
    });

    IT("Utf8ToWide handles a four-byte sequence", {
        // U+1F600, a surrogate pair where wchar_t is 16-bit.
        const std::wstring out = Framework::Utils::StringUtils::Utf8ToWide("\xF0\x9F\x98\x80");
        if (sizeof(wchar_t) >= 4) {
            UEQUALS(out.size(), 1u);
            UEQUALS(static_cast<unsigned>(out[0]), 0x1F600u);
        }
        else {
            UEQUALS(out.size(), 2u);
            UEQUALS(static_cast<unsigned>(out[0]), 0xD83Du);
            UEQUALS(static_cast<unsigned>(out[1]), 0xDE00u);
        }
    });

    IT("Utf8ToWide drops invalid and truncated sequences", {
        UEQUALS(Framework::Utils::StringUtils::Utf8ToWide("").size(), 0u);
        // Lone continuation byte.
        UEQUALS(Framework::Utils::StringUtils::Utf8ToWide("\x80").size(), 0u);
        // Lead byte announcing more bytes than remain.
        UEQUALS(Framework::Utils::StringUtils::Utf8ToWide("a\xE6\x97").size(), 1u);
        // Continuation byte missing its high bits.
        UEQUALS(Framework::Utils::StringUtils::Utf8ToWide("\xC3\x41").size(), 0u);
    });

    IT("NormalToWide stays a byte widen, so UTF-8 callers must not use it", {
        const std::wstring out = Framework::Utils::StringUtils::NormalToWide("n\xC3\xAD");
        UEQUALS(out.size(), 3u);
    });
});
