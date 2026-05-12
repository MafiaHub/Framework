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

namespace Framework::World::Modules {
    namespace {
        // Pull the payload that the streaming system attached to the event.
        // Bails out gracefully if anyone emits one of these events without a
        // payload — observers must be defensive since flecs lets you fire
        // events from arbitrary call sites.
        template <typename E>
        const Base::StreamEventBase *PayloadFrom(flecs::iter &it) {
            // flecs::iter::param() returns a void* to the event payload, which
            // we cast back to the event type and slice up to the shared base.
            return static_cast<const E *>(it.param());
        }

        void InvokeModProc(const Base::Streamable::Events::Proc &proc, Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            if (proc)
                proc(peer, guid, e);
        }
    } // anonymous namespace

    void Base::RegisterServerStreamObservers(flecs::world &world) {
        world.observer<Streamable>("ServerStreamSpawn")
            .event<StreamSpawnEvent>()
            .each([](flecs::iter &it, size_t i, Streamable &s) {
                const auto *payload = PayloadFrom<StreamSpawnEvent>(it);
                if (!payload || !payload->peer) return;
                const auto e = it.entity(i);

                Framework::Networking::Messages::GameSyncEntitySpawn msg;
                if (const auto tr = e.try_get<Transform>())
                    msg.FromParameters(*tr);
                msg.SetServerID(e.id());
                payload->peer->Send(msg, payload->targetGuid);

                InvokeModProc(s.modEvents.spawnProc, payload->peer, payload->targetGuid, e);
            });

        world.observer<Streamable>("ServerStreamDespawn")
            .event<StreamDespawnEvent>()
            .each([](flecs::iter &it, size_t i, Streamable &s) {
                const auto *payload = PayloadFrom<StreamDespawnEvent>(it);
                if (!payload || !payload->peer) return;
                const auto e = it.entity(i);

                // Mod's despawn proc runs BEFORE the network message so it can
                // still read state that may rely on the entity being mapped on
                // the recipient. Matches the legacy SetupServerEmitters order.
                InvokeModProc(s.modEvents.despawnProc, payload->peer, payload->targetGuid, e);

                Framework::Networking::Messages::GameSyncEntityDespawn msg;
                msg.SetServerID(e.id());
                payload->peer->Send(msg, payload->targetGuid);
            });

        world.observer<Streamable>("ServerStreamSelfUpdate")
            .event<StreamSelfUpdateEvent>()
            .each([](flecs::iter &it, size_t i, Streamable &s) {
                const auto *payload = PayloadFrom<StreamSelfUpdateEvent>(it);
                if (!payload || !payload->peer) return;
                const auto e = it.entity(i);

                Framework::Networking::Messages::GameSyncEntitySelfUpdate msg;
                msg.SetServerID(e.id());
                payload->peer->Send(msg, payload->targetGuid);

                InvokeModProc(s.modEvents.selfUpdateProc, payload->peer, payload->targetGuid, e);
            });

        world.observer<Streamable>("ServerStreamUpdate")
            .event<StreamUpdateEvent>()
            .each([](flecs::iter &it, size_t i, Streamable &s) {
                const auto *payload = PayloadFrom<StreamUpdateEvent>(it);
                if (!payload || !payload->peer) return;
                const auto e = it.entity(i);

                const auto tr = e.try_get<Transform>();
                if (tr && s.owner != SLNet::UNASSIGNED_RAKNET_GUID.g) {
                    Framework::Networking::Messages::GameSyncEntityUpdate msg;
                    msg.FromParameters(*tr, s.owner);
                    msg.SetServerID(e.id());
                    payload->peer->Send(msg, payload->targetGuid);
                }

                InvokeModProc(s.modEvents.updateProc, payload->peer, payload->targetGuid, e);
            });

        world.observer<Streamable>("ServerStreamOwnerUpdate")
            .event<StreamOwnerUpdateEvent>()
            .each([](flecs::iter &it, size_t i, Streamable &s) {
                const auto *payload = PayloadFrom<StreamOwnerUpdateEvent>(it);
                if (!payload || !payload->peer) return;
                const auto e = it.entity(i);

                if (const auto tr = e.try_get<Transform>(); tr && s.owner != SLNet::UNASSIGNED_RAKNET_GUID.g) {
                    Framework::Networking::Messages::GameSyncEntityOwnerUpdate msg;
                    msg.FromParameters(s.owner);
                    msg.SetServerID(e.id());
                    payload->peer->Send(msg, payload->targetGuid);
                }

                InvokeModProc(s.modEvents.ownerUpdateProc, payload->peer, payload->targetGuid, e);
            });
    }

