/*
 * MafiaHub source-available license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE.txt file in the source repository for information regarding licensing.
 */

#pragma once

#include "integrations/server/instance.h"
#include "integrations/shared/rpc/emit_script_event.h"
#include "networking/network_client.h"
#include "networking/network_server.h"
#include "networking/replication/client_entities.h"
#include "networking/replication/entity_registry.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/world.h"
#include "networking/rpc/client_identity.h"
#include "networking/rpc/client_join.h"
#include "networking/rpc/rpc_identifier.h"
#include "networking/rpc/server_resources.h"
#include "scripting/builtins/entity.h"
#include "scripting/builtins/player.h"
#include "scripting/engine_scope.h"
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"
#include "utils/path.h"

#include <httplib.h>
#include <mafianet/bit_stream.h>
#include <mafianet/get_time.h>
#include <v8pp/convert.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Whole servers once more, with a client on loopback: what arrives from the wire is handled on the packet thread,
// beside the tick this thread runs and the scripts on theirs.
namespace ServerPacketThreadTest {
    using Clock = std::chrono::steady_clock;
    using Framework::Networking::Sender;
    using Framework::Networking::Replication::World;

    inline int32_t FreePort() {
        std::mt19937 random(static_cast<unsigned int>(Clock::now().time_since_epoch().count()));
        for (int attempt = 0; attempt < 8; ++attempt) {
            const int32_t candidate = 42000 + static_cast<int32_t>(random() % 20000);
            httplib::Server probe;
            if (probe.bind_to_port("127.0.0.1", candidate)) {
                probe.stop();
                return candidate;
            }
        }
        return 0;
    }

