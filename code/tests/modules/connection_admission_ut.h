/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/connection.h"
#include "networking/network_client.h"
#include "networking/network_server.h"
#include "networking/rpc/client_identity.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

// The admission handshake end to end over loopback, through the framework's own NetworkServer and
// NetworkClient: the identity a client sends in its connection request, the server's request callback,
// and each way a request ends -- refused with a reason, kept waiting with status then accepted, left by
// the client, accepted by default. No build token is involved: all of it happens before the connection
// exists, which is the point.
class ConnectionAdmissionRig {
  public:
    struct Observed {
        std::vector<std::optional<Framework::Networking::RPC::ClientIdentity>> requests;
        std::vector<MafiaNet::RakNetGUID> requestGuids;
        std::vector<uint64_t> abandoned;
        std::vector<std::string> statuses;
        bool clientConnected = false;
        bool clientDisconnected = false;
        Framework::Networking::DisconnectionReason reason = Framework::Networking::DisconnectionReason::UNKNOWN;
        std::string customReason;
    };

    std::unique_ptr<Framework::Networking::NetworkServer> server = std::make_unique<Framework::Networking::NetworkServer>();
    std::unique_ptr<Framework::Networking::NetworkClient> client = std::make_unique<Framework::Networking::NetworkClient>();
    Observed seen;
    int32_t port = 0;

    // Starts the server on a free-looking loopback port, retrying a few in case one is taken.
    bool StartServer(bool withRequestCallback, uint16_t pendingPerAddress = 4) {
        Framework::Networking::AdmissionSettings admission;
        admission.pendingConnections           = 4;
        admission.pendingConnectionsPerAddress = pendingPerAddress;
        admission.sessionTimeoutMs             = 20000;

        std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
        for (int attempt = 0; attempt < 8; ++attempt) {
            port = 42000 + static_cast<int32_t>(random() % 20000);
            if (server->Init("127.0.0.1", port, 2, "", admission)) {
                break;
            }
            server = std::make_unique<Framework::Networking::NetworkServer>();
            port   = 0;
        }
        if (port == 0) {
            return false;
        }

        // Once admitted, the client challenges the server's build as every instance does; both carry
        // the same token so an accepted connection stays up long enough to be observed.
        server->SetBuildToken(Framework::Networking::NetworkPeer::kBuildVerificationDisabledToken);
        client->SetBuildToken(Framework::Networking::NetworkPeer::kBuildVerificationDisabledToken);

        if (withRequestCallback) {
            server->SetOnSessionRequestCallback([this](MafiaNet::RakNetGUID guid, const std::optional<Framework::Networking::RPC::ClientIdentity> &identity) {
                seen.requests.push_back(identity);
                seen.requestGuids.push_back(guid);
            });
        }
        server->SetOnSessionAbandonedCallback([this](MafiaNet::RakNetGUID guid) {
            seen.abandoned.push_back(guid.g);
        });

        client->SetOnPlayerConnectedCallback([this](MafiaNet::Packet *) {
            seen.clientConnected = true;
        });
        client->SetOnPlayerDisconnectedCallback([this](MafiaNet::Packet *, Framework::Networking::DisconnectionReason reason, const std::string &customReason) {
            seen.clientDisconnected = true;
            seen.reason             = reason;
            seen.customReason       = customReason;
        });
        client->SetOnSessionStatusCallback([this](const std::string &status) {
            seen.statuses.push_back(status);
        });
        return true;
    }

    bool Connect(const std::string &sessionPayload) {
        return static_cast<bool>(client->Connect("127.0.0.1", port, "", sessionPayload));
    }

    static std::string IdentityPayload(const char *name, const char *ticket) {
        Framework::Networking::RPC::ClientIdentity identity;
        identity.name   = name;
        identity.ticket = ticket;
        return identity.Encode();
    }

