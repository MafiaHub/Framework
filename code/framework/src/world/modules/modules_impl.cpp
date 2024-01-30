/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "base.hpp"
#include "networking/messages/game_sync/entity_messages.h"
#include "networking/network_peer.h"
#include "world/client.h"
#include "world/engine.h"

#include "world/types/streaming.hpp"

#define CALL_CUSTOM_PROC(kind)                                                                                         \
    const auto streamable = e.get<Framework::World::Modules::Base::Streamable>();                                      \
    if (streamable != nullptr) {                                                                                       \
        if (streamable->modEvents.kind != nullptr) {                                                                   \
            streamable->modEvents.kind(peer, guid, e);                                                                 \
        }                                                                                                              \
    }

namespace Framework::World::Modules {
    void Base::SetupServerEmitters(Streamable *streamable) {
        streamable->events.spawnProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            Framework::Networking::Messages::GameSyncEntitySpawn entitySpawn;
            const auto tr = e.get<Framework::World::Modules::Base::Transform>();
            if (tr)
                entitySpawn.FromParameters(*tr);
            entitySpawn.SetServerID(e.id());
            peer->Send(entitySpawn, guid);
            CALL_CUSTOM_PROC(spawnProc);
            return true;
        };

        streamable->events.despawnProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            CALL_CUSTOM_PROC(despawnProc);
            Framework::Networking::Messages::GameSyncEntityDespawn entityDespawn;
            entityDespawn.SetServerID(e.id());
            peer->Send(entityDespawn, guid);
            return true;
        };

        streamable->events.selfUpdateProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid,
                                                flecs::entity e) {
            Framework::Networking::Messages::GameSyncEntitySelfUpdate entitySelfUpdate;
            entitySelfUpdate.SetServerID(e.id());
            peer->Send(entitySelfUpdate, guid);
            CALL_CUSTOM_PROC(selfUpdateProc);
            return true;
        };

        streamable->events.updateProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            Framework::Networking::Messages::GameSyncEntityUpdate entityUpdate;
            const auto tr = e.get<Framework::World::Modules::Base::Transform>();
            const auto es = e.get<Framework::World::Modules::Base::Streamable>();
            if (tr && es)
                entityUpdate.FromParameters(*tr, es->owner);
            entityUpdate.SetServerID(e.id());
            peer->Send(entityUpdate, guid);
            CALL_CUSTOM_PROC(updateProc);
            return true;
        };

        streamable->events.ownerUpdateProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid,
                                                 flecs::entity e) {
            Framework::Networking::Messages::GameSyncEntityOwnerUpdate entityUpdate;
            const auto tr = e.get<Framework::World::Modules::Base::Transform>();
            const auto es = e.get<Framework::World::Modules::Base::Streamable>();
            if (tr && es)
                entityUpdate.FromParameters(es->owner);
            entityUpdate.SetServerID(e.id());
            peer->Send(entityUpdate, guid);
            CALL_CUSTOM_PROC(ownerUpdateProc);
            return true;
        };
    }
    void Base::SetupClientEmitters(Streamable *streamable) {
        streamable->events.updateProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            Framework::Networking::Messages::GameSyncEntityUpdate entityUpdate;
            const auto tr = e.get<Framework::World::Modules::Base::Transform>();
            const auto sid = e.get<Framework::World::Modules::Base::ServerID>();
            if (tr && sid) {
                entityUpdate.FromParameters(*tr, 0);
                entityUpdate.SetServerID(sid->id);
            }
            peer->Send(entityUpdate, guid);
            CALL_CUSTOM_PROC(updateProc);
            return true;
        };
    }

    void Base::SetupServerReceivers(Framework::Networking::NetworkPeer *net, Framework::World::Engine *worldEngine) {
        using namespace Framework::Networking::Messages;
        net->RegisterMessage<GameSyncEntityUpdate>(GameMessages::GAME_SYNC_ENTITY_UPDATE,
                                                   [worldEngine](SLNet::RakNetGUID guid, GameSyncEntityUpdate *msg) {
                                                       if (!msg->Valid()) {
                                                           return;
                                                       }

                                                       const auto e = worldEngine->WrapEntity(msg->GetServerID());

                                                       if (!e.is_alive()) {
                                                           return;
                                                       }

                                                       if (!worldEngine->IsEntityOwner(e, guid.g)) {
                                                           return;
                                                       }

                                                       auto tr = e.get_mut<World::Modules::Base::Transform>();
                                                       const auto incomingTr = msg->GetTransform();

                                                       if (tr->ValidateGeneration(incomingTr)) {
                                                           *tr = incomingTr;
                                                       }
                                                   });
    }

    void Base::SetupClientReceivers(Framework::Networking::NetworkPeer *net,
                                    Framework::World::ClientEngine *worldEngine,
                                    Framework::World::Archetypes::StreamingFactory *streamingFactory) {
        using namespace Framework::Networking::Messages;
        net->RegisterMessage<GameSyncEntitySpawn>(
            GameMessages::GAME_SYNC_ENTITY_SPAWN,
            [worldEngine, streamingFactory](SLNet::RakNetGUID guid, GameSyncEntitySpawn *msg) {
                if (!msg->Valid()) {
                    return;
                }
                if (worldEngine->GetEntityByServerID(msg->GetServerID()).is_alive()) {
                    return;
                }
                const auto e = worldEngine->CreateEntity(msg->GetServerID());
                streamingFactory->SetupClient(e, SLNet::UNASSIGNED_RAKNET_GUID.g);

                auto tr = e.get_mut<World::Modules::Base::Transform>();
                *tr = msg->GetTransform();
            });
        net->RegisterMessage<GameSyncEntityDespawn>(
            GameMessages::GAME_SYNC_ENTITY_DESPAWN, [worldEngine](SLNet::RakNetGUID guid, GameSyncEntityDespawn *msg) {
                if (!msg->Valid()) {
                    return;
                }

                const auto e = worldEngine->GetEntityByServerID(msg->GetServerID());

                if (!e.is_alive()) {
                    return;
                }

                e.destruct();
            });
        net->RegisterMessage<GameSyncEntityUpdate>(
            GameMessages::GAME_SYNC_ENTITY_UPDATE, [worldEngine](SLNet::RakNetGUID guid, GameSyncEntityUpdate *msg) {
                if (!msg->Valid()) {
                    return;
                }

                const auto e = worldEngine->GetEntityByServerID(msg->GetServerID());

                if (!e.is_alive()) {
                    return;
                }

                auto tr = e.get_mut<World::Modules::Base::Transform>();
                *tr = msg->GetTransform();

                auto es = e.get_mut<World::Modules::Base::Streamable>();
                es->owner = msg->GetOwner();
            });
        net->RegisterMessage<GameSyncEntityUpdate>(GameMessages::GAME_SYNC_ENTITY_OWNER_UPDATE,
                                                   [worldEngine](SLNet::RakNetGUID guid, GameSyncEntityUpdate *msg) {
                                                       if (!msg->Valid()) {
                                                           return;
                                                       }

                                                       const auto e =
                                                           worldEngine->GetEntityByServerID(msg->GetServerID());

                                                       if (!e.is_alive()) {
                                                           return;
                                                       }
                                                       auto es = e.get_mut<World::Modules::Base::Streamable>();
                                                       es->owner = msg->GetOwner();
                                                   });
        net->RegisterMessage<GameSyncEntitySelfUpdate>(
            GameMessages::GAME_SYNC_ENTITY_SELF_UPDATE,
            [worldEngine](SLNet::RakNetGUID guid, GameSyncEntitySelfUpdate *msg) {
                if (!msg->Valid()) {
                    return;
                }

                const auto e = worldEngine->GetEntityByServerID(msg->GetServerID());

                if (!e.is_alive()) {
                    return;
                }

                // Nothing to do for now.
            });
    }
} // namespace Framework::World::Modules
