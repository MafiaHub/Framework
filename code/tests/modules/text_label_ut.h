/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "core_modules.h"
#include "networking/network_server.h"
#include "networking/replication/entities/text_label_entity.h"
#include "networking/replication/entity_registry.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_connection.h"
#include "networking/replication/replication_manager.h"
#include "scripting/builtins/text_label.h"

#include <mafianet/bit_stream.h>

#include <string>

// A server replication manager on an unstarted peer, published to the scripting builtins.
class TextLabelWorld {
  public:
    Framework::Networking::NetworkServer peer;
    uint32_t targetType = 0;

    TextLabelWorld() {
        using namespace Framework::Networking::Replication;
        Manager()->Init(&peer, true);
        Manager()->ConfigureGrid(100, -4096, 4096);
        Manager()->SetInterestUsesEntityRange(Entities::TextLabelEntity::kTypeName, true);
        EntityRegistry::Get().Register<Entities::TextLabelEntity>(Entities::TextLabelEntity::kTypeName);
        targetType = EntityRegistry::Get().Register<NetworkEntity>("Test::LabelTarget");
        Framework::CoreModules::SetReplication(Manager());
    }

    ~TextLabelWorld() {
        Framework::CoreModules::SetReplication(nullptr);
        Manager()->Clear();
    }

