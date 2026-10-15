/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::External::ImGUI::Widgets {
    // A stretch of text sharing one inline style. color is 0xAARRGGBB; unset follows the label's own.
    struct WorldTextRun {
        std::string text;
        std::optional<uint32_t> color;
        bool bold   = false;
        bool italic = false;
    };

    namespace Detail {
        inline int HexDigit(char c) {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }
            return -1;
        }

        // "#RRGGBB" or "#RRGGBBAA" (CSS order) to 0xAARRGGBB.
        inline std::optional<uint32_t> ParseHexColor(std::string_view tag) {
            if (tag.size() != 7 && tag.size() != 9) {
                return std::nullopt;
            }
            uint32_t value = 0;
            for (size_t i = 1; i < tag.size(); ++i) {
                const int digit = HexDigit(tag[i]);
                if (digit < 0) {
                    return std::nullopt;
                }
                value = (value << 4) | static_cast<uint32_t>(digit);
            }
            if (tag.size() == 7) {
                return 0xFF000000u | value;
            }
            return (value >> 8) | ((value & 0xFFu) << 24);
        }
    } // namespace Detail

    // Reads one code point at `i` and advances past it; malformed input yields U+FFFD one byte on.
    inline uint32_t DecodeUtf8(std::string_view text, size_t &i) {
        const auto byte = [&](size_t at) {
            return static_cast<uint8_t>(text[at]);
        };
        const uint8_t lead = byte(i);
        size_t length      = 0;
        uint32_t cp        = 0;
        if (lead < 0x80) {
            ++i;
            return lead;
        }
        if ((lead & 0xE0) == 0xC0) {
            length = 2;
            cp     = lead & 0x1F;
        }
        else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            cp     = lead & 0x0F;
        }
        else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            cp     = lead & 0x07;
        }
        if (length == 0 || i + length > text.size()) {
            ++i;
            return 0xFFFD;
        }
        for (size_t k = 1; k < length; ++k) {
            if ((byte(i + k) & 0xC0) != 0x80) {
                ++i;
                return 0xFFFD;
            }
            cp = (cp << 6) | (byte(i + k) & 0x3F);
        }
        static constexpr uint32_t kMinimum[] {0, 0, 0x80, 0x800, 0x10000};
        i += length;
        if (cp < kMinimum[length] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return 0xFFFD;
        }
        return cp;
    }

    // Tags: {#RRGGBB} / {#RRGGBBAA} set the colour, {#} restores the label's, {b}{/b} and {i}{/i}
    // toggle bold and italic, {{ is a literal brace. Anything else in braces stays as text.
    inline std::vector<WorldTextRun> ParseWorldTextMarkup(std::string_view text) {
        std::vector<WorldTextRun> runs;
        WorldTextRun current;
        const auto flush = [&]() {
            if (!current.text.empty()) {
                runs.push_back(current);
                current.text.clear();
            }
        };

        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c != '{') {
                current.text.push_back(c);
                continue;
            }
            if (i + 1 < text.size() && text[i + 1] == '{') {
                current.text.push_back('{');
                ++i;
                continue;
            }
            const size_t close = text.find('}', i + 1);
            if (close == std::string_view::npos) {
                current.text.push_back(c);
                continue;
            }

            const std::string_view tag = text.substr(i + 1, close - i - 1);
            std::optional<uint32_t> color;
            bool known = true;
            if (tag == "#") {
                flush();
                current.color.reset();
            }
            else if (!tag.empty() && tag[0] == '#' && (color = Detail::ParseHexColor(tag))) {
                flush();
                current.color = color;
            }
            else if (tag == "b" || tag == "/b") {
                flush();
                current.bold = tag == "b";
            }
            else if (tag == "i" || tag == "/i") {
                flush();
                current.italic = tag == "i";
            }
            else {
                known = false;
            }

            if (known) {
                i = close;
            }
            else {
                current.text.push_back(c);
            }
        }
        flush();
        return runs;
    }
} // namespace Framework::External::ImGUI::Widgets