    // Pumps both peers the way the instances do until the condition holds or the time runs out.
    template <typename Condition>
    bool PumpUntil(Condition condition, int maxMs = 10000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(maxMs);
        while (std::chrono::steady_clock::now() < deadline) {
            server->Update();
            client->Update();
            if (condition()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    // Keeps pumping for a fixed window: for the assertions that something must NOT happen.
    void PumpFor(int ms) {
        PumpUntil([] { return false; }, ms);
    }

    ~ConnectionAdmissionRig() {
        client.reset();
        server->Shutdown();
    }
};

MODULE(connection_admission, {
    using Rig = ConnectionAdmissionRig;
    using Framework::Networking::DisconnectionReason;

    IT("hands the server the identity and ticket the client connected with, before any connection", {
        Rig rig;
        EQUALS(rig.StartServer(true), true);
        EQUALS(rig.Connect(Rig::IdentityPayload("Jan", "tkt-abc+123")), true);

        EQUALS(rig.PumpUntil([&] { return !rig.seen.requests.empty(); }), true);
        EQUALS(rig.seen.requests[0].has_value(), true);
        STREQUALS(rig.seen.requests[0]->name.c_str(), "Jan");
        STREQUALS(rig.seen.requests[0]->ticket.c_str(), "tkt-abc+123");

        // Waiting: nobody is connected on either side, and the player count says so.
        rig.PumpFor(300);
        EQUALS(rig.seen.clientConnected, false);
        EQUALS(rig.server->GetPlayerCount(), 0u);
    });

    IT("reports a request that carried no identity as unidentified", {
        Rig rig;
        EQUALS(rig.StartServer(true), true);
        EQUALS(rig.Connect("{\"build\":\"someone else\"}"), true);

        EQUALS(rig.PumpUntil([&] { return !rig.seen.requests.empty(); }), true);
        EQUALS(rig.seen.requests[0].has_value(), false);
    });

    IT("refuses with the reason the client then reads, and nobody ever connects", {
        Rig rig;
        EQUALS(rig.StartServer(true), true);
        EQUALS(rig.Connect(Rig::IdentityPayload("Player2", "")), true);
        EQUALS(rig.PumpUntil([&] { return !rig.seen.requestGuids.empty(); }), true);

        rig.server->RejectSession(rig.seen.requestGuids[0], "You are not on this server's whitelist.");

        EQUALS(rig.PumpUntil([&] { return rig.seen.clientDisconnected; }), true);
        EQUALS(rig.seen.reason == DisconnectionReason::CONNECTION_REFUSED, true);
        STREQUALS(rig.seen.customReason.c_str(), "You are not on this server's whitelist.");
        EQUALS(rig.seen.clientConnected, false);
        EQUALS(rig.server->GetPlayerCount(), 0u);
        // A refusal is the server's own answer: it is not reported back to it as abandoned.
        rig.PumpFor(300);
        EQUALS(rig.seen.abandoned.empty(), true);
    });

    IT("delivers status while waiting, then connects when accepted", {
        Rig rig;
        EQUALS(rig.StartServer(true), true);
        EQUALS(rig.Connect(Rig::IdentityPayload("Queued", "")), true);
        EQUALS(rig.PumpUntil([&] { return !rig.seen.requestGuids.empty(); }), true);
        const MafiaNet::RakNetGUID guid = rig.seen.requestGuids[0];

        rig.server->SendSessionStatus(guid, "You are 2nd in the queue.");
        EQUALS(rig.PumpUntil([&] { return rig.seen.statuses.size() >= 1; }), true);
        rig.server->SendSessionStatus(guid, "You are 1st in the queue.");
        EQUALS(rig.PumpUntil([&] { return rig.seen.statuses.size() >= 2; }), true);
        STREQUALS(rig.seen.statuses[0].c_str(), "You are 2nd in the queue.");
        STREQUALS(rig.seen.statuses[1].c_str(), "You are 1st in the queue.");
        EQUALS(rig.seen.clientConnected, false);

        rig.server->AcceptSession(guid);
        EQUALS(rig.PumpUntil([&] { return rig.seen.clientConnected && rig.server->GetPlayerCount() == 1u; }), true);
        EQUALS(rig.seen.clientDisconnected, false);
    });

    IT("tells the server when a waiting client gives up", {
        Rig rig;
        EQUALS(rig.StartServer(true), true);
        EQUALS(rig.Connect(Rig::IdentityPayload("Impatient", "")), true);
        EQUALS(rig.PumpUntil([&] { return !rig.seen.requestGuids.empty(); }), true);
        const uint64_t guid = rig.seen.requestGuids[0].g;

        EQUALS(static_cast<bool>(rig.client->Disconnect()), true);

        EQUALS(rig.PumpUntil([&] { return !rig.seen.abandoned.empty(); }), true);
        EQUALS(rig.seen.abandoned[0], guid);
        EQUALS(rig.server->GetPlayerCount(), 0u);
    });

    IT("admits at once when nothing answers requests", {
        Rig rig;
        EQUALS(rig.StartServer(false), true);
        EQUALS(rig.Connect(Rig::IdentityPayload("Anyone", "")), true);
        EQUALS(rig.PumpUntil([&] { return rig.seen.clientConnected && rig.server->GetPlayerCount() == 1u; }), true);
    });

    IT("does not let one address hold more waiting requests than its share", {
        Rig rig;
        EQUALS(rig.StartServer(true, 1), true);
        EQUALS(rig.Connect(Rig::IdentityPayload("First", "")), true);
        EQUALS(rig.PumpUntil([&] { return !rig.seen.requestGuids.empty(); }), true);

        // A second client from the same address, while the first still waits.
        auto second = std::make_unique<Framework::Networking::NetworkClient>();
        second->SetBuildToken(Framework::Networking::NetworkPeer::kBuildVerificationDisabledToken);
        bool refused = false;
        DisconnectionReason reason = DisconnectionReason::UNKNOWN;
        second->SetOnPlayerDisconnectedCallback([&](MafiaNet::Packet *, DisconnectionReason why, const std::string &) {
            refused = true;
            reason  = why;
        });
        EQUALS(static_cast<bool>(second->Connect("127.0.0.1", rig.port, "", Rig::IdentityPayload("Second", ""))), true);
        EQUALS(rig.PumpUntil([&] {
            second->Update();
            return refused;
        }),
            true);
        EQUALS(reason == DisconnectionReason::NO_FREE_SLOT, true);
        EQUALS(rig.seen.requests.size(), 1u);
        second.reset();
    });
});
