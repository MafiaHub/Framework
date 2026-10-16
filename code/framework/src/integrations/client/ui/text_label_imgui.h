/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <external/imgui/widgets/world_text_layout.h>
#include <external/imgui/widgets/world_text_markup.h>
#include <networking/replication/entities/text_label_entity.h>

#include <algorithm>
#include <vector>

namespace Framework::Integrations::Client::UI::TextLabels {
    // A replicated TextLabelEntity in the terms of the world text widgets. The game resolves where the
    // label is and whether it can be seen; this is only what it looks like.

    static_assert(static_cast<int>(::Framework::External::ImGUI::Widgets::WorldTextAlign::Right) == static_cast<int>(::Framework::Networking::Replication::Entities::TextLabelAlign::Right));
    static_assert(static_cast<int>(::Framework::External::ImGUI::Widgets::WorldTextAnchor::Top) == static_cast<int>(::Framework::Networking::Replication::Entities::TextLabelAnchor::Top));
    static_assert(static_cast<int>(::Framework::External::ImGUI::Widgets::WorldTextAnchor::Bottom) == static_cast<int>(::Framework::Networking::Replication::Entities::TextLabelAnchor::Bottom));
    static_assert(::Framework::Networking::Replication::Entities::kTextLabelMaxOutlineWidth <= ::Framework::External::ImGUI::Widgets::kWorldTextMaxOutline, "the widget would cut a label's outline short");
    static_assert(::Framework::Networking::Replication::Entities::kTextLabelMaxShadowBlur <= ::Framework::External::ImGUI::Widgets::kWorldTextMaxBlur, "the widget would cut a label's blur short");

    inline ::Framework::External::ImGUI::Widgets::WorldTextLook Look(const ::Framework::Networking::Replication::Entities::TextLabelEntity &label) {
        ::Framework::External::ImGUI::Widgets::WorldTextLook look;
        look.color             = label.color;
        look.outlineWidth      = std::clamp(label.outlineWidth, 0.0f, ::Framework::Networking::Replication::Entities::kTextLabelMaxOutlineWidth);
        look.outlineColor      = label.outlineColor;
        look.shadowX           = label.shadowOffsetX;
        look.shadowY           = label.shadowOffsetY;
        look.shadowBlur        = std::clamp(label.shadowBlur, 0.0f, ::Framework::Networking::Replication::Entities::kTextLabelMaxShadowBlur);
        look.shadowColor       = label.shadowColor;
        look.backgroundColor   = label.backgroundColor;
        look.backgroundPadding = label.backgroundPadding;
        look.backgroundRadius  = label.backgroundRadius;
        look.anchor            = static_cast<::Framework::External::ImGUI::Widgets::WorldTextAnchor>(label.anchor);
        return look;
    }

    inline ::Framework::External::ImGUI::Widgets::WorldTextAlign Align(const ::Framework::Networking::Replication::Entities::TextLabelEntity &label) {
        return static_cast<::Framework::External::ImGUI::Widgets::WorldTextAlign>(label.align);
    }

    // Markup is parsed only when the label asks for it, so plain text never loses a brace.
    inline std::vector<::Framework::External::ImGUI::Widgets::WorldTextRun> Runs(const ::Framework::Networking::Replication::Entities::TextLabelEntity &label) {
        if (label.markup) {
            return ::Framework::External::ImGUI::Widgets::ParseWorldTextMarkup(label.text);
        }
        return {::Framework::External::ImGUI::Widgets::WorldTextRun {label.text}};
    }
} // namespace Framework::Integrations::Client::UI::TextLabels
