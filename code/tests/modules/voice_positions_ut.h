/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/replication/client_entities.h"
#include "../replication_test_world.h"
#include "networking/network_server.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"
#include "voice/server/voice_server.h"

#include <glm/glm.hpp>
#include <mafianet/bit_stream.h>
#include <mafianet/message_identifiers.h>
#include <mafianet/replica_manager3.h>
#include <mafianet/types.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
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
    using ServerEntity = TestWorld::Owned;

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
    // Where each peer finds its players: a server in its viewer map, a client in the avatars the
    // server flagged.
    const auto avatars = [](ReplicationManager *manager) {
        std::vector<Reported> out;
        if (manager->IsServer()) {
            for (const auto &[guid, id] : manager->GetViewerIds()) {
                if (const auto avatar = manager->GetWorld().Read<NetworkEntity>(id)) {
                    out.push_back({guid, avatar->position});
                }
            }
            return out;
        }
        Framework::Networking::Replication::ClientEntities(*manager).ForEachAvatar([&out](MafiaNet::PeerGuid guid, NetworkEntity *avatar) {
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
        ServerEntity aliceAvatar(serverManager);
        ServerEntity bobAvatar(serverManager);
        ServerEntity delegatedNpc(serverManager);

        aliceAvatar->ownerGUID = alice;
        aliceAvatar->position  = aliceAt;
        serverManager->SetViewer(alice, aliceAvatar.id);
        bobAvatar->ownerGUID = bob;
        bobAvatar->position  = bobAt;
        serverManager->SetViewer(bob, bobAvatar.id);
        delegatedNpc->ownerGUID = alice;
        delegatedNpc->position  = farAway;

        // Exactly what Server::VoicePositions does each tick.
        VoiceServer voice;
        voice.SyncAvatars(*serverManager);

        std::vector<uint64_t> recipients;
        voice.WithRouter([&](Framework::Voice::VoiceRouter &router) {
            router.ComputeRecipients(static_cast<uint64_t>(alice), recipients);
        });
        const bool bobHearsAlice = std::ranges::contains(recipients, static_cast<uint64_t>(bob));
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
        ServerEntity aliceAvatar(serverManager);
        ServerEntity bobAvatar(serverManager);
        aliceAvatar->ownerGUID = alice;
        aliceAvatar->position  = aliceAt;
        serverManager->SetViewer(alice, aliceAvatar.id);
        bobAvatar->ownerGUID = bob;
        bobAvatar->position  = bobAt;
        bobAvatar->SetVirtualWorld(42);
        serverManager->SetViewer(bob, bobAvatar.id);

        VoiceServer voice;
        const auto hears = [&voice](MafiaNet::PeerGuid talker, MafiaNet::PeerGuid listener) {
            std::vector<uint64_t> recipients;
            voice.WithRouter([&](Framework::Voice::VoiceRouter &router) {
                router.ComputeRecipients(static_cast<uint64_t>(talker), recipients);
            });
            return std::ranges::contains(recipients, static_cast<uint64_t>(listener));
        };

        voice.SyncAvatars(*serverManager);
        const bool bobHearsAlice = hears(alice, bob);
        const bool aliceHearsBob = hears(bob, alice);

        // Walking out of the dungeon puts them back in earshot on the next tick.
        bobAvatar->SetVirtualWorld(aliceAvatar->GetVirtualWorld());
        voice.SyncAvatars(*serverManager);
        const bool bobHearsAliceAgain = hears(alice, bob);

        // Before asserting: a failed EQUALS returns, and stale viewers would break later tests.
        serverManager->ClearViewer(alice);
        serverManager->ClearViewer(bob);
        EQUALS(bobHearsAlice, false);
        EQUALS(aliceHearsBob, false);
        EQUALS(bobHearsAliceAgain, true);
    });

    IT("stops routing a player whose avatar is gone, in both directions", {
        // A mod moving a player to another map can tear the avatar down before making the next
        // one. In between they stand nowhere, so they must not be heard where they last stood.
        ServerEntity aliceAvatar(serverManager);
        ServerEntity bobAvatar(serverManager);
        aliceAvatar->ownerGUID = alice;
        aliceAvatar->position  = aliceAt;
        serverManager->SetViewer(alice, aliceAvatar.id);
        bobAvatar->ownerGUID = bob;
        bobAvatar->position  = bobAt;
        serverManager->SetViewer(bob, bobAvatar.id);

        VoiceServer voice;
        const auto hears = [&voice](MafiaNet::PeerGuid talker, MafiaNet::PeerGuid listener) {
            std::vector<uint64_t> recipients;
            voice.WithRouter([&](Framework::Voice::VoiceRouter &router) {
                router.ComputeRecipients(static_cast<uint64_t>(talker), recipients);
            });
            return std::ranges::contains(recipients, static_cast<uint64_t>(listener));
        };

        voice.SyncAvatars(*serverManager);
        const bool bobHeardWithAvatar = hears(alice, bob);

        serverManager->ClearViewer(bob);
        voice.SyncAvatars(*serverManager);
        const bool bobHearsAlice = hears(alice, bob);
        const bool aliceHearsBob = hears(bob, alice);

        serverManager->ClearViewer(alice);
        EQUALS(bobHeardWithAvatar, true);
        EQUALS(bobHearsAlice, false);
        EQUALS(aliceHearsBob, false);
    });

    IT("gives the server no position for a player who owns entities but has no avatar", {
        ServerEntity ownedProp(serverManager);
        ownedProp->ownerGUID = alice;
        ownedProp->position  = farAway;

        const int aliceReports = countFor(avatars(serverManager), alice);
        EQUALS(aliceReports, 0);
    });

    IT("places a speaker at their avatar on a client, not at an entity they only own", {
        NetworkEntity serverAvatar;
        NetworkEntity serverNpc;
        serverAvatar.replicaManager     = TestWorld::Rm3(serverManager);
        serverNpc.replicaManager        = TestWorld::Rm3(serverManager);
        serverAvatar.ownerGUID          = alice;
        serverAvatar.position           = aliceAt;
        serverAvatar.streaming.isViewer = true;
        serverNpc.ownerGUID             = alice;
        serverNpc.position              = farAway;

        NetworkEntity clientAvatar;
        NetworkEntity clientNpc;
        TestWorld::Rm3(clientManager)->Reference(&clientAvatar);
        TestWorld::Rm3(clientManager)->Reference(&clientNpc);
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
        ServerEntity serverOld(serverManager);
        ServerEntity serverNew(serverManager);
        serverOld->ownerGUID = alice;
        serverOld->position  = farAway;
        serverNew->ownerGUID = alice;
        serverNew->position  = aliceAt;
        serverManager->SetViewer(alice, serverOld.id);

        NetworkEntity clientOld;
        NetworkEntity clientNew;
        TestWorld::Rm3(clientManager)->Reference(&clientOld);
        TestWorld::Rm3(clientManager)->Reference(&clientNew);
        construct(*serverOld, clientOld);
        construct(*serverNew, clientNew);
        deliverState(*serverOld, clientOld, true);
        deliverState(*serverNew, clientNew, true);

        serverManager->SetViewer(alice, serverNew.id);
        deliverState(*serverOld, clientOld, false);
        deliverState(*serverNew, clientNew, false);

        const std::vector<Reported> reported = avatars(clientManager);
        const int aliceReports               = countFor(reported, alice);
        const bool atAvatar                  = reported.size() == 1 && reported[0].position == aliceAt;
        EQUALS(aliceReports, 1);
        EQUALS(atAvatar, true);

        serverManager->ClearViewer(alice);
    });

    IT("does not let a client promote an entity to an avatar on the server", {
        NetworkEntity serverNpc;
        serverNpc.replicaManager = TestWorld::Rm3(serverManager);
        serverNpc.ownerGUID      = alice;

        NetworkEntity clientNpc;
        clientNpc.replicaManager     = TestWorld::Rm3(clientManager);
        clientNpc.ownerGUID          = alice;
        clientNpc.streaming.isViewer = true;

        deliverState(clientNpc, serverNpc, true);
        const bool promoted = serverNpc.streaming.isViewer;
        EQUALS(promoted, false);
    });

    // A relay frame as a client's RakVoice writes it: the header, then one byte of payload.
    const auto relayFrame = [](MafiaNet::PeerGuid origin) {
        std::vector<unsigned char> data(MafiaNet::RAKVOICE_RELAY_HEADER_SIZE + 1, 0);
        data[0]                                       = ID_RAKVOICE_RELAY_DATA;
        data[MafiaNet::RAKVOICE_RELAY_OFFSET_VERSION] = MafiaNet::RAKVOICE_RELAY_FORMAT_VERSION;
        const uint64_t raw                            = static_cast<uint64_t>(origin);
        std::memcpy(data.data() + MafiaNet::RAKVOICE_RELAY_OFFSET_ORIGIN, &raw, sizeof(raw));
        return data;
    };
    // What RakVoice hands the relay on the network thread.
    const auto relay = [&relayFrame](VoiceServer &voice, MafiaNet::PeerGuid origin, MafiaNet::PeerGuid sender) {
        std::vector<unsigned char> data = relayFrame(origin);
        MafiaNet::Packet packet {};
        packet.data   = data.data();
        packet.length = static_cast<unsigned int>(data.size());
        packet.guid   = MafiaNet::ToGuid(sender);
        voice.OnVoiceFrame(&packet);
    };
    const auto relayedTo = [](const VoiceServer &voice, MafiaNet::PeerGuid talker, MafiaNet::PeerGuid listener) {
        const std::vector<MafiaNet::RakNetGUID> recipients = voice.GetRelayRecipients(static_cast<uint64_t>(talker));
        return std::any_of(recipients.begin(), recipients.end(), [listener](const MafiaNet::RakNetGUID &guid) {
            return MafiaNet::ToPeerGuid(guid) == listener;
        });
    };
    const auto placeBoth = [&](NetworkEntity &aliceAvatar, NetworkEntity &bobAvatar) {
        aliceAvatar.ownerGUID = alice;
        aliceAvatar.position  = aliceAt;
        serverManager->SetViewer(alice, aliceAvatar.GetNetworkID());
        bobAvatar.ownerGUID = bob;
        bobAvatar.position  = bobAt;
        serverManager->SetViewer(bob, bobAvatar.GetNetworkID());
    };

    IT("relays a talker from the tick after their first frame, and reports them talking", {
        ServerEntity aliceAvatar(serverManager);
        ServerEntity bobAvatar(serverManager);
        placeBoth(*aliceAvatar, *bobAvatar);

        VoiceServer voice;
        voice.SyncAvatars(*serverManager);
        voice.Update();

        relay(voice, alice, alice);
        const bool relayedBeforeTick = relayedTo(voice, alice, bob);
        const bool talkingBeforeTick = voice.IsPlayerTalking(static_cast<uint64_t>(alice));

        voice.Update();
        const bool relayedAfterTick = relayedTo(voice, alice, bob);
        const bool talkingAfterTick = voice.IsPlayerTalking(static_cast<uint64_t>(alice));
        std::vector<Framework::Voice::TalkingChange> changes;
        voice.DrainTalkingChanges(changes);

        serverManager->ClearViewer(alice);
        serverManager->ClearViewer(bob);
        EQUALS(relayedBeforeTick, false);
        EQUALS(talkingBeforeTick, false);
        EQUALS(relayedAfterTick, true);
        EQUALS(talkingAfterTick, true);
        EQUALS(changes.size(), static_cast<size_t>(1));
        EQUALS(changes[0].guid, static_cast<uint64_t>(alice));
        EQUALS(changes[0].talking, true);
    });

    IT("drops a listener from the relay table the tick their world or avatar changes", {
        ServerEntity aliceAvatar(serverManager);
        ServerEntity bobAvatar(serverManager);
        placeBoth(*aliceAvatar, *bobAvatar);

        VoiceServer voice;
        voice.SyncAvatars(*serverManager);
        relay(voice, alice, alice);
        voice.Update();
        const bool relayedTogether = relayedTo(voice, alice, bob);

        bobAvatar->SetVirtualWorld(42);
        voice.SyncAvatars(*serverManager);
        voice.Update();
        const bool relayedAcrossWorlds = relayedTo(voice, alice, bob);

        bobAvatar->SetVirtualWorld(aliceAvatar->GetVirtualWorld());
        voice.SyncAvatars(*serverManager);
        voice.Update();
        const bool relayedOnReturn = relayedTo(voice, alice, bob);

        serverManager->ClearViewer(bob);
        voice.SyncAvatars(*serverManager);
        voice.Update();
        const bool relayedWithoutAvatar = relayedTo(voice, alice, bob);

        serverManager->ClearViewer(alice);
        EQUALS(relayedTogether, true);
        EQUALS(relayedAcrossWorlds, false);
        EQUALS(relayedOnReturn, true);
        EQUALS(relayedWithoutAvatar, false);
    });

    IT("drops a listener who walks out of range once the refresh interval has passed", {
        ServerEntity aliceAvatar(serverManager);
        ServerEntity bobAvatar(serverManager);
        placeBoth(*aliceAvatar, *bobAvatar);

        VoiceServer voice;
        voice.SyncAvatars(*serverManager);
        relay(voice, alice, alice);
        voice.Update();

        bobAvatar->position = farAway;
        voice.SyncAvatars(*serverManager);
        voice.Update();
        const bool relayedWithinInterval = relayedTo(voice, alice, bob);

        std::this_thread::sleep_for(std::chrono::milliseconds(Framework::Voice::kRecipientRefreshMs + 20));
        voice.Update();
        const bool relayedAfterInterval = relayedTo(voice, alice, bob);

        serverManager->ClearViewer(alice);
        serverManager->ClearViewer(bob);
        EQUALS(relayedWithinInterval, true);
        EQUALS(relayedAfterInterval, false);
    });

    IT("takes preferences and tier requests from another thread while the tick runs", {
        ServerEntity aliceAvatar(serverManager);
        ServerEntity bobAvatar(serverManager);
        placeBoth(*aliceAvatar, *bobAvatar);

        VoiceServer voice;
        std::atomic<bool> stop {false};
        // What a thread handling packets does with the two voice RPCs and a connection closing.
        std::thread packets([&] {
            for (int i = 0; !stop; ++i) {
                voice.OnPlayerPreference(static_cast<uint64_t>(bob), (i & 1) == 0);
                voice.OnTierRequest(static_cast<uint64_t>(alice), static_cast<uint8_t>(i % 2));
                voice.SetPlayerRange(static_cast<uint64_t>(alice), 10.0f + static_cast<float>(i % 5));
                voice.OnPlayerDisconnect(static_cast<uint64_t>(MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(900 + i % 3))));
            }
        });
        std::vector<Framework::Voice::TierChange> tiers;
        // Every tier change the ticks are told of: who it names, and the last one.
        bool strayChange = false;
        int changes      = 0;
        uint32_t lastTier = 0;
        const auto note = [&] {
            for (const Framework::Voice::TierChange &change : tiers) {
                strayChange = strayChange || change.guid != static_cast<uint64_t>(alice) || static_cast<uint32_t>(change.tier) > 1;
                lastTier    = static_cast<uint32_t>(change.tier);
                ++changes;
            }
        };
        for (int tick = 0; tick < 300; ++tick) {
            voice.SyncAvatars(*serverManager);
            relay(voice, alice, alice);
            voice.Update();
            voice.DrainTierChanges(tiers);
            note();
            (void)voice.IsPlayerTalking(static_cast<uint64_t>(alice));
            (void)voice.WithRouter([&](Framework::Voice::VoiceRouter &router) {
                return router.GetPlayerTier(static_cast<uint64_t>(alice));
            });
        }
        stop = true;
        packets.join();

        // Settled by one thread, the rule reads back what was last asked of it.
        voice.OnPlayerPreference(static_cast<uint64_t>(bob), false);
        const bool bobDisabled = voice.WithRouter([&](Framework::Voice::VoiceRouter &router) {
            return router.IsPlayerVoiceDisabled(static_cast<uint64_t>(bob));
        });
        const bool aliceTalking = voice.IsPlayerTalking(static_cast<uint64_t>(alice));

        // And the tier the router ends on is the one the last change it reported said: requests are paced,
        // so which of the two she asked for it is depends on timing, but the two always agree. Every change
        // named the player who asked and a tier she asked for.
        voice.Update();
        voice.DrainTierChanges(tiers);
        note();
        const auto aliceTier = voice.WithRouter([&](Framework::Voice::VoiceRouter &router) {
            return router.GetPlayerTier(static_cast<uint64_t>(alice));
        });

        serverManager->ClearViewer(alice);
        serverManager->ClearViewer(bob);
        EQUALS(bobDisabled, true);
        EQUALS(aliceTalking, true);
        EQUALS(static_cast<uint32_t>(aliceTier), lastTier);
        GREATER(changes, 0);
        EQUALS(strayChange, false);
    });

    IT("forgets a frame still waiting for the tick when its talker disconnects", {
        ServerEntity aliceAvatar(serverManager);
        ServerEntity bobAvatar(serverManager);
        placeBoth(*aliceAvatar, *bobAvatar);

        VoiceServer voice;
        voice.SyncAvatars(*serverManager);
        relay(voice, alice, alice);
        voice.OnPlayerDisconnect(static_cast<uint64_t>(alice));
        voice.Update();

        const bool talking = voice.IsPlayerTalking(static_cast<uint64_t>(alice));
        const bool relayed = relayedTo(voice, alice, bob);
        std::vector<Framework::Voice::TalkingChange> changes;
        voice.DrainTalkingChanges(changes);

        serverManager->ClearViewer(alice);
        serverManager->ClearViewer(bob);
        EQUALS(talking, false);
        EQUALS(relayed, false);
        EQUALS(changes.empty(), true);
    });

    IT("ignores a frame whose origin is not its sender", {
        ServerEntity aliceAvatar(serverManager);
        ServerEntity bobAvatar(serverManager);
        placeBoth(*aliceAvatar, *bobAvatar);

        VoiceServer voice;
        voice.SyncAvatars(*serverManager);
        relay(voice, alice, bob);
        voice.Update();

        const bool aliceTalking = voice.IsPlayerTalking(static_cast<uint64_t>(alice));
        const bool bobTalking   = voice.IsPlayerTalking(static_cast<uint64_t>(bob));
        const bool relayed      = relayedTo(voice, alice, bob);

        serverManager->ClearViewer(alice);
        serverManager->ClearViewer(bob);
        EQUALS(aliceTalking, false);
        EQUALS(bobTalking, false);
        EQUALS(relayed, false);
    });
});
