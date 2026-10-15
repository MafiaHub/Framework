/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "external/imgui/widgets/world_text_markup.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace WorldTextTest {
    namespace W = Framework::External::ImGUI::Widgets;
} // namespace WorldTextTest

MODULE(world_text, {
    using namespace WorldTextTest;

    IT("markup without tags is one plain run", {
        const auto runs = W::ParseWorldTextMarkup("Gryffindor");
        UEQUALS(runs.size(), 1u);
        EQUALS((runs[0].text == "Gryffindor"), true);
        EQUALS((!runs[0].color.has_value()), true);
        EQUALS((!runs[0].bold && !runs[0].italic), true);
    });

    IT("markup colour tags set and reset the colour", {
        const auto runs = W::ParseWorldTextMarkup("{#ff0000}Red{#}Base{#00ff0080}Half");
        UEQUALS(runs.size(), 3u);
        UEQUALS(*runs[0].color, 0xFFFF0000u);
        EQUALS((!runs[1].color.has_value()), true);
        // CSS order RRGGBBAA arrives as 0xAARRGGBB.
        UEQUALS(*runs[2].color, 0x8000FF00u);
    });

    IT("markup bold and italic toggle independently", {
        const auto runs = W::ParseWorldTextMarkup("{b}B{i}BI{/b}I{/i}N");
        UEQUALS(runs.size(), 4u);
        EQUALS((runs[0].bold && !runs[0].italic), true);
        EQUALS((runs[1].bold && runs[1].italic), true);
        EQUALS((!runs[2].bold && runs[2].italic), true);
        EQUALS((!runs[3].bold && !runs[3].italic), true);
    });

    IT("markup keeps escaped, unknown and unterminated braces as text", {
        const auto runs = W::ParseWorldTextMarkup("{{a} {name} {#12} {open");
        UEQUALS(runs.size(), 1u);
        EQUALS((runs[0].text == "{a} {name} {#12} {open"), true);
    });

    IT("hex colours need their leading #", {
        EQUALS((!W::Detail::ParseHexColor("1234567").has_value()), true);
        EQUALS((!W::Detail::ParseHexColor("x00ff00").has_value()), true);
        UEQUALS(*W::Detail::ParseHexColor("#00ff00"), 0xFF00FF00u);
    });

    IT("utf8 decodes every sequence length", {
        const std::string text = "A\xC3\xAD\xE6\x97\xA5\xF0\x9F\x98\x80";
        size_t i               = 0;
        const uint32_t a       = W::DecodeUtf8(text, i);
        const uint32_t b       = W::DecodeUtf8(text, i);
        const uint32_t c       = W::DecodeUtf8(text, i);
        const uint32_t d       = W::DecodeUtf8(text, i);
        UEQUALS(a, 0x41u);
        UEQUALS(b, 0xEDu);
        UEQUALS(c, 0x65E5u);
        UEQUALS(d, 0x1F600u);
        UEQUALS(i, text.size());
    });

    IT("utf8 replaces malformed input one byte at a time", {
        const std::string truncated = "\xE6\x97";
        size_t i                    = 0;
        const uint32_t cut          = W::DecodeUtf8(truncated, i);
        UEQUALS(cut, 0xFFFDu);
        UEQUALS(i, 1u);

        const std::string overlong = "\xC0\x80";
        i                          = 0;
        const uint32_t padded      = W::DecodeUtf8(overlong, i);
        UEQUALS(padded, 0xFFFDu);

        const std::string surrogate = "\xED\xA0\x80";
        i                           = 0;
        const uint32_t half         = W::DecodeUtf8(surrogate, i);
        UEQUALS(half, 0xFFFDu);
    });
});