    void Base::RegisterClientStreamObservers(flecs::world &world) {
        world.observer<Streamable>("ClientStreamUpdate")
            .event<StreamUpdateEvent>()
            .each([](flecs::iter &it, size_t i, Streamable &s) {
                const auto *payload = PayloadFrom<StreamUpdateEvent>(it);
                if (!payload || !payload->peer) return;
                const auto e = it.entity(i);

                const auto tr  = e.try_get<Transform>();
                const auto sid = e.try_get<ServerID>();
                if (tr && sid) {
                    Framework::Networking::Messages::GameSyncEntityUpdate msg;
                    msg.FromParameters(*tr, 0);
                    msg.SetServerID(sid->id);
                    payload->peer->Send(msg, payload->targetGuid);
                }

                InvokeModProc(s.modEvents.updateProc, payload->peer, payload->targetGuid, e);
            });
    }

    void Base::SetupServerReceivers(Framework::Networking::NetworkPeer *net, Framework::World::Engine *worldEngine) {
        using namespace Framework::Networking::Messages;
        net->RegisterMessage<GameSyncEntityUpdate>(GameMessages::GAME_SYNC_ENTITY_UPDATE, [worldEngine](SLNet::RakNetGUID guid, GameSyncEntityUpdate *msg) {
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

            const auto tr = e.try_get_mut<World::Modules::Base::Transform>();
            if (!tr)
                return;
            const auto incomingTr = msg->GetTransform();

            if (tr->ValidateGeneration(incomingTr)) {
                *tr = incomingTr;
            }
        });
    }

    void Base::SetupClientReceivers(Framework::Networking::NetworkPeer *net, Framework::World::ClientEngine *worldEngine, Framework::World::Archetypes::StreamingFactory *streamingFactory) {
        using namespace Framework::Networking::Messages;
        net->RegisterMessage<GameSyncEntitySpawn>(GameMessages::GAME_SYNC_ENTITY_SPAWN, [worldEngine, streamingFactory](SLNet::RakNetGUID guid, GameSyncEntitySpawn *msg) {
            if (!msg->Valid()) {
                return;
            }
            if (worldEngine->GetEntityByServerID(msg->GetServerID()).is_alive()) {
                return;
            }
            const auto e = worldEngine->CreateEntity(msg->GetServerID());
            streamingFactory->SetupClient(e, SLNet::UNASSIGNED_RAKNET_GUID.g);

            e.add<World::Modules::Base::Transform>();
            const auto tr = e.try_get_mut<World::Modules::Base::Transform>();
            *tr           = msg->GetTransform();
        });
        net->RegisterMessage<GameSyncEntityDespawn>(GameMessages::GAME_SYNC_ENTITY_DESPAWN, [worldEngine](SLNet::RakNetGUID guid, GameSyncEntityDespawn *msg) {
            if (!msg->Valid()) {
                return;
            }

            const auto e = worldEngine->GetEntityByServerID(msg->GetServerID());

            if (!e.is_alive()) {
                return;
            }

            e.destruct();
        });
        net->RegisterMessage<GameSyncEntityUpdate>(GameMessages::GAME_SYNC_ENTITY_UPDATE, [worldEngine](SLNet::RakNetGUID guid, GameSyncEntityUpdate *msg) {
            if (!msg->Valid()) {
                return;
            }

            const auto e = worldEngine->GetEntityByServerID(msg->GetServerID());

            if (!e.is_alive()) {
                return;
            }

            const auto tr = e.try_get_mut<World::Modules::Base::Transform>();
            const auto es = e.try_get_mut<World::Modules::Base::Streamable>();
            if (!tr || !es)
                return;
            *tr       = msg->GetTransform();
            es->owner = msg->GetOwner();
        });
        net->RegisterMessage<GameSyncEntityUpdate>(GameMessages::GAME_SYNC_ENTITY_OWNER_UPDATE, [worldEngine](SLNet::RakNetGUID guid, GameSyncEntityUpdate *msg) {
            if (!msg->Valid()) {
                return;
            }

            const auto e = worldEngine->GetEntityByServerID(msg->GetServerID());

            if (!e.is_alive()) {
                return;
            }
            const auto es = e.try_get_mut<World::Modules::Base::Streamable>();
            if (!es)
                return;
            es->owner = msg->GetOwner();
        });
        net->RegisterMessage<GameSyncEntitySelfUpdate>(GameMessages::GAME_SYNC_ENTITY_SELF_UPDATE, [worldEngine](SLNet::RakNetGUID guid, GameSyncEntitySelfUpdate *msg) {
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
