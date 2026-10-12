/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "connection_admission_ut.h"

#include "core_modules.h"
#include "integrations/server/connection_gate.h"
#include "integrations/server/scripting/module.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"
#include "scripting/builtins/player.h"
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// The admission gate against a real Node engine and a real resource: what a playerConnecting
// handler does -- return, reject, throw, await, hang -- and what the gate decides for it. The
// decisions are what the instance acts on. A loopback admission also checks the joined player's
// script-visible identity.
class ConnectionGateTestRig {
  public:
    // Register an admitted peer's avatar for the same owner-guid lookup used by server scripts.
    struct JoinedPlayer {
        Framework::Networking::NetworkServer &server;
        Framework::Networking::Replication::NetworkEntity avatar;

        JoinedPlayer(Framework::Networking::NetworkServer &peer, MafiaNet::RakNetGUID guid): server(peer) {
            avatar.SetNetworkID(1);
            avatar.ownerGUID = MafiaNet::ToPeerGuid(guid);
            server.GetReplicationManager()->Reference(&avatar);
            server.GetReplicationManager()->SetViewer(avatar.ownerGUID, &avatar);
            Framework::CoreModules::SetNetworkPeer(&server);
            Framework::CoreModules::SetReplication(server.GetReplicationManager());
        }

        ~JoinedPlayer() {
            Framework::CoreModules::SetReplication(nullptr);
            Framework::CoreModules::SetNetworkPeer(nullptr);
            server.GetReplicationManager()->ClearViewer(avatar.ownerGUID);
            server.GetReplicationManager()->Dereference(&avatar);
        }
    };

    static std::string ResourcesPath() {
#ifdef _WIN32
        const char *temp = std::getenv("TEMP");
        if (!temp) temp = std::getenv("TMP");
        if (!temp) temp = "C:\\Temp";
        return std::string(temp) + "\\framework_gate_test_resources";
#else
        return "/tmp/framework_gate_test_resources";
#endif
    }

    static void WriteResource() {
        Cleanup();
        cppfs::fs::open(ResourcesPath()).createDirectory();
        cppfs::fs::open(ResourcesPath() + "/gate").createDirectory();

        std::ofstream(ResourcesPath() + "/gate/package.json") << R"({ "name": "gate", "version": "1.0.0", "mafiahub": { "server": "main.js" } })";
        std::ofstream(ResourcesPath() + "/gate/main.js") << R"(
            const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
            Events.on("playerConnecting", async (connection) => {
                globalThis.__seen = `${connection.nickname}|${connection.ticket}|${connection.ip}|${connection.steamId}`;
                switch (connection.nickname) {
                case "admit": return;
                case "refuse": connection.reject("Not on the whitelist"); return;
                case "refuse-default": connection.reject(); return;
                case "refuse-long": connection.reject("x".repeat(600)); return;
                case "throw": throw new Error("database is down");
                case "async-admit": await sleep(20); return;
                case "async-refuse": await sleep(20); connection.reject("Banned until Friday"); return;
                case "hang": await new Promise(() => {}); return;
                case "probe":
                    globalThis.__probe = connection;
                    globalThis.__pendingDuring = connection.isPending() ? 1 : 0;
                    return;
                case "epic-whitelist":
                    globalThis.__epicId = connection.epicId;
                    if (connection.epicId !== "0123456789abcdef0123456789abcdef") {
                        connection.reject("Your Epic account is not on the whitelist.");
                    }
                    return;
                }
            });
        )";
    }

    static void Cleanup() {
        cppfs::FileHandle dir = cppfs::fs::open(ResourcesPath());
        if (dir.exists()) {
            dir.removeDirectoryRec();
        }
    }

    // The handler under test runs in the gate resource's own runtime, so that is where these evaluate.
    static int32_t EvalInt(Framework::Scripting::NodeEngine *engine, const char *source) {
        v8::Isolate *isolate = engine->GetResourceRuntime("gate")->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);
        v8::TryCatch tryCatch(isolate);
        v8::Local<v8::Script> script;
        v8::Local<v8::Value> result;
        if (!v8::Script::Compile(context, v8::String::NewFromUtf8(isolate, source).ToLocalChecked()).ToLocal(&script) || !script->Run(context).ToLocal(&result) || !result->IsNumber()) {
            return -1;
        }
        return result->Int32Value(context).FromMaybe(-1);
    }

    static std::string EvalString(Framework::Scripting::NodeEngine *engine, const char *source) {
        v8::Isolate *isolate = engine->GetResourceRuntime("gate")->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);
        v8::TryCatch tryCatch(isolate);
        v8::Local<v8::Script> script;
        v8::Local<v8::Value> result;
        if (!v8::Script::Compile(context, v8::String::NewFromUtf8(isolate, source).ToLocalChecked()).ToLocal(&script) || !script->Run(context).ToLocal(&result) || !result->IsString()) {
            return {};
        }
        return *v8::String::Utf8Value(isolate, result);
    }

    // Tick the engine the way the server does until the gate settles something, or give up.
    static std::vector<Framework::Integrations::Server::AdmissionDecision> Settle(Framework::Integrations::Server::Scripting::ServerScriptingModule &module, Framework::Integrations::Server::ConnectionGate &gate, int maxMs = 1000) {
        std::vector<Framework::Integrations::Server::AdmissionDecision> out;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(maxMs);
        while (out.empty() && std::chrono::steady_clock::now() < deadline) {
            module.Update();
            gate.Collect(out);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return out;
    }

    static MafiaNet::RakNetGUID Guid(uint64_t value) {
        MafiaNet::RakNetGUID guid;
        guid.g           = value;
        guid.systemIndex = static_cast<MafiaNet::SystemIndex>(value & 0xFF);
        return guid;
    }

    static Framework::Networking::RPC::ClientIdentity Identity(const char *nickname) {
        Framework::Networking::RPC::ClientIdentity identity;
        identity.name    = nickname;
        identity.steamId = "76561198000000000";
        identity.ticket  = "one-time-ticket";
        return identity;
    }
};

