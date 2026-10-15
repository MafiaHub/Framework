/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// Windows only: ImGui comes from a Windows-only vcpkg port.

#include "external/imgui/widgets/world_text_layout.h"

#include <imgui.h>

#include <cmath>
#include <string>
#include <vector>

namespace WorldTextLayoutTest {
    namespace W = Framework::External::ImGUI::Widgets;

    // A headless frame: layout needs a current font and baked sizes, never a renderer.
    struct Frame {
        ImGuiContext *context = nullptr;
        Frame() {
            context        = ImGui::CreateContext();
            ImGuiIO &io    = ImGui::GetIO();
            io.DisplaySize = ImVec2(1280.0f, 720.0f);
            io.DeltaTime   = 1.0f / 60.0f;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.Fonts->AddFontDefault();
            ImGui::NewFrame();
        }
        ~Frame() {
            ImGui::EndFrame();
            ImGui::DestroyContext(context);
        }
    };

    inline std::vector<W::WorldTextRun> Plain(const char *text) {
        return {W::WorldTextRun {text}};
    }

    inline int LineCount(const W::WorldTextLaidOut &layout, float lineHeight) {
        return static_cast<int>(std::lround((layout.height - layout.bakeSize) / lineHeight)) + 1;
    }
} // namespace WorldTextLayoutTest

MODULE(world_text_layout, {
    using namespace WorldTextLayoutTest;

    IT("bake sizes are exact up to 24 px, then a capped ladder", {
        UEQUALS(W::WorldTextBakeSize(0.0f), 0.0f);
        UEQUALS(W::WorldTextBakeSize(17.4f), 17.0f);
        UEQUALS(W::WorldTextBakeSize(24.0f), 24.0f);
        float previous = 24.0f;
        int distinct   = 0;
        for (float size = 24.5f; size < 400.0f; size += 0.5f) {
            const float bake = W::WorldTextBakeSize(size);
            EQUALS((bake >= previous), true);
            EQUALS((bake <= 256.0f), true);
            if (size <= 256.0f) {
                EQUALS((std::abs(bake - size) / size < 0.06f), true);
            }
            distinct += bake != previous ? 1 : 0;
            previous = bake;
        }
        EQUALS((distinct < 40), true);
    });

    IT("layout breaks lines at newlines", {
        Frame frame;
        const auto one = W::LayoutWorldText(Plain("Hogwarts"), {}, 20.0f, 0.0f, 1.0f, W::WorldTextAlign::Center);
        const auto two = W::LayoutWorldText(Plain("Hogwarts\nYear 5"), {}, 20.0f, 0.0f, 1.5f, W::WorldTextAlign::Center);
        UEQUALS(one.height, 20.0f);
        EQUALS((one.width > 0.0f), true);
        UEQUALS(two.height, 20.0f + 30.0f);
        UEQUALS(one.glyphs.size(), 8u);
        UEQUALS(two.glyphs.size(), 14u);
    });

    IT("layout wraps after spaces and never counts trailing ones", {
        Frame frame;
        const auto full    = W::LayoutWorldText(Plain("aaa bbb"), {}, 20.0f, 0.0f, 1.0f, W::WorldTextAlign::Left);
        const auto word    = W::LayoutWorldText(Plain("aaa"), {}, 20.0f, 0.0f, 1.0f, W::WorldTextAlign::Left);
        const auto wrapped = W::LayoutWorldText(Plain("aaa bbb ccc"), {}, 20.0f, full.width + 1.0f, 1.0f, W::WorldTextAlign::Left);
        UEQUALS(LineCount(wrapped, 20.0f), 2);
        UEQUALS(wrapped.width, full.width);
        // "ccc" starts its own line at the left edge.
        UEQUALS(wrapped.glyphs.back().x + wrapped.glyphs.back().advance, word.width);
        UEQUALS(wrapped.glyphs.back().y, 20.0f);

        const auto trailing = W::LayoutWorldText(Plain("aaa   "), {}, 20.0f, 0.0f, 1.0f, W::WorldTextAlign::Left);
        UEQUALS(trailing.width, word.width);
    });

    IT("layout splits a word wider than the line", {
        Frame frame;
        const auto word    = W::LayoutWorldText(Plain("abcdefgh"), {}, 20.0f, 0.0f, 1.0f, W::WorldTextAlign::Left);
        const auto wrapped = W::LayoutWorldText(Plain("abcdefgh"), {}, 20.0f, word.width * 0.5f, 1.0f, W::WorldTextAlign::Left);
        EQUALS((LineCount(wrapped, 20.0f) >= 2), true);
        EQUALS((wrapped.width <= word.width * 0.5f + 0.01f), true);
    });

    IT("layout aligns shorter lines", {
        Frame frame;
        const auto left  = W::LayoutWorldText(Plain("long line\nab"), {}, 20.0f, 0.0f, 1.0f, W::WorldTextAlign::Left);
        const auto right = W::LayoutWorldText(Plain("long line\nab"), {}, 20.0f, 0.0f, 1.0f, W::WorldTextAlign::Right);
        const auto &b    = right.glyphs.back();
        UEQUALS(left.glyphs.back().y, 20.0f);
        EQUALS((left.glyphs[left.glyphs.size() - 2].x == 0.0f), true);
        UEQUALS(b.x + b.advance, right.width);
    });

    IT("faux bold widens and markup colours reach their spans", {
        Frame frame;
        const auto plain = W::LayoutWorldText(Plain("Name"), {}, 24.0f, 0.0f, 1.0f, W::WorldTextAlign::Center);
        const auto bold  = W::LayoutWorldText(W::ParseWorldTextMarkup("{b}Name"), {}, 24.0f, 0.0f, 1.0f, W::WorldTextAlign::Center);
        EQUALS((bold.width > plain.width), true);
        EQUALS((bold.spans[0].fauxBold), true);

        const auto coloured = W::LayoutWorldText(W::ParseWorldTextMarkup("{#ff0000}Red{#} base"), {}, 24.0f, 0.0f, 1.0f, W::WorldTextAlign::Center);
        UEQUALS(coloured.spans.size(), 2u);
        UEQUALS(*coloured.spans[coloured.glyphs.front().span].color, 0xFFFF0000u);
        EQUALS((!coloured.spans[coloured.glyphs.back().span].color.has_value()), true);
    });
});
