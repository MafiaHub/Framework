/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "integrations/server/instance.h"
#include "scripting/engine_scope.h"
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"
#include "utils/path.h"

#include <httplib.h>
#include <v8pp/convert.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>

// Whole servers again, this time for what the script thread changes: who waits for whom.
namespace ServerScriptThreadTest {
    // A port nothing listens on, as far as a TCP bind on loopback can tell; 0 when eight tries found none. A server
    // cannot be initialised twice, so the port is settled before Init rather than by retrying it.
    inline int32_t FreePort() {
        std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
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

    using Clock = std::chrono::steady_clock;

    struct Server final: Framework::Integrations::Server::Instance {
        std::filesystem::path root;
        bool started = false;

        // Longest stretch between two ticks reaching PostUpdate, in milliseconds.
        double longestGapMs = 0.0;
        Clock::time_point lastTick {};
        int ticks = 0;

        // Raised by the tick for as long as it is in PostUpdate; a script's native call that sees it raised ran
        // while the tick had the world.
        std::atomic<bool> inTick {false};
        std::atomic<long> touches {0};
        std::atomic<long> touchesDuringTick {0};

        void PostUpdate() override {
            const auto now = Clock::now();
            if (lastTick != Clock::time_point {}) {
                longestGapMs = std::max(longestGapMs, std::chrono::duration<double, std::milli>(now - lastTick).count());
            }
            lastTick = now;
            ++ticks;
            inTick = true;
            std::this_thread::sleep_for(std::chrono::microseconds(300));
            inTick = false;
        }

        // touchWorld(): a native as a project would bind one, standing in for anything that reads the world.
        void ModuleRegister(Framework::Scripting::Engine *engine) override {
            v8::Isolate *isolate = engine->GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            if (context.IsEmpty()) {
                return;
            }
            v8::MaybeLocal<v8::Function> touch = v8::Function::New(
                context,
                [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                    auto *self = static_cast<Server *>(info.Data().As<v8::External>()->Value());
                    ++self->touches;
                    if (self->inTick) {
                        ++self->touchesDuringTick;
                    }
                },
                v8::External::New(isolate, this));
            context->Global()->Set(context, v8pp::to_v8(isolate, "touchWorld"), touch.ToLocalChecked()).Check();
        }

        static void Write(const std::filesystem::path &path, const std::string &text) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
        }

        std::string Marker(const char *name) const {
            return (root / name).generic_string();
        }

        bool Start(const char *name, bool scriptThread, const std::string &serverScript) {
            root = std::filesystem::temp_directory_path() / name;
            std::filesystem::remove_all(root);
            Write(root / "resources" / "demo" / "package.json", R"({"name":"demo","version":"1.0.0","main":"server.js","mafiahub":{"server":"server.js"}})");
            std::string script = serverScript;
            // $ROOT in a script is this server's scratch folder.
            for (size_t at = script.find("$ROOT"); at != std::string::npos; at = script.find("$ROOT")) {
                script.replace(at, 5, root.generic_string());
            }
            Write(root / "resources" / "demo" / "server.js", script);

            static char arg0[]  = "server";
            static char *argv[] = {arg0, nullptr};
            Framework::Integrations::Server::InstanceOptions opts;
            opts.modSlug            = "script_thread_ut";
            opts.modName            = "Script thread test";
            opts.modVersion         = "1.0.0";
            opts.gameName           = "test";
            opts.gameVersion        = "1";
            opts.bindHost           = "127.0.0.1";
            opts.bindPort           = FreePort();
            opts.maxPlayers         = 8;
            opts.bindPublicServer   = false;
            opts.enableSignals      = false;
            opts.modConfigFile      = (root / "server.json").string();
            opts.resourcesPath      = (root / "resources").string();
            opts.replicationThreads = 0;
            opts.backgroundThreads  = 1;
            opts.scriptThread       = scriptThread;
            // The slow-script warnings are the point of some of these scripts.
            opts.slowScriptWarnMs = 0;
            opts.argc             = 1;
            opts.argv             = argv;
            auto result = Init(opts);
            if (!result) {
                fprintf(stderr, "server_script_thread: Init failed: %s\n", result.GetError().message.c_str());
                Shutdown();
                return false;
            }
            started = true;
            return true;
        }

        // Ticks, as Run() does, for this long.
        void TickFor(std::chrono::milliseconds duration) {
            const auto deadline = Clock::now() + duration;
            while (Clock::now() < deadline) {
                Update();
            }
        }

        template <typename Done>
        bool TickUntil(Done done, std::chrono::milliseconds limit = std::chrono::seconds(10)) {
            const auto deadline = Clock::now() + limit;
            while (!done()) {
                if (Clock::now() >= deadline) {
                    return false;
                }
                Update();
            }
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

        // Raises consoleCommand in every resource, the way the server raises its own events. Wherever it is called.
        void EmitConsoleCommand(const std::string &command) {
            auto *engine = GetScriptingModule()->GetEngine();
            Framework::Scripting::EngineScope scope(engine);
            std::vector<v8::Local<v8::Value>> args {v8pp::to_v8(scope.Isolate(), command), v8::Array::New(scope.Isolate(), 0)};
            GetScriptingModule()->GetResourceManager()->GetEvents().EmitReserved(scope.Isolate(), scope.Context(), "consoleCommand", args);
        }
    };

    // A script that computes for 600 ms out of every 650, and leaves a file once it has done so a few times. Long
    // on purpose: a hosted CI runner stalls a thread for tens of milliseconds on its own, and what is measured has
    // to stand well clear of that.
    inline const char *kBusyScript = R"(
        const fs = require('node:fs');
        let rounds = 0;
        setInterval(() => {
            const end = Date.now() + 600;
            let spins = 0;
            while (Date.now() < end) { spins++; }
            if (++rounds === 3) { fs.writeFileSync('$ROOT/busy', String(spins)); }
        }, 650);
    )";
} // namespace ServerScriptThreadTest

