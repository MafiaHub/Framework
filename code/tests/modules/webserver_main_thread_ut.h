/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "http/webserver.h"
#include "utils/task_scheduler.h"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

namespace WebserverMainThreadTest {
    // A web server on a free loopback port; 0 when none could be bound.
    inline int32_t Listen(Framework::HTTP::Webserver &server) {
        std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
        for (int attempt = 0; attempt < 8; ++attempt) {
            const int32_t candidate = 42000 + static_cast<int32_t>(random() % 20000);
            if (server.Init("127.0.0.1", candidate, "") == Framework::HTTP::WebserverError::WEBSERVER_NONE) {
                return candidate;
            }
        }
        return 0;
    }

    struct Answer {
        int status = 0;
        std::string body;
    };

    // GETs `path` from another thread while this one drains the scheduler, as the server's tick does.
    inline Answer GetWhileDraining(int32_t port, const char *path, Framework::Utils::TaskScheduler &scheduler) {
        Answer answer;
        std::atomic<bool> done {false};
        std::thread client([&] {
            httplib::Client http("127.0.0.1", port);
            if (const auto res = http.Get(path)) {
                answer.status = res->status;
                answer.body   = res->body;
            }
            done = true;
        });
        while (!done) {
            scheduler.Drain();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        client.join();
        return answer;
    }
} // namespace WebserverMainThreadTest

MODULE(webserver_main_thread, {
    using namespace WebserverMainThreadTest;
    using Framework::Utils::TaskScheduler;

    IT("runs a tick-thread route on the thread that drains, and a plain route on a web worker", {
        TaskScheduler scheduler(0);
        Framework::HTTP::Webserver server;
        const int32_t port = Listen(server);
        NEQUALS(port, 0);
        server.SetTickThreadScheduler(&scheduler);

        const auto owner = std::this_thread::get_id();
        std::atomic<bool> tickRouteOnOwner {false};
        std::atomic<bool> plainRouteOnOwner {true};
        server.RegisterTickThreadRequest("/world", [&](const httplib::Request &, httplib::Response &res) {
            tickRouteOnOwner = std::this_thread::get_id() == owner;
            res.set_content("from the tick", "text/plain");
        });
        server.RegisterRequest("/plain", [&](const httplib::Request &, httplib::Response &res) {
            plainRouteOnOwner = std::this_thread::get_id() == owner;
            res.set_content("from a worker", "text/plain");
        });

        const Answer world = GetWhileDraining(port, "/world", scheduler);
        EQUALS(world.status, 200);
        EQUALS(world.body == "from the tick", true);
        EQUALS(tickRouteOnOwner.load(), true);

        const Answer plain = GetWhileDraining(port, "/plain", scheduler);
        EQUALS(plain.status, 200);
        EQUALS(plainRouteOnOwner.load(), false);
        server.Shutdown();
    });

    IT("answers 503 when the tick thread does not come round in time, and never runs the handler late", {
        TaskScheduler scheduler(0);
        Framework::HTTP::Webserver server;
        const int32_t port = Listen(server);
        NEQUALS(port, 0);
        server.SetTickThreadScheduler(&scheduler, std::chrono::milliseconds(100));
        std::atomic<int> ran {0};
        server.RegisterTickThreadRequest("/world", [&](const httplib::Request &, httplib::Response &res) {
            ++ran;
            res.set_content("late", "text/plain");
        });

        // Nobody drains: the tick thread is stuck for longer than the route waits.
        httplib::Client http("127.0.0.1", port);
        const auto res = http.Get("/world");
        const int status = res ? res->status : 0;
        EQUALS(status, 503);
        // The request and response it would have written to are gone by now.
        scheduler.Drain();
        EQUALS(ran.load(), 0);
        server.Shutdown();
    });

    IT("answers 503 from a tick-thread route when no scheduler was given", {
        Framework::HTTP::Webserver server;
        const int32_t port = Listen(server);
        NEQUALS(port, 0);
        std::atomic<int> ran {0};
        server.RegisterTickThreadRequest("/world", [&](const httplib::Request &, httplib::Response &) {
            ++ran;
        });
        httplib::Client http("127.0.0.1", port);
        const auto res = http.Get("/world");
        const int status = res ? res->status : 0;
        EQUALS(status, 503);
        EQUALS(ran.load(), 0);
        server.Shutdown();
    });

    IT("answers 500 when a tick-thread handler throws, and keeps serving", {
        TaskScheduler scheduler(0);
        Framework::HTTP::Webserver server;
        const int32_t port = Listen(server);
        NEQUALS(port, 0);
        server.SetTickThreadScheduler(&scheduler);
        server.RegisterTickThreadRequest("/broken", [](const httplib::Request &, httplib::Response &) {
            throw std::runtime_error("handler bug");
        });
        server.RegisterTickThreadRequest("/fine", [](const httplib::Request &, httplib::Response &res) {
            res.set_content("ok", "text/plain");
        });
        EQUALS(GetWhileDraining(port, "/broken", scheduler).status, 500);
        EQUALS(GetWhileDraining(port, "/fine", scheduler).status, 200);
        server.Shutdown();
    });
});
