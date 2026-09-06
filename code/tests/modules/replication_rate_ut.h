/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/channels.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"

#include <mafianet/BitStream.h>
#include <mafianet/ReplicaManager3.h>

// Transform channel rate rules: distance bands, idle refresh, and per-entity ordering.
MODULE(replication_rate, {
    using Framework::Networking::Channel;
    using Framework::Networking::ToOrderingChannel;
    using Framework::Networking::Replication::FieldSerializer;
    using Framework::Networking::Replication::NetworkEntity;
    using Framework::Networking::Replication::ReplicationManager;
    using Framework::Networking::Replication::SerializeRateBands;

    // Drives Serialize like ReplicaManager3 does for a broadcast-identical replica.
    struct SerializeHarness {
        NetworkEntity entity;
        MafiaNet::BitStream lastSent;
        bool first = true;

        int Serialize(MafiaNet::Time now) {
            MafiaNet::SerializeParameters sp;
            for (int i = 0; i < MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS; ++i) {
                sp.lastSentBitstream[i] = nullptr;
            }
            sp.lastSentBitstream[0] = &lastSent;
            sp.whenLastSerialized   = first ? 0 : 1;
            sp.curTime              = now;
            first                   = false;
            entity.OnUserReplicaPreSerializeTick();
            const int result = static_cast<int>(entity.Serialize(&sp));
            lastSent.Reset();
            lastSent.Write(&sp.outputBitstream[0]);
            return result;
        }
    };

    const int normal = static_cast<int>(MafiaNet::RM3SR_BROADCAST_IDENTICALLY);
    const int forced = static_cast<int>(MafiaNet::RM3SR_BROADCAST_IDENTICALLY_FORCE_SERIALIZATION);

    IT("keeps the transform stream on a channel of its own", {
        const char transform = ToOrderingChannel(Channel::Transform);
        NEQUALS(transform, ToOrderingChannel(Channel::State));
        NEQUALS(transform, ToOrderingChannel(Channel::Assets));
        NEQUALS(transform, ToOrderingChannel(Channel::Events));
        NEQUALS(transform, ToOrderingChannel(Channel::Construction));
        NEQUALS(transform, ToOrderingChannel(Channel::VoiceFrames));
        NEQUALS(transform, ToOrderingChannel(Channel::VoiceControl));
    });

    IT("orders entity construction with the RPCs that name entities", {
        EQUALS(ToOrderingChannel(Channel::Construction), ToOrderingChannel(Channel::Events));
    });

    IT("sends every tick when no band is configured", {
        const SerializeRateBands off {};
        EQUALS(static_cast<int>(ReplicationManager::TransformSendIntervalMs(off, 0.0f)), 0);
        EQUALS(static_cast<int>(ReplicationManager::TransformSendIntervalMs(off, 1000.0f * 1000.0f)), 0);
    });

    IT("picks the interval by the band the distance falls in", {
        const SerializeRateBands bands {50.0f, 150.0f, 33, 66};
        EQUALS(static_cast<int>(ReplicationManager::TransformSendIntervalMs(bands, 10.0f * 10.0f)), 0);
        EQUALS(static_cast<int>(ReplicationManager::TransformSendIntervalMs(bands, 100.0f * 100.0f)), 33);
        EQUALS(static_cast<int>(ReplicationManager::TransformSendIntervalMs(bands, 300.0f * 300.0f)), 66);
    });

    IT("treats a band edge as inside the nearer band", {
        const SerializeRateBands bands {50.0f, 150.0f, 33, 66};
        EQUALS(static_cast<int>(ReplicationManager::TransformSendIntervalMs(bands, 50.0f * 50.0f)), 0);
        EQUALS(static_cast<int>(ReplicationManager::TransformSendIntervalMs(bands, 50.5f * 50.5f)), 33);
        EQUALS(static_cast<int>(ReplicationManager::TransformSendIntervalMs(bands, 150.0f * 150.0f)), 33);
        EQUALS(static_cast<int>(ReplicationManager::TransformSendIntervalMs(bands, 150.5f * 150.5f)), 66);
    });

    IT("lets a type override the default bands and falls back for the rest", {
        ReplicationManager manager;
        manager.SetSerializeRateBands(SerializeRateBands {50.0f, 150.0f, 33, 50});
        manager.SetSerializeRateBands(42u, SerializeRateBands {50.0f, 150.0f, 50, 100});
        EQUALS(static_cast<int>(manager.GetSerializeRateBands(42u).farIntervalMs), 100);
        EQUALS(static_cast<int>(manager.GetSerializeRateBands(7u).farIntervalMs), 50);
    });

    IT("never refreshes an entity that has not moved since construction", {
        SerializeHarness h;
        h.entity.position = glm::vec3(1.0f, 2.0f, 3.0f);
        EQUALS(h.Serialize(1000), normal);
        EQUALS(h.Serialize(1000 + NetworkEntity::kTransformRefreshMs), normal);
        EQUALS(h.Serialize(1000 + NetworkEntity::kTransformHeartbeatMs), normal);
        EQUALS(h.Serialize(1000 + 10 * NetworkEntity::kTransformHeartbeatMs), normal);
    });

    IT("refreshes a stopped entity through the burst, then on the heartbeat", {
        SerializeHarness h;
        const MafiaNet::Time refresh   = NetworkEntity::kTransformRefreshMs;
        const MafiaNet::Time burst     = NetworkEntity::kTransformRefreshBurstMs;
        const MafiaNet::Time heartbeat = NetworkEntity::kTransformHeartbeatMs;

        EQUALS(h.Serialize(1000), normal);
        h.entity.position = glm::vec3(5.0f, 0.0f, 0.0f);
        EQUALS(h.Serialize(2000), normal);
        EQUALS(h.Serialize(2000 + refresh - 1), normal);
        EQUALS(h.Serialize(2000 + refresh), forced);
        EQUALS(h.Serialize(2000 + refresh + 1), normal);
        EQUALS(h.Serialize(2000 + 2 * refresh), forced);
        EQUALS(h.Serialize(2000 + 3 * refresh), forced);
        EQUALS(h.Serialize(2000 + burst), normal);
        EQUALS(h.Serialize(2000 + 3 * refresh + heartbeat - 1), normal);
        EQUALS(h.Serialize(2000 + 3 * refresh + heartbeat), forced);
    });

    IT("restarts the burst when the entity moves again", {
        SerializeHarness h;
        const MafiaNet::Time refresh = NetworkEntity::kTransformRefreshMs;
        const MafiaNet::Time burst   = NetworkEntity::kTransformRefreshBurstMs;

        EQUALS(h.Serialize(1000), normal);
        h.entity.position = glm::vec3(5.0f, 0.0f, 0.0f);
        EQUALS(h.Serialize(2000), normal);
        EQUALS(h.Serialize(2000 + burst), normal);
        h.entity.position = glm::vec3(9.0f, 0.0f, 0.0f);
        EQUALS(h.Serialize(10000), normal);
        EQUALS(h.Serialize(10000 + refresh), forced);
    });

    const auto deliver = [](NetworkEntity &target, const glm::vec3 &position, MafiaNet::Time sentAt) {
        NetworkEntity source;
        source.position = position;
        MafiaNet::DeserializeParameters dp;
        for (int i = 0; i < MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS; ++i) {
            dp.bitstreamWrittenTo[i] = false;
        }
        dp.bitstreamWrittenTo[0] = true;
        dp.timeStamp             = sentAt;
        dp.sourceConnection      = nullptr;
        const uint8_t epoch      = 0;
        dp.serializationBitstream[0].Write(epoch);
        FieldSerializer fields(&dp.serializationBitstream[0], true);
        source.SerializeTransform(fields);
        dp.serializationBitstream[0].ResetReadPointer();
        target.Deserialize(&dp);
    };

    IT("applies poses in send order and drops one that arrives after a newer one", {
        NetworkEntity target;
        deliver(target, glm::vec3(1.0f, 0.0f, 0.0f), 200);
        EQUALS(static_cast<int>(target.position.x), 1);
        deliver(target, glm::vec3(2.0f, 0.0f, 0.0f), 100);
        EQUALS(static_cast<int>(target.position.x), 1);
        deliver(target, glm::vec3(3.0f, 0.0f, 0.0f), 300);
        EQUALS(static_cast<int>(target.position.x), 3);
    });

    IT("applies an unstamped pose rather than guessing its age", {
        NetworkEntity target;
        deliver(target, glm::vec3(1.0f, 0.0f, 0.0f), 200);
        deliver(target, glm::vec3(4.0f, 0.0f, 0.0f), 0);
        EQUALS(static_cast<int>(target.position.x), 4);
    });
});