MODULE(server_script_thread, {
    using ServerScriptThreadTest::kBusyScript;
    using ServerScriptThreadTest::Server;
    using namespace std::chrono_literals;

    IT("keeps ticking while a script computes, with scripts on their own thread", {
        Server server;
        const bool up = server.Start("fw_script_thread_busy", true, kBusyScript);
        EQUALS(up, true);
        if (!up) {
            break;
        }
        EQUALS(server.ScriptsHaveOwnThread(), true);
        EQUALS(server.TickUntil([&] {
            return std::filesystem::exists(server.Marker("busy"));
        }),
            true);
        fprintf(stderr, "server_script_thread: longest gap between ticks %.1f ms over %d ticks\n", server.longestGapMs, server.ticks);
        // The script held its thread for 600 ms at a time, three times over, and no tick waited that out.
        LESSER(server.longestGapMs, 300.0);
        GREATER(server.ticks, 20);
        server.Stop();
    });

    IT("keeps ticking while a script computes after a native call that called back into script", {
        Server server;
        const bool up = server.Start("fw_script_thread_nested", true, R"(
            const fs = require('node:fs');
            let rounds = 0;
            let heard = 0;
            Events.on('ping', () => { heard++; });
            setInterval(() => {
                // Native code that calls back into script, and returns. Only while it is on the stack is this no
                // safe point; the loop after it is the script's own again.
                Events.emit('ping');
                const end = Date.now() + 600;
                while (Date.now() < end) {}
                if (++rounds === 3) { fs.writeFileSync('$ROOT/busy', String(heard)); }
            }, 650);
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        EQUALS(server.TickUntil([&] {
            return std::filesystem::exists(server.Marker("busy"));
        }),
            true);
        fprintf(stderr, "server_script_thread: after a nested call, longest gap between ticks %.1f ms over %d ticks\n", server.longestGapMs, server.ticks);
        LESSER(server.longestGapMs, 300.0);
        std::string heard;
        std::ifstream(server.Marker("busy")) >> heard;
        EQUALS(heard == "3", true);
        server.Stop();
    });

    IT("waits for that script with scripts inside the tick, as before", {
        Server server;
        const bool up = server.Start("fw_script_thread_busy_inline", false, kBusyScript);
        EQUALS(up, true);
        if (!up) {
            break;
        }
        EQUALS(server.ScriptsHaveOwnThread(), false);
        EQUALS(server.TickUntil([&] {
            return std::filesystem::exists(server.Marker("busy"));
        }),
            true);
        GREATER(server.longestGapMs, 500.0);
        server.Stop();
    });

    IT("never runs a script's native call while the tick has the world", {
        Server server;
        const bool up = server.Start("fw_script_thread_touch", true, R"(
            setInterval(() => { for (let i = 0; i < 5000; i++) { touchWorld(); } }, 1);
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        server.TickFor(1500ms);
        GREATER(server.touches.load(), 10000L);
        EQUALS(server.touchesDuringTick.load(), 0L);
        GREATER(server.ticks, 10);
        server.Stop();
    });

    IT("runs what the tick thread posts to scripts, and its handlers, off the tick", {
        Server server;
        const bool up = server.Start("fw_script_thread_post", true, R"(
            const fs = require('node:fs');
            Events.on('consoleCommand', (command) => {
                // A handler that takes its time: the tick that raised the event does not wait for it.
                const end = Date.now() + 600;
                while (Date.now() < end) {}
                fs.writeFileSync('$ROOT/handled', command);
            });
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        server.TickFor(200ms);
        const auto tickThread = std::this_thread::get_id();
        std::atomic<bool> postedElsewhere {false};
        server.PostToScripts([&] {
            postedElsewhere = std::this_thread::get_id() != tickThread;
            server.EmitConsoleCommand("from-the-tick");
        });
        server.longestGapMs = 0.0;
        EQUALS(server.TickUntil([&] {
            return std::filesystem::exists(server.Marker("handled"));
        }),
            true);
        EQUALS(postedElsewhere.load(), true);
        LESSER(server.longestGapMs, 300.0);
        std::string handled;
        std::ifstream(server.Marker("handled")) >> handled;
        EQUALS(handled == "from-the-tick", true);
        server.Stop();
    });

    IT("still lets tick-thread code enter script itself, once the running script is out of the way", {
        Server server;
        const bool up = server.Start("fw_script_thread_enter", true, std::string(kBusyScript) + R"(
            Events.on('consoleCommand', (command) => { fs.writeFileSync('$ROOT/direct', command); });
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        // Until the script is in the middle of its work.
        server.TickFor(900ms);
        // The way server code always raised events: an EngineScope on the tick thread. It has to wait for the
        // script, which is why PostToScripts exists, but it must work and must not deadlock.
        server.EmitConsoleCommand("from-the-tick-thread");
        EQUALS(std::filesystem::exists(server.Marker("direct")), true);
        // And the same through the resource manager, which frees and starts runtimes.
        EQUALS(static_cast<bool>(server.GetScriptingModule()->GetResourceManager()->RestartResource("demo")), true);
        server.TickFor(100ms);
        GREATER(server.ticks, 10);
        server.Stop();
    });

    IT("shuts down while a script is in the middle of computing", {
        Server server;
        const bool up = server.Start("fw_script_thread_shutdown", true, kBusyScript);
        EQUALS(up, true);
        if (!up) {
            break;
        }
        server.TickFor(900ms);
        const auto started = std::chrono::steady_clock::now();
        server.Stop();
        // It lets the script's current callback finish, which is at most its 600 ms.
        const double stopMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        LESSER(stopMs, 3000.0);
    });
});
