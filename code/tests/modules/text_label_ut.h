/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/replication/entities/text_label_entity.h"
#include "networking/replication/network_entity.h"

#include <mafianet/bit_stream.h>

#include <string>

MODULE(text_label, {
    using namespace Framework::Networking::Replication;
    using namespace Framework::Networking::Replication::Entities;

    IT("the full look survives a round trip on the wire", {
        TextLabelEntity sent;
        sent.position          = glm::vec3(1.0f, -2.0f, 3.5f);
        sent.text              = "Gryffindor\n{#ff0000}Prefect";
        sent.color             = 0x80FF0000;
        sent.fontSize          = 42.0f;
        sent.drawDistance      = 2500.0f;
        sent.fadeDistance      = 1800.0f;
        sent.attachNetworkId   = 77;
        sent.attachOffset      = glm::vec3(0.0f, 0.0f, 25.0f);
        sent.markup            = true;
        sent.sizeMode          = TextLabelSizeMode::World;
        sent.minSize           = 8.0f;
        sent.maxSize           = 96.0f;
        sent.opacity           = 0.5f;
        sent.outlineWidth      = 2.5f;
        sent.outlineColor      = 0xFF102030;
        sent.shadowColor       = 0xC0000000;
        sent.shadowOffsetX     = 3.0f;
        sent.shadowOffsetY     = -2.0f;
        sent.shadowBlur        = 6.0f;
        sent.backgroundColor   = 0xA0203040;
        sent.backgroundPadding = 9.0f;
        sent.backgroundRadius  = 7.0f;
        sent.align             = TextLabelAlign::Right;
        sent.anchor            = TextLabelAnchor::Bottom;
        sent.lineSpacing       = 1.5f;
        sent.maxWidth          = 240.0f;
        sent.occlusion         = false;
        sent.style             = NearestTextLabelStylePreset(sent);

        MafiaNet::BitStream bs;
        FieldSerializer writer(&bs, true);
        sent.SerializeFields(writer);
        EQUALS(writer.Good(), true);

        TextLabelEntity got;
        FieldSerializer reader(&bs, false);
        got.SerializeFields(reader);
        EQUALS(reader.Good(), true);

        EQUALS((got.position == sent.position), true);
        EQUALS((got.text == sent.text), true);
        UEQUALS(got.color, sent.color);
        EQUALS((got.fontSize == sent.fontSize), true);
        EQUALS((got.drawDistance == sent.drawDistance && got.fadeDistance == sent.fadeDistance), true);
        UEQUALS(got.attachNetworkId, sent.attachNetworkId);
        EQUALS((got.attachOffset == sent.attachOffset), true);
        EQUALS(got.markup, true);
        EQUALS((got.sizeMode == TextLabelSizeMode::World), true);
        EQUALS((got.minSize == 8.0f && got.maxSize == 96.0f && got.opacity == 0.5f), true);
        EQUALS((got.outlineWidth == 2.5f), true);
        UEQUALS(got.outlineColor, sent.outlineColor);
        UEQUALS(got.shadowColor, sent.shadowColor);
        EQUALS((got.shadowOffsetX == 3.0f && got.shadowOffsetY == -2.0f && got.shadowBlur == 6.0f), true);
        UEQUALS(got.backgroundColor, sent.backgroundColor);
        EQUALS((got.backgroundPadding == 9.0f && got.backgroundRadius == 7.0f), true);
        EQUALS((got.align == TextLabelAlign::Right && got.anchor == TextLabelAnchor::Bottom), true);
        EQUALS((got.lineSpacing == 1.5f && got.maxWidth == 240.0f), true);
        EQUALS(got.occlusion, false);
        EQUALS((got.style == TextLabelStyle::Box), true);
    });

    IT("a preset sets the parts DrawWorldText draws for it and turns the rest off", {
        TextLabelEntity label;
        label.backgroundColor = 0xFF00FF00;
        label.shadowBlur      = 5.0f;

        ApplyTextLabelStylePreset(label, TextLabelStyle::Outline);
        EQUALS((label.style == TextLabelStyle::Outline), true);
        EQUALS((label.outlineWidth == 1.0f), true);
        UEQUALS(label.outlineColor, 0xFF000000u);
        UEQUALS(label.shadowColor, 0u);
        UEQUALS(label.backgroundColor, 0u);
        EQUALS((label.shadowBlur == 0.0f), true);

        ApplyTextLabelStylePreset(label, TextLabelStyle::Box);
        UEQUALS(label.backgroundColor, 0x99000000u);
        EQUALS((label.backgroundPadding == 4.0f && label.backgroundRadius == 4.0f), true);
        EQUALS((label.outlineWidth == 0.0f), true);

        ApplyTextLabelStylePreset(label, TextLabelStyle::Shadow);
        UEQUALS(label.shadowColor, 0xFF000000u);
        EQUALS((label.shadowOffsetX == 1.0f && label.shadowOffsetY == 1.0f), true);
        UEQUALS(label.backgroundColor, 0u);

        ApplyTextLabelStylePreset(label, TextLabelStyle::None);
        EQUALS((NearestTextLabelStylePreset(label) == TextLabelStyle::None), true);
    });

    IT("the nearest preset prefers a plate, then an outline, then a shadow", {
        TextLabelEntity label;
        ApplyTextLabelStylePreset(label, TextLabelStyle::None);
        label.shadowColor = 0x80000000;
        EQUALS((NearestTextLabelStylePreset(label) == TextLabelStyle::Shadow), true);
        label.outlineWidth = 2.0f;
        EQUALS((NearestTextLabelStylePreset(label) == TextLabelStyle::Outline), true);
        label.outlineColor = 0x00000000; // invisible outline
        EQUALS((NearestTextLabelStylePreset(label) == TextLabelStyle::Shadow), true);
        label.backgroundColor = 0x40FFFFFF;
        EQUALS((NearestTextLabelStylePreset(label) == TextLabelStyle::Box), true);
    });

    IT("a new label's look matches its default Shadow preset", {
        TextLabelEntity label;
        EQUALS((label.style == TextLabelStyle::Shadow), true);
        EQUALS((NearestTextLabelStylePreset(label) == TextLabelStyle::Shadow), true);
    });
});
