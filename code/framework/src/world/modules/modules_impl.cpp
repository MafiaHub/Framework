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

            const auto tr         = e.get_mut<World::Modules::Base::Transform>();
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
            const auto tr = e.get_mut<World::Modules::Base::Transform>();
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

            const auto tr = e.get_mut<World::Modules::Base::Transform>();
            *tr           = msg->GetTransform();

            const auto es = e.get_mut<World::Modules::Base::Streamable>();
            es->owner     = msg->GetOwner();
        });
        net->RegisterMessage<GameSyncEntityUpdate>(GameMessages::GAME_SYNC_ENTITY_OWNER_UPDATE, [worldEngine](SLNet::RakNetGUID guid, GameSyncEntityUpdate *msg) {
            if (!msg->Valid()) {
                return;
            }

            const auto e = worldEngine->GetEntityByServerID(msg->GetServerID());

            if (!e.is_alive()) {
                return;
            }
            const auto es = e.get_mut<World::Modules::Base::Streamable>();
            es->owner     = msg->GetOwner();
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
