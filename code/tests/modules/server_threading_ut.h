/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "core_modules.h"
#include "integrations/server/instance.h"
#include "scripting/resource/resource_manager.h"
#include "utils/crypto.h"
#include "utils/path.h"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

// A whole server, started on loopback and ticked from this thread as Run() ticks it: what the thread
// pools are wired to is only visible with all of it up.
namespace ServerThreadingTest {
    struct Server final: Framework::Integrations::Server::Instance {
        std::filesystem::path root;
        int32_t port = 0;
        std::vector<int> squares;
        uint32_t lanesUsed = 0;

        // A game's own per-tick loop, spread over the replication workers.
        void PostUpdate() override {
            squares.assign(256, 0);
            std::atomic<uint32_t> lanes {0};
            GetTickWorkers()->Run(static_cast<uint32_t>(squares.size()), [&](uint32_t i, uint32_t lane) {
                squares[i] = static_cast<int>(i * i);
                lanes.fetch_or(1u << lane);
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            });
            lanesUsed |= lanes.load();
        }

        static void Write(const std::filesystem::path &path, const std::string &text) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
        }

        bool Start(const char *name, int32_t replicationThreads, int32_t backgroundThreads) {
            root = std::filesystem::temp_directory_path() / name;
            std::filesystem::remove_all(root);
            Write(root / "resources" / "demo" / "package.json", R"({"name":"demo","version":"1.0.0","main":"server.js","mafiahub":{"server":"server.js","client":"client.js"}})");
            Write(root / "resources" / "demo" / "server.js", "globalThis.started = true;");
            Write(root / "resources" / "demo" / "client.js", "console.log('one');");

            static char arg0[] = "server";
            static char *argv[] = {arg0, nullptr};
            std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
            for (int attempt = 0; attempt < 8; ++attempt) {
                Framework::Integrations::Server::InstanceOptions opts;
                opts.modSlug            = "threading_ut";
                opts.modName            = "Threading test";
                opts.modVersion         = "1.0.0";
                opts.gameName           = "test";
                opts.gameVersion        = "1";
                opts.bindHost           = "127.0.0.1";
                opts.bindPort           = 42000 + static_cast<int32_t>(random() % 20000);
                opts.maxPlayers         = 8;
                opts.bindPublicServer   = false;
                opts.enableSignals      = false;
                opts.modConfigFile      = (root / "server.json").string();
                opts.resourcesPath      = (root / "resources").string();
                opts.replicationThreads = replicationThreads;
                opts.backgroundThreads  = backgroundThreads;
                opts.argc               = 1;
                opts.argv               = argv;
                if (Init(opts)) {
                    port = opts.bindPort;
                    return true;
                }
                Shutdown();
                return false;
            }
            return false;
        }

        // Ticks until `done` or the time is up.
        template <typename Done>
        bool TickUntil(Done done, std::chrono::milliseconds limit = std::chrono::seconds(10)) {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (!done()) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    return false;
                }
                Update();
            }
            return true;
        }

        // The staging directory sits beside the resources folder, which the server resolves against its executable.
        std::filesystem::path Staging() const {
            return std::filesystem::path(Framework::Utils::GetAbsolutePathA((root / "resources").string())).parent_path() / ".packages";
        }

        std::filesystem::path Package() const {
            return Staging() / "demo.fwpak";
        }

        void Stop() {
            Shutdown();
            std::error_code code;
            std::filesystem::remove_all(Staging().parent_path(), code);
            std::filesystem::remove_all(root, code);
        }
    };
} // namespace ServerThreadingTest

