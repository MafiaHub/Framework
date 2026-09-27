/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_server.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"

#include <mafianet/BitStream.h>
#include <mafianet/ReplicaManager3.h>
#include <mafianet/types.h>

// Server authority over entity destruction. The base ReplicaManager3 destruction dispatch resolves
// the target replica by a NetworkID read straight from the packet body and deletes it the moment
// NetworkEntity::DeserializeDestruction returns true, doing no ownership check of its own. That hook
// used to return true unconditionally, so any connected client could despawn arbitrary entities.
// These tests pin the owner gate directly: DeserializeDestruction's return value IS the decision
// (true -> the base deletes + relays the destruction, false -> the entity is kept), so a return of
// true for a non-owner connection reproduces the vulnerability and a return of false proves the fix.
MODULE(replication_authority, {
    using Framework::Networking::NetworkServer;
    using Framework::Networking::Replication::NetworkEntity;
    using Framework::Networking::Replication::ReplicationManager;

    // Two in-memory peers, never started (no socket bound): one manager put into server mode, one
    // into client mode. NetworkServer is used for both because the tests link Framework +
    // FrameworkServer (NetworkClient lives in FrameworkClient, which they do not link); the server
    // vs client role is decided by ReplicationManager::Init's flag, not by the peer type. Only
    // ReplicationManager::IsServer() is exercised by the gate, and Init() just flips that flag,
    // records the peer's guid, and attaches the plugin — none of which needs a live connection.
    NetworkServer serverPeer;
    NetworkServer clientPeer;
    auto *serverManager = serverPeer.GetReplicationManager();
    auto *clientManager = clientPeer.GetReplicationManager();
    serverManager->Init(&serverPeer, true);
    clientManager->Init(&clientPeer, false);

    // Distinct, stable guids: one owns the entity, one is the "attacker" that owns nothing.
    const MafiaNet::RakNetGUID ownerGuid(1000);
    const MafiaNet::RakNetGUID attackerGuid(2000);
    const MafiaNet::SystemAddress noAddress;

    // Fresh connection objects carrying the chosen guid; GetRakNetGUID() (what the gate reads)
    // returns exactly what we pass here. Allocated through the manager so they are real
    // ReplicationConnection instances, freed at the end of each case.
    MafiaNet::Connection_RM3 *ownerConn    = serverManager->AllocConnection(noAddress, ownerGuid);
    MafiaNet::Connection_RM3 *attackerConn = serverManager->AllocConnection(noAddress, attackerGuid);

    struct ServerLifetimeEntity final: Framework::Networking::Replication::NetworkEntity {
        bool CanOwnerDestroy() const override {
            return false;
        }
    };

    IT("keeps server-owned lifetime even when a client owns the pose", {
        ServerLifetimeEntity entity;
        entity.replicaManager = serverManager;
        entity.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);

        MafiaNet::BitStream bs;
        EQUALS(entity.DeserializeDestruction(&bs, ownerConn), false);
        EQUALS(entity.DeserializeDestruction(&bs, attackerConn), false);
        EQUALS(entity.DeserializeDestruction(&bs, nullptr), false);
    });

    IT("accepts server destruction on a client even when owners cannot destroy", {
        ServerLifetimeEntity entity;
        entity.replicaManager = clientManager;
        entity.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);

        MafiaNet::BitStream bs;
        EQUALS(entity.DeserializeDestruction(&bs, ownerConn), true);
        EQUALS(entity.DeserializeDestruction(&bs, nullptr), true);
    });

    IT("lets the server delete an entity when the request comes from its owner", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;
        entity.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);

        MafiaNet::BitStream bs;
        EQUALS(entity.DeserializeDestruction(&bs, ownerConn), true);
    });

    IT("refuses a client's attempt to destroy an entity it does not own", {
        // The exploit: a non-owner names another entity's NetworkID in a construction packet's
        // destruction sublist. Before the fix this returned true and the server deleted it.
        NetworkEntity entity;
        entity.replicaManager = serverManager;
        entity.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);

        MafiaNet::BitStream bs;
        EQUALS(entity.DeserializeDestruction(&bs, attackerConn), false);
    });

    IT("refuses any client's attempt to destroy a server-owned entity", {
        // A server-owned entity has no client owner, so no client connection may destroy it.
        NetworkEntity entity;
        entity.replicaManager = serverManager;
        entity.ownerGUID      = MafiaNet::UNASSIGNED_PEER_GUID;

        MafiaNet::BitStream bs;
        EQUALS(entity.DeserializeDestruction(&bs, attackerConn), false);
    });

    IT("fails closed when the server sees a destruction with no source connection", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;
        entity.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);

        MafiaNet::BitStream bs;
        EQUALS(entity.DeserializeDestruction(&bs, nullptr), false);
    });

    IT("still accepts the server's authoritative destruction on a client", {
        // On a client every destruction originates from the server and must be honoured, regardless
        // of the entity's owner — otherwise the client could never despawn anything.
        NetworkEntity entity;
        entity.replicaManager = clientManager;
        entity.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);

        MafiaNet::BitStream bs;
        EQUALS(entity.DeserializeDestruction(&bs, attackerConn), true);
    });

    // A forced state always carries the pose, so the owner has to be told whether that pose is a
    // move the server made or its own report echoed back. The server answers from the last pose the
    // owner's transform channel delivered.
    const auto deliverOwnerPose = [&](NetworkEntity &entity, const glm::vec3 &position) {
        MafiaNet::DeserializeParameters params {};
        params.sourceConnection = ownerConn;
        params.timeStamp        = 0;
        params.bitstreamWrittenTo[0] = true;
        params.serializationBitstream[0].Write(entity.stateEpoch);
        glm::vec3 wirePosition = position;
        glm::vec3 wireVelocity(0.0f);
        glm::quat wireRotation = glm::identity<glm::quat>();
        Framework::Networking::Replication::FieldSerializer transform(&params.serializationBitstream[0], true);
        transform.Field(wirePosition);
        transform.Field(wireVelocity);
        transform.Field(wireRotation);
        entity.Deserialize(&params);
    };

    IT("treats a pose no owner has reported as the server's own", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;
        entity.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);
        entity.position       = glm::vec3(1.0f, 2.0f, 3.0f);

        EQUALS(entity.IsPoseServerAuthored(), true);
    });

    IT("reads the owner's own reported pose as an echo, not a move", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;
        entity.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);

        deliverOwnerPose(entity, glm::vec3(10.0f, 20.0f, 30.0f));
        EQUALS(entity.position.x, 10.0f);
        EQUALS(entity.IsPoseServerAuthored(), false);
    });

    IT("reads a server write after the owner's report as a move", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;
        entity.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);

        deliverOwnerPose(entity, glm::vec3(10.0f, 20.0f, 30.0f));
        entity.position = glm::vec3(500.0f, 20.0f, 30.0f);
        EQUALS(entity.IsPoseServerAuthored(), true);

        // The owner taking the teleport and reporting from there settles it again.
        deliverOwnerPose(entity, glm::vec3(500.0f, 20.0f, 30.0f));
        EQUALS(entity.IsPoseServerAuthored(), false);
    });

    IT("ignores a pose delivered by a connection that does not own the entity", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;
        entity.ownerGUID      = MafiaNet::ToPeerGuid(attackerGuid);
        entity.position       = glm::vec3(1.0f, 2.0f, 3.0f);

        deliverOwnerPose(entity, glm::vec3(1.0f, 2.0f, 3.0f));
        EQUALS(entity.IsPoseServerAuthored(), true);
    });

    // A verdict only the server decides, on an entity a client simulates. The owner's upstream
    // stream leaves it out on both ends, so neither an echo of a stale value nor an update the
    // owner sent before a forced state landed can overwrite the server's.
    struct VerdictEntity final : NetworkEntity {
        int measured = 0;
        int verdict  = 0;
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(measured);
            fields.ServerField(verdict);
        }
    };

    // One entity's state channel, serialized the way its peer writes it and delivered to another.
    const auto deliverState = [](VerdictEntity &from, VerdictEntity &to, MafiaNet::Connection_RM3 *sourceConnection) {
        MafiaNet::SerializeParameters sp;
        for (int i = 0; i < MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS; ++i) {
            sp.lastSentBitstream[i] = nullptr;
        }
        sp.whenLastSerialized = 0;
        sp.curTime            = 1;
        from.OnUserReplicaPreSerializeTick();
        from.Serialize(&sp);

        MafiaNet::DeserializeParameters dp;
        for (int i = 0; i < MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS; ++i) {
            dp.bitstreamWrittenTo[i] = false;
        }
        dp.bitstreamWrittenTo[1] = true;
        dp.timeStamp             = 0;
        dp.sourceConnection      = sourceConnection;
        dp.serializationBitstream[1].Write(&sp.outputBitstream[1]);
        to.Deserialize(&dp);
    };

    IT("keeps a server field out of the owner's upstream state", {
        VerdictEntity simulator;
        simulator.replicaManager = clientManager;
        simulator.measured       = 7;
        simulator.verdict        = 1;

        VerdictEntity server;
        server.replicaManager = serverManager;
        server.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);
        server.verdict        = 2;

        deliverState(simulator, server, ownerConn);
        EQUALS(server.measured, 7);
        EQUALS(server.verdict, 2);
    });

    IT("carries a server field to the clients the server relays to", {
        VerdictEntity server;
        server.replicaManager = serverManager;
        server.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);
        server.measured       = 7;
        server.verdict        = 2;

        VerdictEntity observer;
        observer.replicaManager = clientManager;

        deliverState(server, observer, nullptr);
        EQUALS(observer.measured, 7);
        EQUALS(observer.verdict, 2);
    });

    IT("seeds the virtual world when a client constructs an entity", {
        NetworkEntity server;
        server.replicaManager = serverManager;
        server.SetVirtualWorld(123);
        NetworkEntity observer;
        observer.replicaManager = clientManager;
        MafiaNet::BitStream construction;
        server.SerializeConstruction(&construction, nullptr);
        EQUALS(observer.DeserializeConstruction(&construction, nullptr), true);
        EQUALS(observer.GetVirtualWorld(), 123u);
    });

    IT("replicates world changes and refuses the owning client's world assignment", {
        VerdictEntity server;
        server.replicaManager = serverManager;
        server.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);
        server.SetVirtualWorld(123);
        VerdictEntity observer;
        observer.replicaManager = clientManager;
        deliverState(server, observer, nullptr);
        EQUALS(observer.GetVirtualWorld(), 123u);
        server.SetVirtualWorld(456);
        deliverState(server, observer, nullptr);
        EQUALS(observer.GetVirtualWorld(), 456u);
        observer.SetVirtualWorld(999);
        deliverState(observer, server, ownerConn);
        EQUALS(server.GetVirtualWorld(), 456u);
    });

    IT("pushes a world change to an owner that remains visible but receives no relays", {
        NetworkEntity server;
        server.replicaManager = serverManager;
        server.ownerGUID      = MafiaNet::ToPeerGuid(ownerGuid);
        ownerConn->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        EQUALS(server.QuerySerialization(ownerConn), MafiaNet::RM3QSR_DO_NOT_CALL_SERIALIZE);
        const auto oldEpoch = server.stateEpoch;
        server.SetVirtualWorld(123);
        EQUALS(server.stateEpoch, static_cast<uint8_t>(oldEpoch + 1));
        EQUALS(server.QuerySerialization(ownerConn), MafiaNet::RM3QSR_DO_NOT_CALL_SERIALIZE);
        server.SetVirtualWorld(123);
        EQUALS(server.stateEpoch, static_cast<uint8_t>(oldEpoch + 1));

        NetworkEntity owner;
        owner.replicaManager = clientManager;
        MafiaNet::BitStream payload;
        Framework::Networking::Replication::FieldSerializer output(&payload, true);
        server.SerializeForcedSnapshot(output);
        Framework::Networking::Replication::FieldSerializer input(&payload, false);
        owner.SerializeForcedSnapshot(input);
        EQUALS(input.Good(), true);
        EQUALS(owner.GetVirtualWorld(), 123u);
        EQUALS(owner.stateEpoch, 0u);
        ownerConn->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_DEFAULT);
    });

    IT("carries the forced world even when a game replaces the base forced fields", {
        struct CustomForcedEntity final: NetworkEntity {
            int custom = 0;
            void SerializeForcedState(Framework::Networking::Replication::FieldSerializer &fields) override {
                fields.Field(custom);
            }
        };
        CustomForcedEntity server;
        server.replicaManager = serverManager;
        server.SetVirtualWorld(456);
        server.custom = 42;
        CustomForcedEntity owner;
        owner.replicaManager = clientManager;
        MafiaNet::BitStream payload;
        Framework::Networking::Replication::FieldSerializer output(&payload, true);
        server.SerializeForcedSnapshot(output);
        Framework::Networking::Replication::FieldSerializer input(&payload, false);
        owner.SerializeForcedSnapshot(input);
        EQUALS(input.Good(), true);
        EQUALS(owner.GetVirtualWorld(), 456u);
        EQUALS(owner.custom, 42);
    });

    serverManager->DeallocConnection(ownerConn);
    serverManager->DeallocConnection(attackerConn);
});
