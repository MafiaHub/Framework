/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <networking/rpc/client_identity.h>

#include <mafianet/types.h>
#include <v8.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Framework::Networking {
    class NetworkServer;
} // namespace Framework::Networking

namespace Framework::Integrations::Server::Scripting {
    class ServerScriptingModule;
} // namespace Framework::Integrations::Server::Scripting

namespace Framework::Integrations::Server {
    // What the gate settled for one connection. reason is the text the player is shown on a
    // refusal, and empty on an admission.
    struct AdmissionDecision {
        MafiaNet::RakNetGUID guid;
        bool admitted = false;
        std::string reason;
    };

    // The admission gate: holds a connection request while server scripts decide whether to let it
    // in. The request is MafiaNet's pending session -- neither side has reported a connection, the
    // player holds no slot, is not counted as online and has been sent nothing -- and the gate's
    // decision is what accepts or refuses it.
    //
    // Begin() raises "playerConnecting" with a PendingConnection. Every handler may return a
    // Promise, and the connection waits until all of them settle: settled with no refusal admits,
    // connection.reject() refuses at once, a handler that throws or rejects refuses (the gate
    // fails closed, since it guards who gets in), and so does the deadline. connection.update()
    // shows the player a line of status and moves the deadline out, so a queue that keeps talking
    // is never timed out from under it.
    //
    // Decisions are collected by the instance once per tick, never acted on from inside a script
    // callback, so a refusal cannot close a connection under the handler still running for it.
    class ConnectionGate final {
      public:
        static constexpr const char *kEventName = "playerConnecting";

        // Longest refusal the player is shown; longer text is cut rather than refused, since the
        // decision itself must still reach them.
        static constexpr std::size_t kMaxReasonLength = 512;

        // Longest status line (PendingConnection.update()); one sentence on a connecting screen.
        static constexpr std::size_t kMaxStatusLength = 256;

        void Init(Scripting::ServerScriptingModule *scripting, Networking::NetworkServer *server, std::chrono::milliseconds timeout);

        // Start deciding on a connection request. False when nothing listens for playerConnecting, in
        // which case the caller admits at once.
        bool Begin(MafiaNet::RakNetGUID guid, const Networking::RPC::ClientIdentity &identity, const std::string &address);

        bool IsPending(uint64_t guid) const;

        // PendingConnection.reject(). No-op once the connection is decided or gone.
        void Refuse(uint64_t guid, std::string reason);

        // PendingConnection.update(). False once the connection is decided or gone.
        bool SendStatus(uint64_t guid, std::string message);

        // The request was abandoned while it waited (the client left, or MafiaNet timed it out): forget it
        // without a decision.
        void Drop(uint64_t guid);

        // Settle every connection whose handlers finished, that a script refused, or whose
        // deadline passed. Appends to out; the caller admits or refuses.
        void Collect(std::vector<AdmissionDecision> &out);

        // Release every held Promise. Must run while the scripting engine is still alive.
        void Shutdown();

      private:
        struct Pending {
            MafiaNet::RakNetGUID guid;
            std::string nickname; // for the log lines only
            v8::Global<v8::Promise> verdict;
            std::chrono::steady_clock::time_point deadline;
            std::optional<std::string> refusal;
        };

        // The handler failures behind a rejected verdict, one line each, for the server log.
        static std::string DescribeRejection(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> reason);

        Scripting::ServerScriptingModule *_scripting = nullptr;
        Networking::NetworkServer *_server           = nullptr;
        std::chrono::milliseconds _timeout {30000};
        std::unordered_map<uint64_t, Pending> _pending;
    };
} // namespace Framework::Integrations::Server