MODULE(server_threading, {
    using ServerThreadingTest::Server;

    IT("starts a server with the thread pools it was asked for and shares them with the game", {
        Server server;
        EQUALS(server.Start("fw_server_threading_pools", 3, 2), true);
        EQUALS(Framework::CoreModules::GetTaskScheduler() == server.GetScheduler(), true);
        EQUALS(Framework::CoreModules::GetTickWorkers() == server.GetTickWorkers(), true);
        EQUALS(server.GetScheduler()->BackgroundWorkers(), 2u);
        // Three workers and the tick thread.
        EQUALS(server.GetTickWorkers()->Lanes(), 4u);

        for (int i = 0; i < 5; ++i) {
            server.Update();
        }
        // The game's loop in PostUpdate ran whole, on more than the tick thread.
        EQUALS(server.squares.size(), static_cast<size_t>(256));
        EQUALS(server.squares[255], 255 * 255);
        GREATER(std::popcount(server.lanesUsed), 1);
        server.Stop();
        EQUALS(Framework::CoreModules::GetTaskScheduler() == nullptr, true);
    });

    IT("runs background work off the tick thread and its completion inside a tick", {
        Server server;
        EQUALS(server.Start("fw_server_threading_background", 0, 2), true);
        const auto tickThread = std::this_thread::get_id();
        std::atomic<bool> workedElsewhere {false};
        bool completedInTick = false;
        Framework::CoreModules::GetTaskScheduler()->RunInBackground(
            [&] {
                workedElsewhere = std::this_thread::get_id() != tickThread;
            },
            [&] {
                completedInTick = std::this_thread::get_id() == tickThread;
            });
        EQUALS(server.TickUntil([&] {
            return completedInTick;
        }),
            true);
        EQUALS(workedElsewhere.load(), true);
        server.Stop();
    });

    IT("answers a tick-thread web route from inside the server's tick", {
        Server server;
        EQUALS(server.Start("fw_server_threading_route", 0, 1), true);
        const auto tickThread = std::this_thread::get_id();
        std::atomic<bool> onTick {false};
        server.GetWebserver()->RegisterTickThreadRequest("/world", [&](const httplib::Request &, httplib::Response &res) {
            onTick = std::this_thread::get_id() == tickThread;
            // The replicated world, which only the tick thread may read.
            res.set_content(std::to_string(Framework::CoreModules::GetReplication()->GetConnectionCount()), "text/plain");
        });

        std::atomic<int> status {0};
        std::string body;
        std::thread client([&] {
            httplib::Client http("127.0.0.1", server.port);
            if (const auto res = http.Get("/world")) {
                body   = res->body;
                status = res->status;
            }
            else {
                status = -1;
            }
        });
        EQUALS(server.TickUntil([&] {
            return status.load() != 0;
        }),
            true);
        client.join();
        EQUALS(status.load(), 200);
        EQUALS(body == "0", true);
        EQUALS(onTick.load(), true);
        server.Stop();
    });

    IT("packages every client resource at boot, then a hot reload off the tick thread", {
        Server server;
        EQUALS(server.Start("fw_server_threading_reload", 3, 2), true);
        EQUALS(std::filesystem::exists(server.Package()), true);
        const std::string before = Framework::Utils::Crypto::Sha256FileHex(server.Package().string());
        EQUALS(server.TickUntil([&] {
            return server.GetScriptingModule()->GetResourceManager()->IsResourceRunning("demo");
        }),
            true);

        Server::Write(server.root / "resources" / "demo" / "client.js", "console.log('two, changed');");
        EQUALS(static_cast<bool>(server.GetScriptingModule()->GetResourceManager()->RestartResource("demo")), true);

        // The restart returned without the package having been rebuilt on this thread: what clients are
        // served is still what they were told, until a tick puts the new one in place.
        EQUALS(Framework::Utils::Crypto::Sha256FileHex(server.Package().string()) == before, true);
        EQUALS(server.TickUntil([&] {
            return Framework::Utils::Crypto::Sha256FileHex(server.Package().string()) != before;
        }),
            true);
        EQUALS(std::filesystem::exists(server.Package().string() + ".next"), false);
        EQUALS(server.GetScheduler()->PendingBackground(), static_cast<size_t>(0));
        server.Stop();
    });
});
