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
#include "networking/messages/game_sync/entity_owner_update.h"
#include "networking/network_peer.h"
#include "world/client.h"
#include "world/engine.h"

#include "world/types/streaming.hpp"

#define CALL_CUSTOM_PROC(kind)                                                                                                                                                                                                                                                         \
    const auto streamable = e.get<Framework::World::Modules::Base::Streamable>();                                                                                                                                                                                                      \
    if (streamable != nullptr) {                                                                                                                                                                                                                                                       \
        if (streamable->modEvents.kind != nullptr) {                                                                                                                                                                                                                                   \
            streamable->modEvents.kind(peer, guid, e);                                                                                                                                                                                                                                 \
        }                                                                                                                                                                                                                                                                              \
    }

namespace Framework::World::Modules {
    void Base::SetupServerEmitters(Streamable& streamable) {
        streamable.events.spawnProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            const auto tr = e.get<Framework::World::Modules::Base::Transform>();
            Framework::Networking::Messages::GameSyncEntitySpawn entitySpawn(tr ? *tr : Framework::World::Modules::Base::Transform{});
            entitySpawn.SetServerID(e.id());
            peer->Send(entitySpawn, guid);
            CALL_CUSTOM_PROC(spawnProc);
            return true;
        };

        streamable.events.despawnProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            CALL_CUSTOM_PROC(despawnProc);
            Framework::Networking::Messages::GameSyncEntityDespawn entityDespawn;
            entityDespawn.SetServerID(e.id());
            peer->Send(entityDespawn, guid);
            return true;
        };

        streamable.events.selfUpdateProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            Framework::Networking::Messages::GameSyncEntitySelfUpdate entitySelfUpdate;
            entitySelfUpdate.SetServerID(e.id());
            peer->Send(entitySelfUpdate, guid);
            CALL_CUSTOM_PROC(selfUpdateProc);
            return true;
        };

        streamable.events.updateProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            const auto tr = e.get<Framework::World::Modules::Base::Transform>();
            const auto es = e.get<Framework::World::Modules::Base::Streamable>();
            Framework::Networking::Messages::GameSyncEntityUpdate entityUpdate(
                (tr && es) ? *tr : Framework::World::Modules::Base::Transform{},
                (tr && es) ? es->owner : SLNet::UNASSIGNED_RAKNET_GUID.g);
            entityUpdate.SetServerID(e.id());
            peer->Send(entityUpdate, guid);
            CALL_CUSTOM_PROC(updateProc);
            return true;
        };

        streamable.events.ownerUpdateProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            const auto tr = e.get<Framework::World::Modules::Base::Transform>();
            const auto es = e.get<Framework::World::Modules::Base::Streamable>();
            Framework::Networking::Messages::GameSyncEntityOwnerUpdate entityUpdate(
                (tr && es) ? es->owner : SLNet::UNASSIGNED_RAKNET_GUID.g);
            entityUpdate.SetServerID(e.id());
            peer->Send(entityUpdate, guid);
            CALL_CUSTOM_PROC(ownerUpdateProc);
            return true;
        };
    }
    void Base::SetupClientEmitters(Streamable& streamable) {
        streamable.events.updateProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            const auto tr  = e.get<Framework::World::Modules::Base::Transform>();
            const auto sid = e.get<Framework::World::Modules::Base::ServerID>();
            Framework::Networking::Messages::GameSyncEntityUpdate entityUpdate(
                (tr && sid) ? *tr : Framework::World::Modules::Base::Transform{}, 0);
            if (tr && sid) {
                entityUpdate.SetServerID(sid->id);
            }
            peer->Send(entityUpdate, guid);
            CALL_CUSTOM_PROC(updateProc);
            return true;
        };
    }

    // ServerReceiverHandler implementations
    void ServerReceiverHandler::OnEntityUpdate(SLNet::RakNetGUID guid, Framework::Networking::Messages::GameSyncEntityUpdate *msg) {
        if (!msg->Valid()) {
            return;
        }

        const auto e = _worldEngine->WrapEntity(msg->GetServerID());

        if (!e.is_alive()) {
            return;
        }

        if (!_worldEngine->IsEntityOwner(e, guid.g)) {
            return;
        }

        const auto tr         = e.get_mut<World::Modules::Base::Transform>();
        const auto incomingTr = msg->GetTransform();

        if (tr->ValidateGeneration(incomingTr)) {
            *tr = incomingTr;
        }
    }

    // ClientReceiverHandler implementations
    void ClientReceiverHandler::OnEntitySpawn(SLNet::RakNetGUID guid, Framework::Networking::Messages::GameSyncEntitySpawn *msg) {
        (void)guid;
        if (!msg->Valid()) {
            return;
        }
        if (_worldEngine->GetEntityByServerID(msg->GetServerID()).is_alive()) {
            return;
        }
        const auto e = _worldEngine->CreateEntity(msg->GetServerID());
        _streamingFactory->SetupClient(e, SLNet::UNASSIGNED_RAKNET_GUID.g);

        e.add<World::Modules::Base::Transform>();
        const auto tr = e.get_mut<World::Modules::Base::Transform>();
        *tr           = msg->GetTransform();
    }

    void ClientReceiverHandler::OnEntityDespawn(SLNet::RakNetGUID guid, Framework::Networking::Messages::GameSyncEntityDespawn *msg) {
        (void)guid;
        if (!msg->Valid()) {
            return;
        }

        const auto e = _worldEngine->GetEntityByServerID(msg->GetServerID());

        if (!e.is_alive()) {
            return;
        }

        e.destruct();
    }

    void ClientReceiverHandler::OnEntityUpdate(SLNet::RakNetGUID guid, Framework::Networking::Messages::GameSyncEntityUpdate *msg) {
        (void)guid;
        if (!msg->Valid()) {
            return;
        }

        const auto e = _worldEngine->GetEntityByServerID(msg->GetServerID());

        if (!e.is_alive()) {
            return;
        }

        const auto tr = e.get_mut<World::Modules::Base::Transform>();
        *tr           = msg->GetTransform();

        const auto es = e.get_mut<World::Modules::Base::Streamable>();
        es->owner     = msg->GetOwner();
    }

    void ClientReceiverHandler::OnEntityOwnerUpdate(SLNet::RakNetGUID guid, Framework::Networking::Messages::GameSyncEntityOwnerUpdate *msg) {
        (void)guid;
        if (!msg->Valid()) {
            return;
        }

        const auto e = _worldEngine->GetEntityByServerID(msg->GetServerID());

        if (!e.is_alive()) {
            return;
        }
        const auto es = e.get_mut<World::Modules::Base::Streamable>();
        es->owner     = msg->GetOwner();
    }

    void ClientReceiverHandler::OnEntitySelfUpdate(SLNet::RakNetGUID guid, Framework::Networking::Messages::GameSyncEntitySelfUpdate *msg) {
        (void)guid;
        if (!msg->Valid()) {
            return;
        }

        const auto e = _worldEngine->GetEntityByServerID(msg->GetServerID());

        if (!e.is_alive()) {
            return;
        }

        // Nothing to do for now.
    }

    void Base::SetupServerReceivers(Framework::Networking::NetworkPeer *net, ServerReceiverHandler *handler) {
        using namespace Framework::Networking::Messages;
        auto r = net->router();
        r.on<GameSyncEntityUpdate>().handle(handler, &ServerReceiverHandler::OnEntityUpdate);
    }

    void Base::SetupClientReceivers(Framework::Networking::NetworkPeer *net, ClientReceiverHandler *handler) {
        using namespace Framework::Networking::Messages;
        auto r = net->router();
        r.on<GameSyncEntitySpawn>().handle(handler, &ClientReceiverHandler::OnEntitySpawn);
        r.on<GameSyncEntityDespawn>().handle(handler, &ClientReceiverHandler::OnEntityDespawn);
        r.on<GameSyncEntityUpdate>().handle(handler, &ClientReceiverHandler::OnEntityUpdate);
        r.on<GameSyncEntityOwnerUpdate>().handle(handler, &ClientReceiverHandler::OnEntityOwnerUpdate);
        r.on<GameSyncEntitySelfUpdate>().handle(handler, &ClientReceiverHandler::OnEntitySelfUpdate);
    }
} // namespace Framework::World::Modules