    // A player's avatar, owned by its client: `counter` is the client's to write and comes back up as entity state.
    struct Avatar final: Framework::Networking::Replication::NetworkEntity {
        static constexpr const char *kTypeName = "Test::PacketThread::Avatar";
        uint32_t counter                       = 0;
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(counter);
        }
    };

    // Something the server keeps changing, so every pass has it to serialize.
    struct Prop final: Framework::Networking::Replication::NetworkEntity {
        static constexpr const char *kTypeName = "Test::PacketThread::Prop";
        uint32_t spin                          = 0;
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(spin);
        }
    };

    struct Value {
        uint32_t value = 0;
        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, value);
        }
    };
    // Recorded with the thread that handled it.
    struct Note: Value {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Test::PacketThread::Note");
    };
    // Sleeps for `value` milliseconds.
    struct Slow: Value {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Test::PacketThread::Slow");
    };
    struct Ping: Value {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Test::PacketThread::Ping");
    };
    struct Pong: Value {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Test::PacketThread::Pong");
    };
    // Raises a script event while it holds the sender's avatar.
    struct Hold: Value {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Test::PacketThread::Hold");
    };
    // Keeps the packet thread until the test lets it go.
    struct Block: Value {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Test::PacketThread::Block");
    };
    // Writes `value` into the sender's state bag, under a handle.
    struct Mood: Value {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Test::PacketThread::Mood");
    };
    // Reports what the handler is given for its sender.
    struct Who: Value {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Test::PacketThread::Who");
    };

    struct Server final: Framework::Integrations::Server::Instance {
        std::filesystem::path root;
        int32_t port = 0;
        bool started = false;

        // --- The tick thread's own (the test's thread) ---
        double longestGapMs = 0.0;
        Clock::time_point lastTick {};
        int ticks      = 0;
        int spawnProps = 0;
        bool spinProps = false;
        // Set before Start: poses travel as transform batches, as a game that opted in sends them.
        bool batchTransforms = false;
        // Set before a join: the avatar is made while this resource's script is, as far as the scripting side says,
        // the one running.
        std::atomic<bool> claimForDemo {false};
        // Set before Start to replace the script-event limits: rate, burst, flood rate, flood burst.
        double eventLimits[4] = {-1.0, -1.0, -1.0, -1.0};
        // Client script events that got past the limit to where scripts handle them.
        std::atomic<int> clientEvents {0};

        void OnClientEvent(uint64_t, const std::string &, const std::string &) override {
            ++clientEvents;
        }

        // --- Written by handlers, on the packet thread ---
        std::mutex mutex;
        std::vector<uint32_t> notes;
        std::vector<std::thread::id> noteThreads;
        std::atomic<int> slowStarted {0};
        std::atomic<int> slowDone {0};
        std::atomic<int> connects {0};
        std::atomic<int> disconnects {0};
        std::atomic<int> holds {0};
        std::atomic<int> ranOnScripts {0};
        // Where a script's native call (noteThread) ran, and how long the handler that wrote a state bag took.
        std::vector<std::thread::id> scriptCallThreads;
        std::atomic<double> moodHandlerMs {-1.0};
        std::atomic<bool> heldAvatar {false};
        std::atomic<bool> blocked {false};
        std::atomic<bool> release {false};
        std::atomic<int> whos {0};
        std::atomic<uint64_t> whoAvatar {~0ull};
        std::atomic<bool> whoHandleEmpty {false};
        // The avatar the test destroyed under the queued packet, for its handler to ask for.
        std::atomic<uint64_t> goneAvatar {0};

        Framework::Networking::NetworkServer *Net() const {
            return GetNetworkingEngine()->GetNetworkServer();
        }
        Framework::Networking::Replication::ReplicationManager *Replication() const {
            return Net()->GetReplicationManager();
        }

        // Before the packet thread starts, as a mod registers its own.
        void PostInit() override {
            auto *net = Net();
            Replication()->SetTransformBatchingEnabled(batchTransforms);
            net->RegisterRPC<Note>([this](const Note &note, Sender, World &) {
                const std::lock_guard guard(mutex);
                notes.push_back(note.value);
                noteThreads.push_back(std::this_thread::get_id());
            });
            net->RegisterRPC<Slow>([this](const Slow &slow, Sender, World &) {
                ++slowStarted;
                std::this_thread::sleep_for(std::chrono::milliseconds(slow.value));
                ++slowDone;
            });
            net->RegisterRPC<Ping>([](const Ping &ping, Sender sender, World &) {
                Pong pong;
                pong.value = ping.value;
                sender.Reply(pong);
            });
            net->RegisterRPC<Hold>([this](const Hold &hold, Sender sender, World &world) {
                auto avatar = world.Write<Avatar>(sender.Avatar());
                heldAvatar  = static_cast<bool>(avatar);
                // Asking the script thread for something and waiting: only once the handle is let go.
                if (hold.value == 2) {
                    const uint64_t id = avatar.Id();
                    avatar.Reset();
                    RunOnScriptThread([this, id, &world] {
                        // There, the entity is asked for again, which the waiting handler no longer keeps.
                        if (const auto again = world.Write<Avatar>(id)) {
                            again->counter = 4242;
                            ++ranOnScripts;
                        }
                    });
                    ++holds;
                    return;
                }
                Framework::Scripting::EngineScope scope(GetScriptingModule()->GetEngine());
                std::vector<v8::Local<v8::Value>> args {v8pp::to_v8(scope.Isolate(), std::string("held"))};
                // With the sender itself as an argument, wrapped for script as a mod's event helpers wrap it.
                if (hold.value == 1 && avatar) {
                    avatar->position.x = 7.0f;
                    args.push_back(WrapScriptPlayer(scope.Isolate(), avatar->GetNetworkID()));
                }
                GetScriptingModule()->GetResourceManager()->GetEvents().EmitReserved(scope.Isolate(), scope.Context(), "heldEvent", args);
                ++holds;
            });
            net->RegisterRPC<Mood>([this](const Mood &mood, Sender sender, World &world) {
                const auto before = Clock::now();
                {
                    const auto avatar = world.Write<Avatar>(sender.Avatar());
                    if (avatar) {
                        avatar->state.Set("mood", Number(mood.value));
                    }
                }
                // The handle is gone, so whoever listens for the change has been told, or sent for.
                moodHandlerMs = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
                const std::lock_guard guard(mutex);
                noteThreads.push_back(std::this_thread::get_id());
            });
            net->RegisterRPC<Block>([this](const Block &, Sender, World &) {
                blocked             = true;
                const auto deadline = Clock::now() + std::chrono::seconds(20);
                while (!release && Clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                blocked = false;
            });
            net->RegisterRPC<Who>([this](const Who &, Sender sender, World &world) {
                const uint64_t avatar = sender.Avatar();
                whoAvatar             = avatar;
                // By the id the sender has now, and by the one it had when the packet was sent.
                const bool emptyNow = !world.Write<Avatar>(avatar);
                const bool emptyOld = !world.Write<Avatar>(goneAvatar.load());
                whoHandleEmpty      = emptyNow && emptyOld;
                ++whos;
            });
        }

        static Framework::Networking::Replication::StateValue Number(double number) {
            Framework::Networking::Replication::StateValue value;
            value.type   = Framework::Networking::Replication::StateValue::Type::Number;
            value.number = number;
            return value;
        }

        // noteThread(): a native that records which thread the script calling it runs on.
        void ModuleRegister(Framework::Scripting::Engine *engine) override {
            v8::Isolate *isolate = engine->GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            if (context.IsEmpty()) {
                return;
            }
            v8::MaybeLocal<v8::Function> note = v8::Function::New(
                context,
                [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                    auto *self = static_cast<Server *>(info.Data().As<v8::External>()->Value());
                    const std::lock_guard guard(self->mutex);
                    self->scriptCallThreads.push_back(std::this_thread::get_id());
                },
                v8::External::New(isolate, this));
            context->Global()->Set(context, v8pp::to_v8(isolate, "noteThread"), note.ToLocalChecked()).Check();
            // The handle classes as globals, which is a mod's to decide: scripts here write `new Entity(id)`.
            Framework::Scripting::Builtins::Entity::Register(isolate, context->Global());
            Framework::Scripting::Builtins::Player::Register(isolate, context->Global());
        }

        void OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data, World &world) override {
            // What the script thread says while it loads or calls into a resource, said here so that it is certainly
            // in force when the entity is made.
            const bool claim = claimForDemo;
            if (claim) {
                GetScriptingModule()->GetResourceManager()->SetCurrentResourceContext("demo");
            }
            const auto avatar = world.Create<Avatar>();
            if (claim) {
                GetScriptingModule()->GetResourceManager()->SetCurrentResourceContext("");
            }
            if (!avatar) {
                return;
            }
            Replication()->SetOwner(avatar, data.guid);
            Replication()->SetViewer(data.guid, avatar);
            ++connects;
        }

        void OnPlayerDisconnect(MafiaNet::PeerGuid, World &) override {
            ++disconnects;
        }

        void PostUpdate() override {
            const auto now = Clock::now();
            if (lastTick != Clock::time_point {}) {
                longestGapMs = std::max(longestGapMs, std::chrono::duration<double, std::milli>(now - lastTick).count());
            }
            lastTick = now;
            ++ticks;
            auto &world = Replication()->GetWorld();
            for (; spawnProps > 0; --spawnProps) {
                world.Create<Prop>();
            }
            if (spinProps) {
                world.ForEach<Prop>([](Framework::Networking::Replication::Ref<Prop> &prop) {
                    ++prop->spin;
                    prop->position.x += 0.01f;
                });
            }
        }

        size_t Count(uint32_t typeId) {
            size_t count = 0;
            Replication()->GetWorld().ForEach<Framework::Networking::Replication::NetworkEntity>([&](Framework::Networking::Replication::Ref<Framework::Networking::Replication::NetworkEntity> &entity) {
                count += entity->GetTypeId() == typeId ? 1 : 0;
            });
            return count;
        }

        static void Write(const std::filesystem::path &path, const std::string &text) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
        }

        std::string Marker(const char *name) const {
            return (root / name).generic_string();
        }

        bool Start(const char *name, const std::string &serverScript = "", int32_t replicationThreads = 0) {
            Framework::Networking::Replication::EntityRegistry::Get().Register<Avatar>(Avatar::kTypeName);
            Framework::Networking::Replication::EntityRegistry::Get().Register<Prop>(Prop::kTypeName);

            root = std::filesystem::temp_directory_path() / name;
            std::filesystem::remove_all(root);
            Write(root / "resources" / "demo" / "package.json", R"({"name":"demo","version":"1.0.0","main":"server.js","mafiahub":{"server":"server.js"}})");
            std::string script = serverScript;
            for (size_t at = script.find("$ROOT"); at != std::string::npos; at = script.find("$ROOT")) {
                script.replace(at, 5, root.generic_string());
            }
            Write(root / "resources" / "demo" / "server.js", script);

            static char arg0[]  = "server";
            static char *argv[] = {arg0, nullptr};
            Framework::Integrations::Server::InstanceOptions opts;
            opts.modSlug            = "packet_thread_ut";
            opts.modName            = "Packet thread test";
            opts.modVersion         = "1.0.0";
            opts.gameName           = "test";
            opts.gameVersion        = "1";
            opts.bindHost           = "127.0.0.1";
            opts.bindPort           = FreePort();
            opts.maxPlayers         = 8;
            opts.bindPublicServer   = false;
            opts.enableSignals      = false;
            opts.verifyBuildToken   = false;
            opts.modConfigFile      = (root / "server.json").string();
            opts.resourcesPath      = (root / "resources").string();
            opts.replicationThreads = replicationThreads;
            opts.backgroundThreads  = 1;
            // Several clients of the storm come from this one address at once.
            opts.pendingConnectionsPerAddress = 16;
            if (eventLimits[0] >= 0.0) {
                opts.clientEventRate       = eventLimits[0];
                opts.clientEventBurst      = eventLimits[1];
                opts.clientEventFloodRate  = eventLimits[2];
                opts.clientEventFloodBurst = eventLimits[3];
            }
            opts.argc                         = 1;
            opts.argv                         = argv;
            auto result                       = Init(opts);
            if (!result) {
                fprintf(stderr, "server_packet_thread: Init failed: %s\n", result.GetError().message.c_str());
                Shutdown();
                return false;
            }
            port    = opts.bindPort;
            started = true;
            return true;
        }

        ~Server() {
            Stop();
        }

        void Stop() {
            if (!std::exchange(started, false)) {
                return;
            }
            Shutdown();
            std::error_code code;
            std::filesystem::remove_all(std::filesystem::path(Framework::Utils::GetAbsolutePathA((root / "resources").string())).parent_path(), code);
            std::filesystem::remove_all(root, code);
        }
    };

    // A framework client as far as the network goes: asks to join, proves its build, joins once it has the resource
    // list, and reports ready once the world it was sent is in.
    struct Client final {
        Framework::Networking::NetworkClient net {"127.0.0.1"};
        bool connected      = false;
        bool disconnected   = false;
        std::string disconnectReason;
        bool joined         = false;
        bool ready          = false;
        int32_t readyEvent  = -1;
        bool readySignalled = false;
        std::vector<uint32_t> pongs;
        // Messages sent with an acknowledgement asked for that the server has acknowledged.
        int acknowledged            = 0;
        MafiaNet::RakNetGUID server = MafiaNet::UNASSIGNED_RAKNET_GUID;

        Client() {
            net.SetBuildToken(Framework::Networking::NetworkPeer::kBuildVerificationDisabledToken);
            net.SetOnPlayerConnectedCallback([this](MafiaNet::Packet *packet) {
                connected = true;
                server    = packet->guid;
            });
            net.SetOnPlayerDisconnectedCallback([this](MafiaNet::Packet *, Framework::Networking::DisconnectionReason, const std::string &reason) {
                disconnected     = true;
                disconnectReason = reason;
            });
            net.SetOnConnectionReadyCallback([this](int) {
                ready = true;
            });
            net.RegisterRPC<Framework::Networking::RPC::ServerResources>([this](const Framework::Networking::RPC::ServerResources &resources, MafiaNet::Packet *packet) {
                readyEvent = resources.readyEventId;
                Framework::Networking::RPC::ClientJoin join;
                net.SendRPC(join, packet->guid);
                // The spawn barrier, armed as the client instance arms it.
                net.GetReadyEvent()->SetEvent(readyEvent, false);
                net.GetReadyEvent()->AddToWaitList(readyEvent, packet->guid);
                joined = true;
            });
            net.RegisterRPC<Pong>([this](const Pong &pong, MafiaNet::Packet *) {
                pongs.push_back(pong.value);
            });
            net.SetUnknownPacketHandler([this](MafiaNet::Packet *packet) {
                if (packet->data[0] != ID_SND_RECEIPT_ACKED) {
                    return false;
                }
                ++acknowledged;
                return true;
            });
        }

        bool Connect(int32_t port, const char *name = "tester") {
            connected = disconnected = joined = ready = readySignalled = false;
            readyEvent                                                 = -1;
            Framework::Networking::RPC::ClientIdentity identity;
            identity.name = name;
            return static_cast<bool>(net.Connect("127.0.0.1", port, "", identity.Encode()));
        }

        void Update() {
            net.Update();
            // The spawn barrier, as the client instance lowers it: the world it was sent is in.
            if (joined && !readySignalled && net.IsInitialReplicationDownloadComplete()) {
                readySignalled = true;
                net.GetReadyEvent()->SetEvent(readyEvent, true);
            }
        }

        MafiaNet::PeerGuid Guid() const {
            return MafiaNet::ToPeerGuid(net.GetPeer()->GetMyGUID());
        }

        // Its own avatar as the server constructed it here, null until it has.
        Avatar *OwnAvatar() {
            Avatar *own = nullptr;
            Framework::Networking::Replication::ClientEntities(*net.GetReplicationManager()).ForEach<Avatar>([&](Avatar *avatar) {
                if (avatar->ownerGUID == Guid()) {
                    own = avatar;
                }
            });
            return own;
        }

        // A script event, as Events.emitServer sends one.
        void Emit(const std::string &name) {
            Framework::Integrations::Shared::RPC::EmitScriptEvent event;
            event.FromParameters(name, "{}");
            net.SendRPC(event, server);
        }

        template <typename T>
        void Send(uint32_t value = 0, MafiaNet::Reliability reliability = MafiaNet::Reliability::ReliableOrdered) {
            T payload;
            payload.value = value;
            net.SendRPC(payload, server, MafiaNet::Priority::High, reliability);
        }
    };

    // Ticks the server, as Run() does, and updates the clients, all on this thread, until `done`.
    template <typename Done>
    bool Pump(Server &server, std::initializer_list<Client *> clients, Done done, std::chrono::milliseconds limit = std::chrono::seconds(10)) {
        const auto deadline = Clock::now() + limit;
        while (!done()) {
            if (Clock::now() >= deadline) {
                return false;
            }
            server.Update();
            for (Client *client : clients) {
                client->Update();
            }
        }
        return true;
    }

    inline void PumpFor(Server &server, std::initializer_list<Client *> clients, std::chrono::milliseconds duration) {
        Pump(
            server, clients,
            [] {
                return false;
            },
            duration);
    }

    // Connected, joined and past the spawn barrier, with an avatar on the server.
    inline bool Join(Server &server, Client &client, std::initializer_list<Client *> others = {}) {
        if (!client.Connect(server.port)) {
            return false;
        }
        std::vector<Client *> all(others);
        all.push_back(&client);
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (!(client.ready && server.Replication()->GetViewerId(client.Guid()) != 0)) {
            if (Clock::now() >= deadline) {
                // Logging is paused in the suite: say how far it got, or a failure elsewhere is a guess.
                fprintf(stderr, "server_packet_thread: join stopped at connected=%d disconnected=%d joined=%d downloaded=%d ready=%d avatar=%llu\n", client.connected, client.disconnected, client.joined, client.net.IsInitialReplicationDownloadComplete(), client.ready,
                    static_cast<unsigned long long>(server.Replication()->GetViewerId(client.Guid())));
                return false;
            }
            server.Update();
            for (Client *each : all) {
                each->Update();
            }
        }
        return true;
    }
    // What "applies inbound state while the pass serializes" measures, for one shape of server.
    struct InboundState {
        bool up      = false;
        bool joined  = false;
        bool settled = false;
        // Over one second, the least any client's counter and pose advanced on the server.
        uint32_t leastAdvance = 0;
        float leastMoved      = 0.0f;
        size_t props          = 0;
        int ticks             = 0;
    };

    // Every client writes its own avatar on each update, a counter (the state channel) and a pose (a transform batch),
    // while the server spins 200 props every tick and sends them down to all of them.
    inline InboundState RunInboundState(const char *name, int clientCount, int32_t replicationThreads) {
        using namespace std::chrono_literals;
        InboundState result;
        Server server;
        server.batchTransforms = true;
        result.up              = server.Start(name, "", replicationThreads);
        if (!result.up) {
            return result;
        }
        std::vector<std::unique_ptr<Client>> clients;
        std::vector<uint64_t> avatars;
        const auto update = [&] {
            for (auto &client : clients) {
                client->Update();
            }
        };
        for (int i = 0; i < clientCount; ++i) {
            clients.push_back(std::make_unique<Client>());
            Client &client = *clients.back();
            client.net.GetReplicationManager()->SetTransformBatchingEnabled(true);
            if (!client.Connect(server.port)) {
                return result;
            }
            const auto deadline = Clock::now() + 10s;
            while (!(client.ready && server.Replication()->GetViewerId(client.Guid()) != 0)) {
                if (Clock::now() >= deadline) {
                    fprintf(stderr, "server_packet_thread: client %d of %d did not get in\n", i + 1, clientCount);
                    return result;
                }
                server.Update();
                update();
            }
            avatars.push_back(server.Replication()->GetViewerId(client.Guid()));
        }
        result.joined     = true;
        server.spawnProps = 200;
        server.spinProps  = true;

        const auto drive = [&] {
            for (auto &client : clients) {
                if (Avatar *own = client->OwnAvatar()) {
                    ++own->counter;
                    own->position.x += 0.5f;
                }
            }
        };
        struct Seen {
            uint32_t counter = 0;
            float x          = 0.0f;
        };
        const auto read = [&] {
            std::vector<Seen> seen;
            for (const uint64_t id : avatars) {
                const auto avatar = server.Replication()->GetWorld().Read<Avatar>(id);
                seen.push_back(avatar ? Seen {avatar->counter, avatar->position.x} : Seen {});
            }
            return seen;
        };
        const auto propsOn = [](Client &client) {
            size_t count = 0;
            Framework::Networking::Replication::ClientEntities(*client.net.GetReplicationManager()).ForEach<Prop>([&](Prop *) {
                ++count;
            });
            return count;
        };
        const auto pump = [&](const auto &done, std::chrono::milliseconds limit) {
            const auto deadline = Clock::now() + limit;
            while (Clock::now() < deadline) {
                server.Update();
                update();
                drive();
                if (done()) {
                    return true;
                }
            }
            return false;
        };
        // Until every client has all 200 and its own state has reached the server once.
        result.settled = pump(
            [&] {
                for (auto &client : clients) {
                    if (propsOn(*client) != 200) {
                        return false;
                    }
                }
                for (const Seen &seen : read()) {
                    if (seen.counter == 0 || seen.x == 0.0f) {
                        return false;
                    }
                }
                return true;
            },
            15s);
        if (!result.settled) {
            return result;
        }
        const std::vector<Seen> before = read();
        const int ticksBefore          = server.ticks;
        pump(
            [] {
                return false;
            },
            1000ms);
        const std::vector<Seen> after = read();
        result.leastAdvance           = ~0u;
        result.leastMoved             = 1e9f;
        for (size_t i = 0; i < after.size(); ++i) {
            result.leastAdvance = std::min(result.leastAdvance, after[i].counter - before[i].counter);
            result.leastMoved   = std::min(result.leastMoved, after[i].x - before[i].x);
        }
        result.ticks = server.ticks - ticksBefore;
        result.props = server.Count(Framework::Networking::Replication::EntityTypeId<Prop>());
        server.Stop();
        return result;
    }
} // namespace ServerPacketThreadTest

