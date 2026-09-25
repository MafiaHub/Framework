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

    serverManager->DeallocConnection(ownerConn);
    serverManager->DeallocConnection(attackerConn);
});