    Framework::Networking::Replication::ReplicationManager *Manager() {
        return peer.GetReplicationManager();
    }
};

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
        sent.ownPosition       = glm::vec3(4.0f, 5.0f, 6.0f);
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

        // Attached: the server's position follows the target; peers get the label's own.
        EQUALS((got.position == sent.ownPosition), true);
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

    IT("an attached label's transform carries its own position, not its target's", {
        TextLabelEntity sent;
        sent.position        = glm::vec3(900.0f, 0.0f, 0.0f);
        sent.ownPosition     = glm::vec3(1.0f, 2.0f, 3.0f);
        sent.attachNetworkId = 5;

        MafiaNet::BitStream bs;
        FieldSerializer writer(&bs, true);
        sent.SerializeTransform(writer);
        TextLabelEntity got;
        FieldSerializer reader(&bs, false);
        got.SerializeTransform(reader);
        EQUALS((got.position == sent.ownPosition), true);

        sent.attachNetworkId = 0;
        MafiaNet::BitStream detached;
        FieldSerializer again(&detached, true);
        sent.SerializeTransform(again);
        FieldSerializer readAgain(&detached, false);
        got.SerializeTransform(readAgain);
        EQUALS((got.position == sent.position), true);
    });

    IT("attaching needs another live entity; the label follows it and returns when detached", {
        using Framework::Scripting::Builtins::TextLabel;
        TextLabelWorld world;
        auto *target     = world.Manager()->CreateEntity(world.targetType);
        auto *label      = world.Manager()->CreateEntity<TextLabelEntity>();
        target->position = glm::vec3(50.0f, 0.0f, 0.0f);
        target->SetVirtualWorld(3);
        label->position = glm::vec3(1.0f, 2.0f, 3.0f);
        TextLabel handle(label->GetNetworkID());

        EQUALS(handle.AttachTo(label->GetNetworkID(), glm::vec3(0.0f)), false);
        EQUALS(handle.AttachTo(target->GetNetworkID() + 1000, glm::vec3(0.0f)), false);
        UEQUALS(label->attachNetworkId, 0u);

        EQUALS(handle.AttachTo(target->GetNetworkID(), glm::vec3(0.0f, 0.0f, 10.0f)), true);
        EQUALS((label->position == target->position), true);
        UEQUALS(label->GetVirtualWorld(), 3u);
        EQUALS(label->streaming.alwaysVisible, false);

        target->position = glm::vec3(60.0f, 0.0f, 0.0f);
        target->SetVirtualWorld(4);
        TextLabel::FollowTargets(*world.Manager());
        EQUALS((label->position == target->position), true);
        UEQUALS(label->GetVirtualWorld(), 4u);

        handle.Detach();
        UEQUALS(label->attachNetworkId, 0u);
        EQUALS((label->position == glm::vec3(1.0f, 2.0f, 3.0f)), true);
        UEQUALS(label->GetVirtualWorld(), 0u);
    });

    IT("an attached label is destroyed with its target", {
        using Framework::Scripting::Builtins::TextLabel;
        TextLabelWorld world;
        auto *target       = world.Manager()->CreateEntity(world.targetType);
        auto *label        = world.Manager()->CreateEntity<TextLabelEntity>();
        const auto labelId = label->GetNetworkID();
        TextLabel handle(labelId);
        EQUALS(handle.AttachTo(target->GetNetworkID(), glm::vec3(0.0f)), true);

        world.Manager()->DestroyEntity(target);
        TextLabel::FollowTargets(*world.Manager());
        EQUALS((world.Manager()->GetEntityByNetworkID(labelId) == nullptr), true);
        EQUALS((handle.ResolveLabel() == nullptr), true);
    });

    IT("an attached label streams only to players near its target", {
        using Framework::Scripting::Builtins::TextLabel;
        TextLabelWorld world;
        auto *manager     = world.Manager();
        auto *nearby      = manager->CreateEntity(world.targetType);
        auto *distant     = manager->CreateEntity(world.targetType);
        auto *target      = manager->CreateEntity(world.targetType);
        auto *label       = manager->CreateEntity<TextLabelEntity>();
        distant->position = glm::vec3(2000.0f, 0.0f, 0.0f);
        target->position  = glm::vec3(20.0f, 0.0f, 0.0f);
        label->position   = glm::vec3(-3000.0f, 0.0f, 0.0f);
        TextLabel handle(label->GetNetworkID());
        handle.SetDrawDistance(100.0f);
        EQUALS(handle.AttachTo(target->GetNetworkID(), glm::vec3(0.0f)), true);

        const MafiaNet::RakNetGUID nearbyGuid(91), distantGuid(92);
        manager->SetViewer(MafiaNet::ToPeerGuid(nearbyGuid), nearby);
        manager->SetViewer(MafiaNet::ToPeerGuid(distantGuid), distant);
        manager->RebuildInterest();
        manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, nearbyGuid));
        manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, distantGuid));
        const auto sees = [&](MafiaNet::RakNetGUID guid) {
            auto *connection = static_cast<ReplicationConnection *>(manager->GetConnectionByGUID(guid));
            DataStructures::List<MafiaNet::Replica3 *> create, destroy;
            connection->QueryReplicaList(create, destroy);
            for (unsigned int i = 0; i < create.Size(); ++i) {
                if (create[i] == label) {
                    return true;
                }
            }
            return false;
        };
        EQUALS(sees(nearbyGuid), true);
        EQUALS(sees(distantGuid), false);

        target->position = glm::vec3(2010.0f, 0.0f, 0.0f);
        TextLabel::FollowTargets(*manager);
        manager->RebuildInterest();
        EQUALS(sees(distantGuid), true);
        EQUALS(sees(nearbyGuid), false);
    });

    IT("a label streams only to the players it is visible for, and hideForTarget keeps it from the one it follows", {
        using Framework::Scripting::Builtins::TextLabel;
        TextLabelWorld world;
        auto *manager = world.Manager();
        const MafiaNet::RakNetGUID guids[] {MafiaNet::RakNetGUID(81), MafiaNet::RakNetGUID(82)};
        NetworkEntity *players[2];
        for (int i = 0; i < 2; ++i) {
            players[i]            = manager->CreateEntity(world.targetType);
            players[i]->ownerGUID = MafiaNet::ToPeerGuid(guids[i]);
            manager->SetViewer(players[i]->ownerGUID, players[i]);
        }
        auto *label = manager->CreateEntity<TextLabelEntity>();
        auto *prop  = manager->CreateEntity(world.targetType);
        TextLabel handle(label->GetNetworkID());
        manager->RebuildInterest();
        for (const auto &guid : guids) {
            manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, guid));
        }
        // Routine refreshes are far off and nothing is rebuilt between changes: a filter change alone
        // has to reach the players it concerns, and only them.
        constexpr uint32_t kFarInterval = 1000000;
        manager->SetInterestRebuildInterval(kFarInterval);
        const auto sees = [&](int player, NetworkEntity *entity) {
            auto *connection = static_cast<ReplicationConnection *>(manager->GetConnectionByGUID(guids[player]));
            DataStructures::List<MafiaNet::Replica3 *> create, destroy;
            connection->QueryReplicaList(create, destroy);
            for (unsigned int i = 0; i < create.Size(); ++i) {
                if (create[i] == entity) {
                    return true;
                }
            }
            return false;
        };
        EQUALS(sees(0, label), true);
        EQUALS(sees(1, label), true);
        EQUALS(sees(0, prop), true);

        // Moved out of range without a refresh: only a player who re-queries stops seeing the prop.
        prop->position.x = 2000.0f;
        manager->SetInterestRebuildInterval(0);
        manager->RebuildInterest();
        manager->SetInterestRebuildInterval(kFarInterval);

        handle.SetVisibleFor(players[1]->ownerGUID, false);
        EQUALS(sees(1, label), false);
        EQUALS(sees(1, prop), false);
        EQUALS(sees(0, label), true);
        EQUALS(sees(0, prop), true);
        handle.SetVisibleFor(players[1]->ownerGUID, true);
        EQUALS(sees(1, label), true);
        EQUALS((label->streaming.viewers == nullptr), true);

        EQUALS(handle.AttachTo(players[0]->GetNetworkID(), glm::vec3(0.0f)), true);
        EQUALS(sees(0, label), true);
        handle.SetHideForTarget(true);
        EQUALS(sees(0, label), false);
        EQUALS(sees(1, label), true);
        handle.SetHideForTarget(false);
        EQUALS(sees(0, label), true);
        handle.SetHideForTarget(true);
        EQUALS(handle.AttachTo(players[1]->GetNetworkID(), glm::vec3(0.0f)), true);
        EQUALS(sees(0, label), true);
        EQUALS(sees(1, label), false);
        handle.Detach();
        EQUALS(sees(1, label), true);

        // A player who leaves is dropped from the filter: whoever holds that guid next starts clean.
        handle.SetVisibleFor(players[1]->ownerGUID, false);
        manager->OnClosedConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, guids[1], MafiaNet::LCR_CONNECTION_LOST);
        EQUALS((label->streaming.viewers == nullptr), true);
    });
});