MODULE(connection_gate, {
    using Rig = ConnectionGateTestRig;
    using Framework::Integrations::Server::AdmissionDecision;
    using Framework::Integrations::Server::ConnectionGate;
    using Framework::Integrations::Server::Scripting::ServerScriptingModule;

    Rig::WriteResource();

    ServerScriptingModule module;
    module.SetResourcesPath(Rig::ResourcesPath());
    const bool moduleReady = module.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE;

    ConnectionGate gate;
    gate.Init(&module, nullptr, std::chrono::milliseconds(200));
    Framework::CoreModules::SetConnectionGate(&gate);

    IT("brings up a server scripting module", {
        EQUALS(moduleReady, true);
    });

    if (moduleReady) {
        IT("admits at once when nothing listens for playerConnecting", {
            EQUALS(gate.Begin(Rig::Guid(1), Rig::Identity("admit"), "127.0.0.1"), false);
            EQUALS(gate.IsPending(1), false);
        });

        auto *resources         = module.GetResourceManager();
        const size_t discovered = resources->DiscoverResources();
        const bool started      = discovered == 1 && static_cast<bool>(resources->StartResource("gate"));

        IT("starts the resource holding the handler", {
            EQUALS(discovered, 1u);
            EQUALS(started, true);
        });

        IT("admits a connection whose handler returns", {
            EQUALS(gate.Begin(Rig::Guid(2), Rig::Identity("admit"), "10.0.0.2"), true);
            EQUALS(gate.IsPending(2), true);
            const auto decisions = Rig::Settle(module, gate);
            EQUALS(decisions.size(), 1u);
            EQUALS(decisions[0].guid.g, 2ull);
            EQUALS(decisions[0].admitted, true);
            EQUALS(gate.IsPending(2), false);
        });

        IT("hands the handler the identity, the ticket and the address", {
            STREQUALS(Rig::EvalString(module.GetEngine(), "globalThis.__seen").c_str(), "admit|one-time-ticket|10.0.0.2|76561198000000000");
        });

        IT("admits an Epic whitelist entry using the client-reported ID without a ticket", {
            auto identity = Rig::Identity("epic-whitelist");
            identity.steamId.clear();
            identity.ticket.clear();
            identity.epicId = "0123456789abcdef0123456789abcdef";
            EQUALS(gate.Begin(Rig::Guid(12), identity, "10.0.0.12"), true);
            const auto decisions = Rig::Settle(module, gate);
            EQUALS(decisions.size(), 1u);
            EQUALS(decisions[0].admitted, true);
            STREQUALS(Rig::EvalString(module.GetEngine(), "globalThis.__epicId").c_str(), identity.epicId.c_str());
        });

        IT("exposes the admitted peer's Epic ID through script-visible Player.epicId after joining", {
            ConnectionAdmissionRig network;
            EQUALS(network.StartServer(true), true);
            const char *epicId = "0123456789abcdef0123456789abcdef";
            EQUALS(network.Connect(ConnectionAdmissionRig::IdentityPayload("epic-whitelist", "", epicId)), true);
            EQUALS(network.PumpUntil([&] { return !network.seen.requests.empty(); }), true);
            EQUALS(network.seen.requests[0].has_value(), true);

            const auto guid      = network.seen.requestGuids[0];
            const auto &identity = *network.seen.requests[0];
            // The instance retains the received identity before running playerConnecting.
            network.server->SetPeerIdentity(guid, identity);
            EQUALS(gate.Begin(guid, identity, network.server->GetAddress(guid)), true);
            const auto decisions = Rig::Settle(module, gate);
            EQUALS(decisions.size(), 1u);
            EQUALS(decisions[0].guid.g, guid.g);
            EQUALS(decisions[0].admitted, true);
            network.server->AcceptSession(decisions[0].guid);
            EQUALS(network.PumpUntil([&] { return network.seen.clientConnected && network.server->GetPlayerCount() == 1u; }), true);
            EQUALS(network.seen.clientDisconnected, false);

            Rig::JoinedPlayer player(*network.server, guid);
            auto *engine        = module.GetEngine();
            v8::Isolate *isolate = engine->GetResourceRuntime("gate")->GetIsolate();
            {
                v8::Locker locker(isolate);
                v8::Isolate::Scope isolateScope(isolate);
                v8::HandleScope handleScope(isolate);
                v8::Local<v8::Context> context = engine->GetContext();
                v8::Context::Scope contextScope(context);
                Framework::Scripting::Builtins::Player::Register(isolate, context->Global());
            }
            STREQUALS(Rig::EvalString(engine, "new Player(1).epicId").c_str(), epicId);
        });

        IT("lets an Epic whitelist refuse a connection without an Epic ID", {
            EQUALS(gate.Begin(Rig::Guid(13), Rig::Identity("epic-whitelist"), "10.0.0.13"), true);
            const auto decisions = Rig::Settle(module, gate);
            EQUALS(decisions.size(), 1u);
            EQUALS(decisions[0].admitted, false);
            STREQUALS(decisions[0].reason.c_str(), "Your Epic account is not on the whitelist.");
            STREQUALS(Rig::EvalString(module.GetEngine(), "globalThis.__epicId").c_str(), "");
        });

        IT("lets an Epic whitelist refuse a nonempty ID that is not listed", {
            auto identity = Rig::Identity("epic-whitelist");
            identity.steamId.clear();
            identity.ticket.clear();
            identity.epicId = "fedcba9876543210fedcba9876543210";
            EQUALS(gate.Begin(Rig::Guid(14), identity, "10.0.0.14"), true);
            const auto decisions = Rig::Settle(module, gate);
            EQUALS(decisions.size(), 1u);
            EQUALS(decisions[0].admitted, false);
            STREQUALS(decisions[0].reason.c_str(), "Your Epic account is not on the whitelist.");
            STREQUALS(Rig::EvalString(module.GetEngine(), "globalThis.__epicId").c_str(), identity.epicId.c_str());
        });

        IT("refuses with the script's reason", {
            EQUALS(gate.Begin(Rig::Guid(3), Rig::Identity("refuse"), "10.0.0.3"), true);
            const auto decisions = Rig::Settle(module, gate);
            EQUALS(decisions.size(), 1u);
            EQUALS(decisions[0].admitted, false);
            STREQUALS(decisions[0].reason.c_str(), "Not on the whitelist");
        });

        IT("refuses with a generic reason when the script gives none", {
            EQUALS(gate.Begin(Rig::Guid(4), Rig::Identity("refuse-default"), "10.0.0.4"), true);
            const auto decisions = Rig::Settle(module, gate);
            EQUALS(decisions.size(), 1u);
            EQUALS(decisions[0].admitted, false);
            STREQUALS(decisions[0].reason.c_str(), "The server refused the connection.");
        });

        IT("cuts an overlong reason rather than dropping it", {
            EQUALS(gate.Begin(Rig::Guid(5), Rig::Identity("refuse-long"), "10.0.0.5"), true);
            const auto decisions = Rig::Settle(module, gate);
            EQUALS(decisions.size(), 1u);
            EQUALS(decisions[0].admitted, false);
            EQUALS(decisions[0].reason.size(), ConnectionGate::kMaxReasonLength);
        });

        IT("fails closed when a handler throws", {
            EQUALS(gate.Begin(Rig::Guid(6), Rig::Identity("throw"), "10.0.0.6"), true);
            const auto decisions = Rig::Settle(module, gate);
            EQUALS(decisions.size(), 1u);
            EQUALS(decisions[0].admitted, false);
            // The player is not shown the script's error.
            EQUALS(decisions[0].reason.find("database") == std::string::npos, true);
        });

        IT("waits for an async handler before admitting", {
            EQUALS(gate.Begin(Rig::Guid(7), Rig::Identity("async-admit"), "10.0.0.7"), true);
            const auto decisions = Rig::Settle(module, gate);
            EQUALS(decisions.size(), 1u);
            EQUALS(decisions[0].admitted, true);
        });

        IT("takes a refusal made after an await", {
            EQUALS(gate.Begin(Rig::Guid(8), Rig::Identity("async-refuse"), "10.0.0.8"), true);
            const auto decisions = Rig::Settle(module, gate);
            EQUALS(decisions.size(), 1u);
            EQUALS(decisions[0].admitted, false);
            STREQUALS(decisions[0].reason.c_str(), "Banned until Friday");
        });

        IT("refuses a connection whose handler never settles once the timeout passes", {
            const auto begun = std::chrono::steady_clock::now();
            EQUALS(gate.Begin(Rig::Guid(9), Rig::Identity("hang"), "10.0.0.9"), true);
            const auto decisions = Rig::Settle(module, gate, 2000);
            EQUALS(decisions.size(), 1u);
            EQUALS(decisions[0].admitted, false);
            EQUALS(std::chrono::steady_clock::now() - begun >= std::chrono::milliseconds(200), true);
        });

        IT("forgets a player who leaves while waiting", {
            EQUALS(gate.Begin(Rig::Guid(10), Rig::Identity("hang"), "10.0.0.10"), true);
            gate.Drop(10);
            EQUALS(gate.IsPending(10), false);
            EQUALS(Rig::Settle(module, gate, 400).empty(), true);
        });

        IT("reports pending while undecided and not after", {
            EQUALS(gate.Begin(Rig::Guid(11), Rig::Identity("probe"), "10.0.0.11"), true);
            EQUALS(Rig::EvalInt(module.GetEngine(), "globalThis.__pendingDuring"), 1);
            EQUALS(Rig::Settle(module, gate).size(), 1u);
            EQUALS(Rig::EvalInt(module.GetEngine(), "globalThis.__probe.isPending() ? 1 : 0"), 0);
            // A decided connection's calls do nothing rather than throw.
            EQUALS(Rig::EvalInt(module.GetEngine(), "globalThis.__probe.reject('late'); 7"), 7);
        });

        resources->StopAll();
    }

    // Before the module's destructor takes the engine the gate's Promises belong to.
    gate.Shutdown();
    Framework::CoreModules::SetConnectionGate(nullptr);
    Rig::Cleanup();
});
