/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "network_server.h"

#include <BitStream.h>
#include <MessageIdentifiers.h>
#include <logging/logger.h>

namespace Framework::Networking {
    ServerError NetworkServer::Init(int32_t port, const std::string &host, int32_t maxPlayers, const std::string &password) const {
        auto newSocketSd                  = SLNet::SocketDescriptor((uint16_t)port, host.c_str());
        const SLNet::StartupResult result = _peer->Startup(maxPlayers, &newSocketSd, 1);
        if (result != SLNet::RAKNET_STARTED) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->critical("Failed to init the networking peer. Reason: {}", GetStartupResultString((uint8_t)result));
            return SERVER_PEER_FAILED;
        }

        if (!password.empty()) {
            _peer->SetIncomingPassword(password.c_str(), (uint32_t)password.length());
            Logging::GetInstance()->Get(FRAMEWORK_INNER_NETWORKING)->debug("Applying incoming password to networking peer");
        }

        _peer->SetMaximumIncomingConnections((uint16_t)maxPlayers);
        return SERVER_NONE;
    }

    bool NetworkServer::HandlePacket(uint8_t packetID, SLNet::Packet *packet) {
        switch (packetID) {
        case ID_NEW_INCOMING_CONNECTION: {
            Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_NETWORKING)->debug("Incoming connection request {}", packet->guid.ToString());
            if (_onPlayerConnectCallback) {
                _onPlayerConnectCallback(packet);
            }
            return true;
        };

        case ID_DISCONNECTION_NOTIFICATION: {
            Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_NETWORKING)->debug("Disconnection from {}", packet->guid.ToString());
            if (_onPlayerDisconnectCallback) {
                _onPlayerDisconnectCallback(_packet, Messages::GRACEFUL_SHUTDOWN);
            }
            return true;
        };
        case ID_CONNECTION_LOST: {
            Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_NETWORKING)->debug("Connection lost for {}", packet->guid.ToString());
            if (_onPlayerDisconnectCallback) {
                _onPlayerDisconnectCallback(_packet, Messages::LOST);
            }
            return true;
        };
        default: break;
        }
        return false;
    }

    ServerError NetworkServer::Shutdown() const {
        if (!_peer) {
            return SERVER_PEER_NULL;
        }

        _peer->Shutdown(1000);
        //        SLNet::RakPeerInterface::DestroyInstance(_peer);
        return SERVER_NONE;
    }

    int NetworkServer::GetPing(SLNet::RakNetGUID guid) const {
        return _peer->GetAveragePing(guid);
    }
    bool NetworkServer::SendGameRPCInternal(SLNet::BitStream &bs, Framework::World::ServerEngine *world, flecs::entity_t ent_id, SLNet::RakNetGUID guid, SLNet::RakNetGUID excludeGUID, PacketPriority priority, PacketReliability reliability) const {
        const auto ent = world->WrapEntity(ent_id);

        if (!ent.is_alive()) {
            return false;
        }

        const auto streamers = world->FindVisibleStreamers(ent);

        for (const auto &streamer_ent : streamers) {
            const auto streamer = streamer_ent.get<World::Modules::Base::Streamer>();
            if (streamer->guid != guid.g && guid.g != SLNet::UNASSIGNED_RAKNET_GUID.g) {
                continue;
            }
            if (streamer->guid == excludeGUID.g) {
                continue;
            }
            _peer->Send(&bs, priority, reliability, 0, SLNet::RakNetGUID(streamer->guid), false);
        }

        return true;
    }
} // namespace Framework::Networking
