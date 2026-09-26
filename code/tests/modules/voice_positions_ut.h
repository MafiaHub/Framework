/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_server.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"
#include "voice/server/voice_server.h"

#include <glm/glm.hpp>
#include <mafianet/BitStream.h>
#include <mafianet/ReplicaManager3.h>
#include <mafianet/types.h>

#include <algorithm>
#include <cstdint>
#include <vector>

// Where proximity voice places a speaker. Both peers key a speaker's position on the player's guid,
// and a player can own far more than their avatar: an NPC the server delegated to them, a horse they
// ride, an item they dropped. Taking every owned entity as "the player" let whichever of those was
// visited last move the player's voice there, so the server routed it to whoever stood near that
// entity and clients played it from the wrong place. The fixture is that case: an avatar, then an
// entity the same player owns a long way off, referenced after it so it is visited last.
MODULE(voice_positions, {
    using Framework::Networking::NetworkServer;
    using Framework::Networking::Replication::FieldSerializer;
    using Framework::Networking::Replication::NetworkEntity;
    using Framework::Networking::Replication::ReplicationManager;
    using Framework::Voice::VoiceServer;

    // Unstarted peers, as in replication_authority_ut: the role is ReplicationManager::Init's flag.
    NetworkServer serverPeer;
    NetworkServer clientPeer;
    auto *serverManager = serverPeer.GetReplicationManager();
    auto *clientManager = clientPeer.GetReplicationManager();
    serverManager->Init(&serverPeer, true);
    clientManager->Init(&clientPeer, false);

    const MafiaNet::PeerGuid alice = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(11));
    const MafiaNet::PeerGuid bob   = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(22));

    const glm::vec3 aliceAt(0.0f, 0.0f, 0.0f);
    const glm::vec3 bobAt(5.0f, 0.0f, 0.0f);
    // Far outside any voice range from either player.
    const glm::vec3 farAway(5000.0f, 0.0f, 0.0f);

    struct Reported {
        MafiaNet::PeerGuid guid;
        glm::vec3 position;
    };
    const auto avatars = [](const ReplicationManager *manager) {
        std::vector<Reported> out;
        manager->ForEachAvatar([&out](MafiaNet::PeerGuid guid, NetworkEntity *avatar) {
            out.push_back({guid, avatar->position});
        });
        return out;
    };
    const auto countFor = [](const std::vector<Reported> &reported, MafiaNet::PeerGuid guid) {
        return static_cast<int>(std::count_if(reported.begin(), reported.end(), [guid](const Reported &r) {
            return r.guid == guid;
        }));
    };

    // One server tick's worth of the state channel, carried to another peer's copy of the entity
    // the way ReplicaManager3 would.
    const auto deliverState = [](NetworkEntity &from, NetworkEntity &to, bool firstSend) {
        MafiaNet::SerializeParameters sp;
        for (int i = 0; i < MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS; ++i) {
            sp.lastSentBitstream[i] = nullptr;
        }
        sp.whenLastSerialized = firstSend ? 0 : 1;
        sp.curTime            = 1000;
        from.OnUserReplicaPreSerializeTick();
        from.Serialize(&sp);

        MafiaNet::DeserializeParameters dp;
        for (int i = 0; i < MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS; ++i) {
            dp.bitstreamWrittenTo[i] = false;
        }
        dp.timeStamp        = 0;
        dp.sourceConnection = nullptr;
        if (sp.outputBitstream[1].GetNumberOfBitsUsed() > 0) {
            dp.bitstreamWrittenTo[1] = true;
            dp.serializationBitstream[1].Write(&sp.outputBitstream[1]);
        }
        to.Deserialize(&dp);
    };

    // A server entity's construction snapshot, built into a client copy already in the client's
    // replica list.
    const auto construct = [](NetworkEntity &serverEntity, NetworkEntity &clientEntity) {
        MafiaNet::BitStream bs;
        serverEntity.SerializeConstruction(&bs, nullptr);
        clientEntity.DeserializeConstruction(&bs, nullptr);
    };

    IT("places a speaker at their avatar on the server, not at an entity they only own", {
        NetworkEntity aliceAvatar;
        NetworkEntity bobAvatar;
        NetworkEntity delegatedNpc;
        serverManager->Reference(&aliceAvatar);
        serverManager->Reference(&bobAvatar);
        serverManager->Reference(&delegatedNpc);

        aliceAvatar.ownerGUID = alice;
        aliceAvatar.position  = aliceAt;
        serverManager->SetViewer(alice, &aliceAvatar);
        bobAvatar.ownerGUID = bob;
        bobAvatar.position  = bobAt;
        serverManager->SetViewer(bob, &bobAvatar);
        delegatedNpc.ownerGUID = alice;
        delegatedNpc.position  = farAway;

        // Exactly what Server::VoicePositions does each tick.
        VoiceServer voice;
        voice.SyncAvatars(*serverManager);

        std::vector<uint64_t> recipients;
        voice.GetRouter().ComputeRecipients(static_cast<uint64_t>(alice), recipients);
        const bool bobHearsAlice = std::find(recipients.begin(), recipients.end(), static_cast<uint64_t>(bob)) != recipients.end();
        EQUALS(bobHearsAlice, true);

        const std::vector<Reported> reported = avatars(serverManager);
        const int aliceReports               = countFor(reported, alice);
        const int bobReports                 = countFor(reported, bob);
        EQUALS(aliceReports, 1);
        EQUALS(bobReports, 1);

        serverManager->ClearViewer(alice);
        serverManager->ClearViewer(bob);
    });

    IT("keeps a speaker from a listener whose avatar is in another virtual world", {
        // A dungeon and the overland map share coordinates, so the two stand side by side.
        NetworkEntity aliceAvatar;
        NetworkEntity bobAvatar;
        serverManager->Reference(&aliceAvatar);
        serverManager->Reference(&bobAvatar);
        aliceAvatar.ownerGUID = alice;
        aliceAvatar.position  = aliceAt;
        serverManager->SetViewer(alice, &aliceAvatar);
        bobAvatar.ownerGUID = bob;
        bobAvatar.position  = bobAt;
        bobAvatar.SetVirtualWorld(42);
        serverManager->SetViewer(bob, &bobAvatar);

        VoiceServer voice;
        const auto hears = [&voice](MafiaNet::PeerGuid talker, MafiaNet::PeerGuid listener) {
            std::vector<uint64_t> recipients;
            voice.GetRouter().ComputeRecipients(static_cast<uint64_t>(talker), recipients);
            return std::find(recipients.begin(), recipients.end(), static_cast<uint64_t>(listener)) != recipients.end();
        };

        voice.SyncAvatars(*serverManager);
        const bool bobHearsAlice = hears(alice, bob);
        const bool aliceHearsBob = hears(bob, alice);

        // Walking out of the dungeon puts them back in earshot on the next tick.
        bobAvatar.SetVirtualWorld(aliceAvatar.GetVirtualWorld());
        voice.SyncAvatars(*serverManager);
        const bool bobHearsAliceAgain = hears(alice, bob);

        // Before asserting: a failed EQUALS returns, and stale viewers would break later tests.
        serverManager->ClearViewer(alice);
        serverManager->ClearViewer(bob);
        EQUALS(bobHearsAlice, false);
        EQUALS(aliceHearsBob, false);
        EQUALS(bobHearsAliceAgain, true);
    });

    IT("gives the server no position for a player who owns entities but has no avatar", {
        NetworkEntity ownedProp;
        serverManager->Reference(&ownedProp);
        ownedProp.ownerGUID = alice;
        ownedProp.position  = farAway;

        const int aliceReports = countFor(avatars(serverManager), alice);
        EQUALS(aliceReports, 0);
    });

    IT("places a speaker at their avatar on a client, not at an entity they only own", {
        NetworkEntity serverAvatar;
        NetworkEntity serverNpc;
        serverAvatar.replicaManager     = serverManager;
        serverNpc.replicaManager        = serverManager;
        serverAvatar.ownerGUID          = alice;
        serverAvatar.position           = aliceAt;
        serverAvatar.streaming.isViewer = true;
        serverNpc.ownerGUID             = alice;
        serverNpc.position              = farAway;

        NetworkEntity clientAvatar;
        NetworkEntity clientNpc;
        clientManager->Reference(&clientAvatar);
        clientManager->Reference(&clientNpc);
        construct(serverAvatar, clientAvatar);
        construct(serverNpc, clientNpc);

        const std::vector<Reported> reported = avatars(clientManager);
        const int aliceReports               = countFor(reported, alice);
        const bool atAvatar                  = reported.size() == 1 && reported[0].position == aliceAt;
        EQUALS(aliceReports, 1);
        EQUALS(atAvatar, true);
    });

    IT("moves a client's speaker to a replacement avatar made after construction", {
        // A respawn: the first body was built on the client as the avatar, the server then made
        // another one the viewer. Only the state channel can carry that.
        NetworkEntity serverOld;
        NetworkEntity serverNew;
        serverManager->Reference(&serverOld);
        serverManager->Reference(&serverNew);
        serverOld.ownerGUID = alice;
        serverOld.position  = farAway;
        serverNew.ownerGUID = alice;
        serverNew.position  = aliceAt;
        serverManager->SetViewer(alice, &serverOld);

        NetworkEntity clientOld;
        NetworkEntity clientNew;
        clientManager->Reference(&clientOld);
        clientManager->Reference(&clientNew);
        construct(serverOld, clientOld);
        construct(serverNew, clientNew);
        deliverState(serverOld, clientOld, true);
        deliverState(serverNew, clientNew, true);

        serverManager->SetViewer(alice, &serverNew);
        deliverState(serverOld, clientOld, false);
        deliverState(serverNew, clientNew, false);

        const std::vector<Reported> reported = avatars(clientManager);
        const int aliceReports               = countFor(reported, alice);
        const bool atAvatar                  = reported.size() == 1 && reported[0].position == aliceAt;
        EQUALS(aliceReports, 1);
        EQUALS(atAvatar, true);

        serverManager->ClearViewer(alice);
    });

    IT("does not let a client promote an entity to an avatar on the server", {
        NetworkEntity serverNpc;
        serverNpc.replicaManager = serverManager;
        serverNpc.ownerGUID      = alice;

        NetworkEntity clientNpc;
        clientNpc.replicaManager     = clientManager;
        clientNpc.ownerGUID          = alice;
        clientNpc.streaming.isViewer = true;

        deliverState(clientNpc, serverNpc, true);
        const bool promoted = serverNpc.streaming.isViewer;
        EQUALS(promoted, false);
    });
});
