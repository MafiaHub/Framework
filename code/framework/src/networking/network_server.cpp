/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "network_server.h"

#include "channels.h"
#include "replication/network_entity.h"
#include "replication/replication_manager.h"
#include "replication/world.h"
#include "rpc/decline_simulation.h"

#include <logging/logger.h>
#include <mafianet/bit_stream.h>
#include <mafianet/defines.h>
#include <mafianet/guid_util.h>
#include <mafianet/message_identifiers.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace Framework::Networking {
    namespace {
        // How long Shutdown() blocks to flush the disconnection notification to connected peers, in
        // ms. Larger than the client's because the server broadcasts to every connection.
        constexpr unsigned int kShutdownBlockDurationMs = 1000;
    } // namespace

    uint64_t Sender::Avatar() const {
        return _server->GetReplicationManager()->GetViewerId(Guid());
    }

    void Sender::Drop(std::string_view reason) const {
        Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->warn("Dropping {}: {}", MafiaNet::to_string(_guid), reason);
        _server->KickPlayer(_guid, reason.empty() ? DisconnectionReason::KICKED : DisconnectionReason::KICKED_CUSTOM, std::string(reason));
    }

    void NetworkServer::RegisterRawRPC(RPC::Identifier identifier, fu2::function<void(MafiaNet::BitStream *, Sender, Replication::World &)> handler) {
        NetworkPeer::RegisterRawRPC(identifier, [this, identifier, cb = std::move(handler)](MafiaNet::BitStream *bs, MafiaNet::Packet *packet) mutable {
            // Shutting down: no handler starts once the drain was told to stop.
            if (DrainStopped()) {
                return;
            }
            const auto start = std::chrono::steady_clock::now();
            cb(bs, Sender(*this, packet->guid), _replicationManager->GetWorld());
            NoteHandlerTime(identifier, std::chrono::steady_clock::now() - start);
        });
    }

    void NetworkServer::NoteHandlerTime(RPC::Identifier identifier, std::chrono::steady_clock::duration elapsed) {
        const int32_t warnMs = _slowHandlerWarnMs.load(std::memory_order_relaxed);
        if (warnMs <= 0 || elapsed < std::chrono::milliseconds(warnMs)) {
            return;
        }
        // A slow handler holds up the packets behind it and nothing else; one that is always slow
        // would otherwise write a line per packet.
        ++_slowHandlers;
        const auto now = std::chrono::steady_clock::now();
        if (now < _nextSlowHandlerWarning) {
            return;
        }
        Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->warn("Packet handler for RPC {:016x} took {:.0f} ms against a {} ms budget ({} slow handler(s) since the last warning)", identifier.digest, std::chrono::duration<double, std::milli>(elapsed).count(), warnMs, _slowHandlers);
        _slowHandlers           = 0;
        _nextSlowHandlerWarning = now + std::chrono::seconds(1);
    }

    void NetworkServer::NoteShortPayload(RPC::Identifier identifier, MafiaNet::RakNetGUID guid) {
        ++_shortPayloads;
        const auto now = std::chrono::steady_clock::now();
        if (now < _nextShortPayloadWarning) {
            return;
        }
        Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->warn("Dropped {} RPC(s) whose payload did not read whole, last one RPC {:016x} from {}", _shortPayloads, identifier.digest, guid.g);
        _shortPayloads           = 0;
        _nextShortPayloadWarning = now + std::chrono::seconds(1);
    }

    void NetworkServer::Update() {
        if (!_peer) {
            return;
        }
        // Rebuild the spatial index before ReplicaManager3 computes per-connection relevance, then
        // send this tick's state-bag changes — the flush asks each connection what it has
        // constructed, so it reads the relevance this rebuild just settled.
        _replicationManager->RebuildInterest();
        // After the rebuild and before the flush: the election reads the positions this tick
        // settled, and a handover it decides is an ownership change the flush then sees, so an
        // owner-scoped bag key travels to the peer that has just been given the entity rather
        // than to the one that had it a moment ago.
        _replicationManager->Delegation().Update();
        _replicationManager->FlushStateBags();
        _replicationManager->Update();
        // What the pass put off while it held an entity: a destruction or a state change made from
        // inside one of its callbacks.
        Replication::World::Settle();
    }

    Utils::Result<void, Error> NetworkServer::Init(const std::string &host, int32_t port, int32_t maxPlayers, const std::string &password, const AdmissionSettings &admission) {
        if (port <= 0 || port > 65535) {
            return Error("Invalid server port: " + std::to_string(port));
        }
        if (maxPlayers <= 0 || maxPlayers > 65535) {
            return Error("Invalid maxPlayers: " + std::to_string(maxPlayers));
        }
        // Every connection request waits for the admission gate. Waiting ones are MafiaNet pending
        // sessions: a pool beside the players' slots, so the peer is started with room for both.
        _peer->SetSessionConfigInteractive(true);
        _peer->SetMaximumPendingSessions(admission.pendingConnections, admission.pendingConnectionsPerAddress);
        _peer->SetSessionTimeout(admission.sessionTimeoutMs);

        auto newSocketSd                     = MafiaNet::SocketDescriptor((uint16_t)port, host.c_str());
        const MafiaNet::StartupResult result = _peer->Startup(static_cast<unsigned int>(maxPlayers) + admission.pendingConnections, &newSocketSd, 1);
        if (result != MafiaNet::RAKNET_STARTED) {
            return Error(std::string("Failed to start networking peer: ") + GetStartupResultString((uint8_t)result));
        }

        if (!password.empty()) {
            _peer->SetIncomingPassword(password.c_str(), (uint32_t)password.length());
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Applying incoming password to networking peer");
        }

        _peer->SetMaximumIncomingConnections((uint16_t)maxPlayers);

        RegisterBuildToken();

        // Run replication as the authoritative server.
        _replicationManager->Init(this, true);
        if (!std::exchange(_replicationRPCsRegistered, true)) {
            RegisterRPC<RPC::DeclineSimulation>([this](const RPC::DeclineSimulation &payload, Sender sender, Replication::World &) {
                _replicationManager->Delegation().SetDeclined(payload.networkId, sender.Guid(), payload.declined);
            });
        }

        // Gate replication behind the handshake: don't auto-create the connection on connect (see
        // PushReplicationConnection). autoDestroy stays on so dropped peers are torn down.
        _replicationManager->SetAutoManageConnections(false, true);

        _initialized = true;
        return {};
    }

    bool NetworkServer::SetSessionConfig(std::string payload) {
        if (payload.size() > MAXIMUM_SESSION_CONFIG_SIZE) {
            // Truncating would hand clients a payload that doesn't parse; refuse instead.
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->error("Session payload is {} bytes, over the {} byte handshake limit; not published", payload.size(), MAXIMUM_SESSION_CONFIG_SIZE);
            return false;
        }

        _sessionConfig = std::move(payload);
        if (_peer) {
            _peer->SetSessionConfig(_sessionConfig.c_str(), static_cast<unsigned int>(_sessionConfig.size()));
        }
        return true;
    }

    bool NetworkServer::HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) {
        switch (packetID) {
        // A client asks to join. MafiaNet holds the connection unreported on both sides until it is
        // answered; the payload after the id is the client's identity.
        case ID_SESSION_CONFIG_REQUEST: {
            const unsigned int offset                         = static_cast<unsigned int>(_packetDataOffset) + 1;
            const std::string_view payload                    = packet->length > offset ? std::string_view(reinterpret_cast<const char *>(packet->data) + offset, packet->length - offset) : std::string_view();
            const std::optional<RPC::ClientIdentity> identity = RPC::ClientIdentity::Decode(payload);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Connection request from {}", MafiaNet::to_string(packet->guid));
            if (_onSessionRequestCallback) {
                _onSessionRequestCallback(packet->guid, identity);
            }
            else {
                AcceptSession(packet->guid);
            }
            return true;
        };
        case ID_SESSION_CONFIG_ABANDONED: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Connection request from {} abandoned", MafiaNet::to_string(packet->guid));
            {
                const std::lock_guard guard(_clientsMutex);
                _peerIdentities.erase(packet->guid.g);
            }
            if (_onSessionAbandonedCallback) {
                _onSessionAbandonedCallback(packet->guid);
            }
            return true;
        };

        case ID_NEW_INCOMING_CONNECTION: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Incoming connection request {}", MafiaNet::to_string(packet->guid));
            if (_onPlayerConnectCallback) {
                _onPlayerConnectCallback(packet);
            }
            return true;
        };

        case ID_DISCONNECTION_NOTIFICATION: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Disconnection from {}", MafiaNet::to_string(packet->guid));
            if (_onPlayerDisconnectCallback) {
                _onPlayerDisconnectCallback(_packet, DisconnectionReason::GRACEFUL_SHUTDOWN, "");
            }
            ClearClientState(packet->guid);
            return true;
        };
        case ID_CONNECTION_LOST: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Connection lost for {}", MafiaNet::to_string(packet->guid));
            if (_onPlayerDisconnectCallback) {
                _onPlayerDisconnectCallback(_packet, DisconnectionReason::LOST, "");
            }
            ClearClientState(packet->guid);
            return true;
        };

        // Build gate: the client challenges us with its build token. Match -> asset phase; mismatch
        // -> drop (the version-incompatibility path).
        case ID_TWO_WAY_AUTHENTICATION_INCOMING_CHALLENGE_SUCCESS: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Build verified for {}", MafiaNet::to_string(packet->guid));
            {
                const std::lock_guard guard(_clientsMutex);
                _authenticatedClients.insert(packet->guid.g);
            }
            if (_onClientAuthenticatedCallback) {
                _onClientAuthenticatedCallback(packet->guid);
            }
            return true;
        };
        case ID_TWO_WAY_AUTHENTICATION_INCOMING_CHALLENGE_FAILURE: {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->warn("Build mismatch from {}, dropping peer", MafiaNet::to_string(packet->guid));
            _peer->CloseConnection(packet->guid, true);
            return true;
        };

        case ID_READY_EVENT_SET: {
            MafiaNet::BitStream bs(_packet->data + _packetDataOffset, _packet->length - _packetDataOffset, false);
            bs.IgnoreBytes(sizeof(MafiaNet::MessageID));
            int eventId = 0;
            if (bs.Read(eventId) && _onConnectionReadyCallback) {
                _onConnectionReadyCallback(eventId, packet->guid);
            }
            return true;
        };

        case ID_READY_EVENT_UNSET:
        case ID_READY_EVENT_ALL_SET:
        case ID_READY_EVENT_QUERY:
        case ID_READY_EVENT_FORCE_ALL_SET: return true;

        default: break;
        }
        return false;
    }

    void NetworkServer::Shutdown() {
        if (!_peer) {
            return;
        }

        _peer->Shutdown(kShutdownBlockDurationMs);
        MafiaNet::RakPeerInterface::DestroyInstance(_peer);
        _peer = nullptr;
        Lifecycle::Shutdown();
    }

    int NetworkServer::GetPing(MafiaNet::RakNetGUID guid) const {
        return _peer->GetAveragePing(guid);
    }
    std::string NetworkServer::GetAddress(MafiaNet::RakNetGUID guid) const {
        const auto address = _peer->GetSystemAddressFromGuid(guid);
        if (address == MafiaNet::UNASSIGNED_SYSTEM_ADDRESS) {
            return "";
        }
        char buffer[64] = {0};
        address.ToString(false, buffer, sizeof(buffer));
        return buffer;
    }
    void NetworkServer::SignalExcept(RPC::Identifier identifier, MafiaNet::BitStream &bs, MafiaNet::RakNetGUID excludeGUID, MafiaNet::Priority priority, MafiaNet::Reliability reliability, Channel channel) {
        // When broadcasting, the system identifier is the peer to exclude, so a single Signal reaches
        // everyone but the sender.
        _rpc.Signal(ResolveIdentifier(identifier), &bs, priority, reliability, ToOrderingChannel(channel), excludeGUID, true, false);
    }

    void NetworkServer::PushReplicationConnection(MafiaNet::RakNetGUID guid) {
        if (!_replicationManager) {
            return;
        }
        _replicationManager->StartReplication(_peer->GetSystemAddressFromGuid(guid), guid);
    }

    void NetworkServer::AcceptSession(MafiaNet::RakNetGUID guid) {
        _peer->AcceptSession(guid, _sessionConfig.data(), static_cast<unsigned int>(_sessionConfig.size()));
    }

    void NetworkServer::RejectSession(MafiaNet::RakNetGUID guid, const std::string &reason) {
        {
            const std::lock_guard guard(_clientsMutex);
            _peerIdentities.erase(guid.g);
        }
        _peer->RejectSession(guid, reason.c_str());
    }

    void NetworkServer::SendSessionStatus(MafiaNet::RakNetGUID guid, const std::string &status) {
        _peer->SendSessionStatus(guid, status.data(), static_cast<unsigned int>(std::min<size_t>(status.size(), MAXIMUM_SESSION_CONFIG_SIZE)));
    }

    uint32_t NetworkServer::GetPlayerCount() const {
        return _peer->NumberOfConnections();
    }

    void NetworkServer::KickPlayer(MafiaNet::RakNetGUID guid, DisconnectionReason reason, const std::string &customReason) {
        DisconnectPayload payload;
        payload.reason       = static_cast<uint32_t>(reason);
        payload.customReason = customReason;

        // Reason rides the disconnect notification, so no separate message races the close.
        MafiaNet::BitStream reasonData;
        payload.Serialize(&reasonData, true);
        _peer->CloseConnection(guid, true, 0, MafiaNet::Priority::Low, &reasonData);
        ClearClientState(guid);
    }

    void NetworkServer::ClearClientState(MafiaNet::RakNetGUID guid) {
        const std::lock_guard guard(_clientsMutex);
        _authenticatedClients.erase(guid.g);
        _peerIdentities.erase(guid.g);
        // Recycles the slot for reconnects, before the next packet is handled: so before any from a
        // connection that takes the slot over.
        _staleReadyEvents.push_back(ReadyEventId(guid));
        _anyStaleReadyEvents.store(true, std::memory_order_release);
    }

    void NetworkServer::DeleteStaleReadyEvents() {
        if (!_anyStaleReadyEvents.load(std::memory_order_acquire)) {
            return;
        }
        std::vector<int> stale;
        {
            const std::lock_guard guard(_clientsMutex);
            stale.swap(_staleReadyEvents);
            _anyStaleReadyEvents.store(false, std::memory_order_release);
        }
        for (const int eventId : stale) {
            _readyEvent.DeleteEvent(eventId);
        }
    }
} // namespace Framework::Networking