MODULE(server_packet_thread, {
    using namespace ServerPacketThreadTest;
    using namespace std::chrono_literals;

    IT("runs a handler off the tick thread", {
        Server server;
        const bool up = server.Start("fw_packet_thread_off_tick");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        Client client;
        const bool in = Join(server, client);
        EQUALS(in, true);
        if (!in) {
            break;
        }
        client.Send<Note>(7);
        client.Send<Note>(8);
        EQUALS(Pump(server, {&client}, [&] {
            const std::lock_guard guard(server.mutex);
            return server.notes.size() == 2;
        }),
            true);
        std::vector<std::thread::id> threads;
        {
            const std::lock_guard guard(server.mutex);
            threads = server.noteThreads;
        }
        EQUALS(threads.size(), static_cast<size_t>(2));
        if (threads.size() != 2) {
            break;
        }
        // This thread runs the ticks; neither handler ran on it, and both ran on the same one.
        EQUALS(threads[0] != std::this_thread::get_id(), true);
        EQUALS(threads[0] == threads[1], true);
        // Connecting was handled there too, by the mod's override.
        EQUALS(server.connects.load(), 1);
        server.Stop();
    });

    IT("leaves an entity a handler creates out of the resource whose script is running", {
        Server server;
        const bool up = server.Start("fw_packet_thread_owner");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        // Entities made while a resource's script runs are the resource's, and go when it stops. A handler making
        // one at that moment is not that script.
        auto *resources    = server.GetScriptingModule()->GetResourceManager();
        server.claimForDemo = true;
        Client client;
        const bool in = Join(server, client);
        EQUALS(in, true);
        if (!in) {
            break;
        }
        EQUALS(server.Count(Framework::Networking::Replication::EntityTypeId<Avatar>()), static_cast<size_t>(1));
        // Stopping it takes what it owns with it.
        (void)resources->StopAll();
        EQUALS(server.Count(Framework::Networking::Replication::EntityTypeId<Avatar>()), static_cast<size_t>(1));
        EQUALS(server.Replication()->GetViewerId(client.Guid()) != 0, true);
        server.Stop();
    });

    IT("leaves an entity the tick creates out of the resource whose script was stopped for that tick", {
        Server server;
        const bool up = server.Start("fw_packet_thread_tick_owner");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        PumpFor(server, {}, 50ms);
        // A script stopped at a safe point for a tick is still, as far as the scripting side says, the one
        // running: said here for it, for the length of a tick in which the mod creates entities.
        auto *resources = server.GetScriptingModule()->GetResourceManager();
        resources->SetCurrentResourceContext("demo");
        server.spawnProps = 3;
        server.Update();
        const bool spawned = Pump(server, {}, [&] {
            return server.spawnProps == 0;
        });
        resources->SetCurrentResourceContext("");
        EQUALS(spawned, true);
        EQUALS(server.Count(Framework::Networking::Replication::EntityTypeId<Prop>()), static_cast<size_t>(3));
        // Stopping the resource takes what it owns with it, and these are not its.
        (void)resources->StopAll();
        EQUALS(server.Count(Framework::Networking::Replication::EntityTypeId<Prop>()), static_cast<size_t>(3));
        server.Stop();
    });

    IT("keeps ticking while a handler takes its time", {
        Server server;
        const bool up = server.Start("fw_packet_thread_slow");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        Client client;
        const bool in = Join(server, client);
        EQUALS(in, true);
        if (!in) {
            break;
        }
        // Three times the bound, as 300 ms is to 100: a hosted runner with three cores stalls a thread for 150 ms on
        // its own, so both are stretched well clear of that.
        constexpr uint32_t kHandlerMs = 900;
        constexpr double kLongestGap  = 300.0;
        client.Send<Slow>(kHandlerMs);
        EQUALS(Pump(server, {&client}, [&] {
            return server.slowStarted.load() == 1;
        }),
            true);
        server.longestGapMs  = 0.0;
        const int ticksAtStart = server.ticks;
        EQUALS(Pump(server, {&client}, [&] {
            return server.slowDone.load() == 1;
        }),
            true);
        const int ticksDuring = server.ticks - ticksAtStart;
        fprintf(stderr, "server_packet_thread: longest gap between ticks %.1f ms over %d ticks while a handler slept %u ms\n", server.longestGapMs, ticksDuring, kHandlerMs);
        LESSER(server.longestGapMs, kLongestGap);
        GREATER(ticksDuring, 10);
        server.Stop();
    });

    IT("runs one connection's handlers in the order they were sent", {
        Server server;
        const bool up = server.Start("fw_packet_thread_order");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        Client client;
        const bool in = Join(server, client);
        EQUALS(in, true);
        if (!in) {
            break;
        }
        constexpr uint32_t kCount = 1000;
        for (uint32_t i = 0; i < kCount; ++i) {
            client.Send<Note>(i);
            // In bursts, with ticks in between: the handlers run while the pass does.
            if (i % 100 == 99) {
                server.Update();
                client.Update();
            }
        }
        EQUALS(Pump(server, {&client}, [&] {
            const std::lock_guard guard(server.mutex);
            return server.notes.size() >= kCount;
        }),
            true);
        std::vector<uint32_t> notes;
        {
            const std::lock_guard guard(server.mutex);
            notes = server.notes;
        }
        EQUALS(notes.size(), static_cast<size_t>(kCount));
        uint32_t inOrder = 0;
        while (inOrder < notes.size() && notes[inOrder] == inOrder) {
            ++inOrder;
        }
        EQUALS(inOrder, kCount);
        server.Stop();
    });

    IT("applies inbound state while the pass serializes", {
        const InboundState run = RunInboundState("fw_packet_thread_state", 1, 0);
        EQUALS(run.up, true);
        EQUALS(run.joined, true);
        EQUALS(run.settled, true);
        fprintf(stderr, "server_packet_thread: client-owned counters advanced by at least %u and poses by %.1f over %d ticks, 1 client, no workers, 200 props replicating\n", run.leastAdvance, run.leastMoved, run.ticks);
        GREATER(run.leastAdvance, 5u);
        GREATER(run.leastMoved, 2.0f);
        EQUALS(run.props, static_cast<size_t>(200));
    });

    IT("applies inbound state while replication workers serve the pass", {
        // Eight connections, which is where the pass starts to share them out, and two workers beside the tick.
        const InboundState run = RunInboundState("fw_packet_thread_workers", 8, 2);
        EQUALS(run.up, true);
        EQUALS(run.joined, true);
        EQUALS(run.settled, true);
        fprintf(stderr, "server_packet_thread: client-owned counters advanced by at least %u and poses by %.1f over %d ticks, 8 clients, 2 workers, 200 props replicating\n", run.leastAdvance, run.leastMoved, run.ticks);
        GREATER(run.leastAdvance, 5u);
        GREATER(run.leastMoved, 2.0f);
        EQUALS(run.props, static_cast<size_t>(200));
    });

    IT("lets a handler raise an event with a handle held", {
        Server server;
        const bool up = server.Start("fw_packet_thread_held", R"(
            const fs = require('node:fs');
            Events.on('heldEvent', (text) => { fs.writeFileSync('$ROOT/held', text); });
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        Client client;
        const bool in = Join(server, client);
        EQUALS(in, true);
        if (!in) {
            break;
        }
        client.Send<Hold>();
        // Its content, not its existence: the file is there, empty, a moment before the script has written it.
        EQUALS(Pump(server, {&client}, [&] {
            std::string text;
            std::ifstream(server.Marker("held")) >> text;
            return text == "held" && server.holds.load() == 1;
        }),
            true);
        EQUALS(server.heldAvatar.load(), true);
        server.Stop();
    });

    IT("lets a handler pass the entity it holds to an event, wrapped for script", {
        Server server;
        const bool up = server.Start("fw_packet_thread_wrapped", R"(
            const fs = require('node:fs');
            Events.on('heldEvent', (text, player) => { fs.writeFileSync('$ROOT/wrapped', `${player.id}:${player.position.x}`); });
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        Client client;
        const bool in = Join(server, client);
        EQUALS(in, true);
        if (!in) {
            break;
        }
        const std::string expected = std::to_string(server.Replication()->GetViewerId(client.Guid())) + ":7";
        client.Send<Hold>(1);
        std::string seen;
        EQUALS(Pump(server, {&client}, [&] {
            seen.clear();
            std::ifstream(server.Marker("wrapped")) >> seen;
            return seen == expected;
        }),
            true);
        STREQUALS(seen.c_str(), expected.c_str());
        EQUALS(server.heldAvatar.load(), true);
        server.Stop();
    });

    IT("wraps an id that names no entity without throwing, and answers for it with nothing", {
        Server server;
        const bool up = server.Start("fw_packet_thread_ghost", R"(
            const fs = require('node:fs');
            // Refused for the id, not for a class that is not there.
            const why = (make) => { try { make(); return 'no'; } catch (error) { return String(error.message).includes('is not valid') ? 'yes' : 'other'; } };
            const refused = why(() => new Entity(999999)) + ',' + why(() => new Player(999999));
            fs.writeFileSync('$ROOT/refused', refused);
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        constexpr uint64_t kNobody = 999999;
        bool threw                 = false;
        bool wrapped               = false;
        float x                    = 1.0f;
        uint32_t virtualWorld      = 1;
        bool hasOwner              = true;
        std::string steamId        = "?";
        try {
            // Natively, as an event argument is built: no entity is asked for, so none has to be there.
            const Framework::Scripting::Builtins::Entity ghost(kNobody);
            x            = ghost.GetPosition().getX();
            virtualWorld = ghost.GetVirtualWorld();
            MafiaNet::PeerGuid owner = MafiaNet::UNASSIGNED_PEER_GUID;
            hasOwner                 = ghost.ResolveOwner(owner);
            const Framework::Scripting::Builtins::Player nobody(kNobody);
            steamId = nobody.GetSteamId();
            Framework::Scripting::EngineScope scope(server.GetScriptingModule()->GetEngine());
            wrapped = !server.WrapScriptPlayer(scope.Isolate(), kNobody).IsEmpty() && !server.WrapScriptEntity(scope.Isolate(), kNobody).IsEmpty();
        }
        catch (const std::exception &) {
            threw = true;
        }
        EQUALS(threw, false);
        EQUALS(wrapped, true);
        EQUALS(x, 0.0f);
        EQUALS(virtualWorld, 0u);
        EQUALS(hasOwner, false);
        STREQUALS(steamId.c_str(), "");
        // A script that names an entity itself is still told when there is none.
        std::string refused;
        EQUALS(Pump(server, {}, [&] {
            refused.clear();
            std::ifstream(server.Marker("refused")) >> refused;
            return refused == "yes,yes";
        }),
            true);
        STREQUALS(refused.c_str(), "yes,yes");
        server.Stop();
    });

    IT("lets a handler wait for the script thread once it has let go of its handle", {
        Server server;
        const bool up = server.Start("fw_packet_thread_run");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        Client client;
        const bool in = Join(server, client);
        EQUALS(in, true);
        if (!in) {
            break;
        }
        const uint64_t avatarId = server.Replication()->GetViewerId(client.Guid());
        client.Send<Hold>(2);
        EQUALS(Pump(server, {&client}, [&] {
            return server.holds.load() == 1;
        }),
            true);
        EQUALS(server.heldAvatar.load(), true);
        EQUALS(server.ranOnScripts.load(), 1);
        uint32_t counter = 0;
        {
            const auto avatar = server.Replication()->GetWorld().Read<Avatar>(avatarId);
            counter           = avatar ? avatar->counter : 0;
        }
        EQUALS(counter, 4242u);
        server.Stop();
    });

    IT("runs a resource's state-bag listener on the script thread, whoever wrote the bag", {
        Server server;
        // A resource that listens to one entity's bag, and computes for most of its time.
        const bool up = server.Start("fw_packet_thread_listener", R"(
            const fs = require('node:fs');
            Events.on('watch', (id) => {
                new Entity(id).state.onChange('mood', (key, value) => {
                    noteThread();
                    fs.writeFileSync('$ROOT/mood', String(value));
                });
                fs.writeFileSync('$ROOT/watching', 'yes');
            });

            setInterval(() => { const end = Date.now() + 300; while (Date.now() < end) {} }, 350);
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        Client client;
        const bool in = Join(server, client);
        EQUALS(in, true);
        if (!in) {
            break;
        }
        const uint64_t avatarId = server.Replication()->GetViewerId(client.Guid());
        {
            Framework::Scripting::EngineScope scope(server.GetScriptingModule()->GetEngine());
            std::vector<v8::Local<v8::Value>> args {v8pp::to_v8(scope.Isolate(), static_cast<double>(avatarId))};
            server.GetScriptingModule()->GetResourceManager()->GetEvents().EmitReserved(scope.Isolate(), scope.Context(), "watch", args);
        }
        const auto marker = [&](const char *name) {
            std::string text;
            std::ifstream(server.Marker(name)) >> text;
            return text;
        };
        EQUALS(Pump(server, {&client}, [&] {
            return marker("watching") == "yes";
        }),
            true);

        // Written by a packet handler, under its handle.
        client.Send<Mood>(1);
        EQUALS(Pump(server, {&client}, [&] {
            return marker("mood") == "1";
        }),
            true);
        // And by the tick thread, which is this one.
        const auto before = Clock::now();
        {
            const auto avatar = server.Replication()->GetWorld().Write<Avatar>(avatarId);
            if (avatar) {
                avatar->state.Set("mood", Server::Number(2));
            }
        }
        const double tickWriteMs = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
        EQUALS(Pump(server, {&client}, [&] {
            return marker("mood") == "2";
        }),
            true);

        std::vector<std::thread::id> listeners;
        std::vector<std::thread::id> handlers;
        {
            const std::lock_guard guard(server.mutex);
            listeners = server.scriptCallThreads;
            handlers  = server.noteThreads;
        }
        EQUALS(listeners.size(), static_cast<size_t>(2));
        EQUALS(handlers.size(), static_cast<size_t>(1));
        if (listeners.size() != 2 || handlers.size() != 1) {
            break;
        }
        // Both times on one thread, which is neither the packet thread nor this one.
        EQUALS(listeners[0] == listeners[1], true);
        EQUALS(listeners[0] != handlers[0], true);
        EQUALS(listeners[0] != std::this_thread::get_id(), true);
        fprintf(stderr, "server_packet_thread: writing a listened state-bag key took %.2f ms in a packet handler and %.2f ms on the tick thread, beside a busy script\n", server.moodHandlerMs.load(), tickWriteMs);
        // Neither writer waited for the script, which computes 300 ms at a time.
        LESSER(server.moodHandlerMs.load(), 300.0);
        LESSER(tickWriteMs, 300.0);
        server.Stop();
    });

    IT("cuts a client's burst of script events at the limit, and leaves a client under it alone", {
        Server server;
        // Twenty at once and one more a second: a burst is cut where the test can count it. The flood limit is far.
        server.eventLimits[0] = 1.0;
        server.eventLimits[1] = 20.0;
        server.eventLimits[2] = 1.0;
        server.eventLimits[3] = 1000.0;
        const bool up         = server.Start("fw_packet_thread_events");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        Client noisy;
        Client quiet;
        const bool in = Join(server, noisy) && Join(server, quiet, {&noisy});
        EQUALS(in, true);
        if (!in) {
            break;
        }
        for (int i = 0; i < 60; ++i) {
            noisy.Emit("poke");
        }
        for (int i = 0; i < 10; ++i) {
            quiet.Emit("poke");
        }
        // A marker behind each client's events: once both are answered, every event before them was looked at.
        noisy.Send<Ping>(1);
        quiet.Send<Ping>(2);
        EQUALS(Pump(server, {&noisy, &quiet}, [&] {
            return !noisy.pongs.empty() && !quiet.pongs.empty();
        }),
            true);
        PumpFor(server, {&noisy, &quiet}, 100ms);
        const int handled   = server.clientEvents.load();
        const uint64_t cut  = server.ClientEventsDropped();
        fprintf(stderr, "server_packet_thread: of 60 script events from one client and 10 from another, %d reached scripts and %llu were dropped\n", handled, static_cast<unsigned long long>(cut));
        // The quiet one's ten, and the noisy one's twenty plus what a second or two refilled.
        GREATEREQ(handled, 30);
        LESSEREQ(handled, 34);
        EQUALS(static_cast<uint64_t>(handled) + cut, static_cast<uint64_t>(70));
        EQUALS(noisy.disconnected, false);
        EQUALS(quiet.disconnected, false);
        server.Stop();
    });

    IT("drops a client that keeps flooding script events past the limit", {
        Server server;
        server.eventLimits[0] = 1.0;
        server.eventLimits[1] = 20.0;
        server.eventLimits[2] = 1.0;
        server.eventLimits[3] = 30.0;
        const bool up         = server.Start("fw_packet_thread_flood");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        Client flooder;
        Client bystander;
        const bool in = Join(server, flooder) && Join(server, bystander, {&flooder});
        EQUALS(in, true);
        if (!in) {
            break;
        }
        for (int i = 0; i < 200; ++i) {
            flooder.Emit("poke");
        }
        EQUALS(Pump(server, {&flooder, &bystander}, [&] {
            return flooder.disconnected;
        }),
            true);
        STREQUALS(flooder.disconnectReason.c_str(), "Reliable network event overflow.");
        // Twenty got through, thirty or so were let off with a drop, and the next one ended it.
        LESSEREQ(server.clientEvents.load(), 24);
        // Nobody else pays for it.
        bystander.Emit("poke");
        bystander.Send<Ping>(3);
        EQUALS(Pump(server, {&bystander}, [&] {
            return !bystander.pongs.empty();
        }),
            true);
        EQUALS(bystander.disconnected, false);
        server.Stop();
    });

    IT("survives clients connecting and dropping in a storm", {
        Server server;
        const bool up = server.Start("fw_packet_thread_storm");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        // Five clients, ten rounds each. A round ends at one of three points: connected and no more, joined, or
        // all the way in; so connections close at every stage of the way in, while others are on theirs.
        constexpr int kClients = 5;
        constexpr int kRounds  = 10;
        struct Storming {
            Client client;
            int round    = 0;
            bool waiting = false;
        };
        std::vector<std::unique_ptr<Storming>> storm;
        for (int i = 0; i < kClients; ++i) {
            storm.push_back(std::make_unique<Storming>());
        }
        // The clients are driven by a thread of their own, as they are processes of their own: leaving blocks for
        // a moment, and this thread's ticks are what is measured.
        std::atomic<int> cycles {0};
        std::atomic<bool> connectFailed {false};
        const auto started  = Clock::now();
        const auto deadline = started + 60s;
        std::thread players([&] {
            while (cycles < kClients * kRounds && Clock::now() < deadline && !connectFailed) {
                for (int i = 0; i < kClients; ++i) {
                    Storming &each = *storm[i];
                    if (each.round >= kRounds) {
                        continue;
                    }
                    if (!each.waiting) {
                        if (!each.client.Connect(server.port)) {
                            connectFailed = true;
                            break;
                        }
                        each.waiting = true;
                        continue;
                    }
                    each.client.Update();
                    const int stage = (each.round + i) % 3;
                    const bool far  = (stage == 0 && each.client.connected) || (stage == 1 && each.client.joined) || (stage == 2 && each.client.ready);
                    if (far || each.client.disconnected) {
                        (void)each.client.net.Disconnect();
                        each.waiting = false;
                        ++each.round;
                        ++cycles;
                    }
                }
                std::this_thread::sleep_for(1ms);
            }
        });
        server.longestGapMs = 0.0;
        const int ticksAtStart = server.ticks;
        while (cycles < kClients * kRounds && Clock::now() < deadline && !connectFailed) {
            server.Update();
        }
        players.join();
        const double stormGapMs = server.longestGapMs;
        const int stormTicks    = server.ticks - ticksAtStart;
        const double stormMs = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        EQUALS(connectFailed.load(), false);
        EQUALS(cycles.load(), kClients * kRounds);

        // Afterwards the server still answers, to a client that goes all the way in.
        Client last;
        const bool in = Join(server, last);
        EQUALS(in, true);
        if (!in) {
            break;
        }
        last.Send<Ping>(41);
        EQUALS(Pump(server, {&last}, [&] {
            return !last.pongs.empty();
        }),
            true);
        EQUALS(last.pongs.empty() ? 0u : last.pongs.front(), 41u);
        (void)last.net.Disconnect();

        // And nothing of anyone is left: no viewer, no avatar, no connection being replicated to.
        const bool cleared = Pump(server, {}, [&] {
            return server.Replication()->GetViewerIds().empty() && server.Replication()->GetConnectionCount() == 0 && server.Net()->GetPlayerCount() == 0;
        });
        EQUALS(cleared, true);
        EQUALS(server.Replication()->GetViewerIds().size(), static_cast<size_t>(0));
        EQUALS(server.Count(Framework::Networking::Replication::EntityTypeId<Avatar>()), static_cast<size_t>(0));
        fprintf(stderr, "server_packet_thread: %d connect/disconnect cycles took %.0f ms over %d ticks, longest gap between ticks %.1f ms; %d joined, %d left after being ready\n", cycles.load(), stormMs, stormTicks, stormGapMs,
            server.connects.load(), server.disconnects.load());
        server.Stop();
    });

    IT("gives a handler empty handles for a sender that has gone", {
        Server server;
        const bool up = server.Start("fw_packet_thread_gone");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        Client blocker;
        Client leaver;
        const bool in = Join(server, blocker) && Join(server, leaver, {&blocker});
        EQUALS(in, true);
        if (!in) {
            break;
        }
        // The packet thread is kept inside a handler, so what arrives next waits in the queue.
        blocker.Send<Block>();
        EQUALS(Pump(server, {&blocker, &leaver}, [&] {
            return server.blocked.load();
        }),
            true);
        // Acknowledged by the server's network thread, which has by then put it in the queue the packet thread is
        // not reading: that very message, not just any packet.
        leaver.Send<Who>(0, MafiaNet::Reliability::ReliableOrderedWithAckReceipt);
        const bool queued = Pump(server, {&blocker, &leaver}, [&] {
            return leaver.acknowledged == 1;
        });
        EQUALS(queued, true);
        // The sender goes before its packet is handled, the way a script's kick takes a player out: its avatar
        // destroyed and its connection dropped, by this thread.
        const uint64_t avatarId = server.Replication()->GetViewerId(leaver.Guid());
        NEQUALS(avatarId, static_cast<uint64_t>(0));
        server.goneAvatar = avatarId;
        EQUALS(server.Replication()->GetWorld().Destroy(avatarId), true);
        server.Net()->KickPlayer(leaver.net.GetPeer()->GetMyGUID(), Framework::Networking::DisconnectionReason::KICKED);
        EQUALS(server.whos.load(), 0);
        server.release = true;
        EQUALS(Pump(server, {&blocker, &leaver}, [&] {
            return server.whos.load() == 1;
        }),
            true);
        EQUALS(server.whoAvatar.load(), static_cast<uint64_t>(0));
        EQUALS(server.whoHandleEmpty.load(), true);
        server.Stop();
    });

    IT("drops hostile packets and keeps draining", {
        Server server;
        const bool up = server.Start("fw_packet_thread_hostile");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        Client client;
        const bool in = Join(server, client);
        EQUALS(in, true);
        if (!in) {
            break;
        }
        // An RPC nobody registered.
        MafiaNet::BitStream nobody;
        nobody.Write(static_cast<uint32_t>(1));
        client.net.SendRawRPC(FW_RPC_IDENTIFIER("Test::PacketThread::Nobody"), nobody, client.server);
        // A registered one whose payload stops after one byte of four.
        MafiaNet::BitStream truncated;
        truncated.Write(static_cast<uint8_t>(0xAB));
        client.net.SendRawRPC(Note::kIdentifier, truncated, client.server);
        // An id only MafiaNet may raise, hidden behind a timestamp: handled, it would close this connection.
        MafiaNet::BitStream forged;
        forged.Write(static_cast<MafiaNet::MessageID>(ID_TIMESTAMP));
        forged.Write(MafiaNet::GetTime());
        forged.Write(static_cast<MafiaNet::MessageID>(ID_DISCONNECTION_NOTIFICATION));
        client.net.GetPeer()->Send(&forged, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, 0, MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, true);
        // Then one that is in order, and a question only a connection still open gets an answer to.
        client.Send<Note>(77);
        client.Send<Ping>(5);
        EQUALS(Pump(server, {&client}, [&] {
            return !client.pongs.empty();
        }),
            true);
        std::vector<uint32_t> notes;
        {
            const std::lock_guard guard(server.mutex);
            notes = server.notes;
        }
        // The truncated one never reached its handler: the only note is the one that was sent whole.
        EQUALS(notes.size(), static_cast<size_t>(1));
        EQUALS(notes.empty() ? 0u : notes.front(), 77u);
        EQUALS(client.disconnected, false);
        EQUALS(server.Replication()->GetViewerId(client.Guid()) != 0, true);
        server.Stop();
    });

    IT("shuts down while a handler is running", {
        Server server;
        const bool up = server.Start("fw_packet_thread_shutdown");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        Client client;
        const bool in = Join(server, client);
        EQUALS(in, true);
        if (!in) {
            break;
        }
        client.Send<Slow>(600);
        // More behind it, which no handler may start on once the server is going down.
        for (uint32_t i = 0; i < 50; ++i) {
            client.Send<Note>(i);
        }
        EQUALS(Pump(server, {&client}, [&] {
            return server.slowStarted.load() == 1;
        }),
            true);
        const auto before = Clock::now();
        server.Stop();
        const double shutdownMs = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
        fprintf(stderr, "server_packet_thread: shutdown with a handler asleep took %.0f ms\n", shutdownMs);
        LESSER(shutdownMs, 5000.0);
        // What the handler does is whole or not there: it began, so it finished, and nothing ran after it.
        EQUALS(server.slowStarted.load(), 1);
        EQUALS(server.slowDone.load(), 1);
        EQUALS(server.notes.size(), static_cast<size_t>(0));
    });
});
