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
            // The runtime being set up is the one the caller is inside.
            if (v8::Isolate *current = v8::Isolate::TryGetCurrent(); current != nullptr && current != isolate) {
                ++registeredElsewhere;
            }
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
            // callBack(fn): a native that calls back into script and returns, as any binding that constructs a
            // wrapped object does (v8pp runs the class's constructor function).
            v8::MaybeLocal<v8::Function> callBack = v8::Function::New(context, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                if (info.Length() > 0 && info[0]->IsFunction()) {
                    (void)info[0].As<v8::Function>()->Call(info.GetIsolate()->GetCurrentContext(), v8::Undefined(info.GetIsolate()), 0, nullptr);
                }
            });
            context->Global()->Set(context, v8pp::to_v8(isolate, "callBack"), callBack.ToLocalChecked()).Check();
        }

        static void Write(const std::filesystem::path &path, const std::string &text) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
        }

        std::string Marker(const char *name) const {
            return (root / name).generic_string();
        }

        bool Start(const char *name, const std::string &serverScript) {
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

        // Raises an event the way projects do from an RPC handler on the tick thread: V8's own scopes on the
        // engine's isolate, values built there, and an emit. No EngineScope, no hand-over.
        std::atomic<int> registeredElsewhere {0};
        enum class Extra { None, Promise, Function };
        Extra extra = Extra::None;
        static inline std::atomic<int> functionCalls {0};

        template <typename Emit>
        auto WithProjectScopes(Emit emit) {
            auto *engine         = GetScriptingModule()->GetEngine();
            v8::Isolate *isolate = engine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine->GetContext();
            v8::Context::Scope contextScope(context);
            std::vector<v8::Local<v8::Value>> args {v8pp::to_v8(isolate, std::string("from-a-project")), v8::Array::New(isolate, 0)};
            if (extra == Extra::Promise) {
                // Cannot be copied to another isolate.
                args.push_back(v8::Promise::Resolver::New(context).ToLocalChecked()->GetPromise());
            }
            else if (extra == Extra::Function) {
                // Crosses as a reference: whoever calls it runs it back in this thread's isolate.
                args.push_back(v8::Function::New(context, [](const v8::FunctionCallbackInfo<v8::Value> &) {
                    functionCalls++;
                }).ToLocalChecked());
            }
            return emit(isolate, context, GetScriptingModule()->GetResourceManager()->GetEvents(), args);
        }

        void EmitAsProjectsDo(const std::string &eventName) {
            WithProjectScopes([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::Builtins::Events &events, std::vector<v8::Local<v8::Value>> &args) {
                events.EmitReserved(isolate, context, eventName, args);
                return 0;
            });
        }

        bool EmitSyncAsProjectsDo(const std::string &eventName) {
            return WithProjectScopes([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::Builtins::Events &events, std::vector<v8::Local<v8::Value>> &args) {
                return events.EmitReservedSync(isolate, context, eventName, args);
            });
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
        const bool up = server.Start("fw_script_thread_busy", kBusyScript);
        EQUALS(up, true);
        if (!up) {
            break;
        }
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
        const bool up = server.Start("fw_script_thread_nested", R"(
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
        // Its content, not its existence: the file is there, empty, a moment before the script has written it.
        EQUALS(server.TickUntil([&] {
            std::string heard;
            std::ifstream(server.Marker("busy")) >> heard;
            return heard == "3";
        }),
            true);
        fprintf(stderr, "server_script_thread: after a nested call, longest gap between ticks %.1f ms over %d ticks\n", server.longestGapMs, server.ticks);
        LESSER(server.longestGapMs, 300.0);
        server.Stop();
    });

    IT("keeps ticking while a script computes beside other timers of its own", {
        // What a real resource looks like, and what M2O's showed: several timers at different intervals. Node runs
        // its queued callbacks between two timer lists from native code, which is a call back into script that has
        // long returned by the time the next timer computes.
        Server server;
        const bool up = server.Start("fw_script_thread_timers", R"(
            const fs = require('node:fs');
            let rounds = 0;
            setInterval(() => { callBack(() => touchWorld()); }, 5);
            setInterval(() => { Promise.resolve().then(() => callBack(() => {})); }, 7);
            setInterval(() => {
                const end = Date.now() + 600;
                while (Date.now() < end) {}
                if (++rounds === 3) { fs.writeFileSync('$ROOT/busy', 'done'); }
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
        fprintf(stderr, "server_script_thread: beside other timers, longest gap between ticks %.1f ms over %d ticks\n", server.longestGapMs, server.ticks);
        LESSER(server.longestGapMs, 300.0);
        server.Stop();
    });

    IT("does not take the world from a script while native code it called is still on the stack", {
        Server server;
        const bool up = server.Start("fw_script_thread_held", R"(
            const fs = require('node:fs');
            let rounds = 0;
            setInterval(() => {
                // The native is in the middle of its call for as long as the function it was handed runs: it may
                // hold on to things a tick would free, so the tick has to wait this one out.
                callBack(() => {
                    const end = Date.now() + 400;
                    while (Date.now() < end) {}
                });
                if (++rounds === 2) { fs.writeFileSync('$ROOT/held', 'done'); }
            }, 450);
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        EQUALS(server.TickUntil([&] {
            return std::filesystem::exists(server.Marker("held"));
        }),
            true);
        fprintf(stderr, "server_script_thread: under a native call, longest gap between ticks %.1f ms\n", server.longestGapMs);
        GREATER(server.longestGapMs, 300.0);
        server.Stop();
    });

    IT("never runs a script's native call while the tick has the world", {
        Server server;
        const bool up = server.Start("fw_script_thread_touch", R"(
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
        const bool up = server.Start("fw_script_thread_post", R"(
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
        // Its content, not its existence: the file is there, empty, a moment before the script has written it.
        EQUALS(server.TickUntil([&] {
            std::string handled;
            std::ifstream(server.Marker("handled")) >> handled;
            return handled == "from-the-tick";
        }),
            true);
        EQUALS(postedElsewhere.load(), true);
        LESSER(server.longestGapMs, 300.0);
        server.Stop();
    });

    IT("raises a project's event from the tick thread without waiting for a script that is computing", {
        Server server;
        const bool up = server.Start("fw_script_thread_project", std::string(kBusyScript) + R"(
            Events.on('projectEvent', (text) => { fs.writeFileSync('$ROOT/project', text); });
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        // Until the script is in the middle of its 600 ms.
        server.TickFor(900ms);
        const auto before = std::chrono::steady_clock::now();
        server.EmitAsProjectsDo("projectEvent");
        const double emitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
        fprintf(stderr, "server_script_thread: a project's emit from the tick thread took %.2f ms\n", emitMs);
        // The emit only hands the event over: the handler has not run, and the tick thread did not wait for the script.
        LESSER(emitMs, 100.0);
        EQUALS(server.TickUntil([&] {
            std::string text;
            std::ifstream(server.Marker("project")) >> text;
            return text == "from-a-project";
        }),
            true);
        server.Stop();
    });

    IT("raises an event from a second thread while the tick thread is inside its own isolate", {
        Server server;
        const bool up = server.Start("fw_script_thread_packet", R"(
            const fs = require('node:fs');
            Events.on('packetEvent', (text) => { fs.writeFileSync('$ROOT/packet', text); });
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        server.TickFor(300ms);
        auto *engine = server.GetScriptingModule()->GetEngine();
        std::atomic<bool> held {false};
        std::atomic<double> emitMs {-1.0};
        std::thread packets([&] {
            engine->BindOffThreadRuntime("packet");
            while (!held) {
                std::this_thread::yield();
            }
            const auto before = std::chrono::steady_clock::now();
            server.EmitAsProjectsDo("packetEvent");
            emitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
        });
        {
            // Tick code in the middle of building its own values.
            v8::Locker locker(engine->GetIsolate());
            held = true;
            std::this_thread::sleep_for(500ms);
        }
        packets.join();
        fprintf(stderr, "server_script_thread: an emit from a second thread took %.2f ms\n", emitMs.load());
        // It built its arguments in a runtime of its own, so it did not wait for the tick thread's.
        LESSER(emitMs.load(), 100.0);
        EQUALS(server.TickUntil([&] {
            std::string text;
            std::ifstream(server.Marker("packet")) >> text;
            return text == "from-a-project";
        }),
            true);
        server.Stop();
    });

    IT("brings a project's synchronous event back with the handlers' veto", {
        Server server;
        const bool up = server.Start("fw_script_thread_veto", R"(
            const fs = require('node:fs');
            let asked = 0;
            Events.on('mayProceed', () => { asked++; fs.writeFileSync('$ROOT/asked', String(asked)); return true; });
            Events.on('mayNot', () => false);
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        server.TickFor(300ms);
        // The handlers ran on the script thread and their answer came back to this one.
        EQUALS(server.EmitSyncAsProjectsDo("mayProceed"), true);
        EQUALS(std::filesystem::exists(server.Marker("asked")), true);
        EQUALS(server.EmitSyncAsProjectsDo("mayNot"), false);
        EQUALS(server.EmitSyncAsProjectsDo("nobodyListens"), true);
        server.Stop();
    });

    IT("does not let a synchronous event through when it could not be raised", {
        Server server;
        const bool up = server.Start("fw_script_thread_unraised", R"(
            Events.on('mayNot', () => false);
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        server.TickFor(300ms);
        // The handler that would have said no never saw it: that is not a yes.
        server.extra = Server::Extra::Promise;
        EQUALS(server.EmitSyncAsProjectsDo("mayNot"), false);
        EQUALS(server.EmitSyncAsProjectsDo("nobodyListens"), false);
        server.Stop();
    });

    IT("lets a synchronous event's handler call a function the tick thread handed it", {
        Server server;
        const bool up = server.Start("fw_script_thread_callback", R"(
            Events.on('withCallback', (text, list, callback) => { callback(); return true; });
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        server.TickFor(300ms);
        Server::functionCalls = 0;
        server.extra          = Server::Extra::Function;
        // The function lives in the isolate this thread is inside while it waits for the answer.
        EQUALS(server.EmitSyncAsProjectsDo("withCallback"), true);
        EQUALS(Server::functionCalls.load(), 1);
        server.Stop();
    });

    IT("lets a synchronous event's handler call a function a second thread handed it", {
        Server server;
        const bool up = server.Start("fw_script_thread_packet_callback", R"(
            Events.on('withCallback', (text, list, callback) => { callback(); return true; });
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        server.TickFor(300ms);
        Server::functionCalls = 0;
        server.extra          = Server::Extra::Function;
        std::atomic<bool> proceeded {false};
        std::thread packets([&] {
            server.GetScriptingModule()->GetEngine()->BindOffThreadRuntime("packet");
            // The function lives in that thread's own isolate, which it lets go of while it waits for the answer.
            proceeded = server.EmitSyncAsProjectsDo("withCallback");
        });
        packets.join();
        EQUALS(proceeded.load(), true);
        EQUALS(Server::functionCalls.load(), 1);
        server.Stop();
    });

    IT("raises events that carry a function from two threads at once", {
        Server server;
        const bool up = server.Start("fw_script_thread_two_emitters", R"(
            Events.on('withCallback', (text, list, callback) => { callback(); });
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        server.TickFor(300ms);
        Server::functionCalls = 0;
        server.extra          = Server::Extra::Function;
        const int each        = 500;
        std::atomic<bool> go {false};
        std::thread packets([&] {
            server.GetScriptingModule()->GetEngine()->BindOffThreadRuntime("packet");
            while (!go) {
                std::this_thread::yield();
            }
            for (int i = 0; i < each; ++i) {
                server.EmitAsProjectsDo("withCallback");
            }
        });
        go = true;
        for (int i = 0; i < each; ++i) {
            server.EmitAsProjectsDo("withCallback");
            // The script thread calls the functions back while both threads go on handing it new ones.
            if (i % 10 == 9) {
                server.Update();
            }
        }
        packets.join();
        EQUALS(server.TickUntil([&] {
            return Server::functionCalls.load() >= 2 * each;
        }),
            true);
        server.TickFor(100ms);
        EQUALS(Server::functionCalls.load(), 2 * each);
        server.Stop();
    });

    IT("leaves a thread that binds a runtime nobody created on the tick thread's", {
        Server server;
        const bool up = server.Start("fw_script_thread_unknown", "");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        auto *engine                   = server.GetScriptingModule()->GetEngine();
        v8::Isolate *const tickIsolate = engine->GetIsolate();
        v8::Isolate *bound             = nullptr;
        v8::Isolate *afterUnknown      = nullptr;
        std::thread other([&] {
            engine->BindOffThreadRuntime("packet");
            bound = engine->GetIsolate();
            engine->BindOffThreadRuntime("nobody-created-this");
            afterUnknown = engine->GetIsolate();
        });
        other.join();
        EQUALS(bound != nullptr, true);
        EQUALS(bound != tickIsolate, true);
        EQUALS(afterUnknown == tickIsolate, true);
        server.Stop();
    });

    IT("forgets what a thread bound once that server is gone", {
        v8::Isolate *boundBefore = nullptr;
        v8::Isolate *tickBefore  = nullptr;
        {
            Server first;
            const bool firstUp = first.Start("fw_script_thread_stale_first", "");
            EQUALS(firstUp, true);
            if (!firstUp) {
                break;
            }
            auto *engine = first.GetScriptingModule()->GetEngine();
            tickBefore   = engine->GetIsolate();
            engine->BindOffThreadRuntime("packet");
            boundBefore = engine->GetIsolate();
            first.Stop();
        }
        EQUALS(boundBefore != tickBefore, true);

        Server second;
        const bool secondUp = second.Start("fw_script_thread_stale_second", "");
        EQUALS(secondUp, true);
        if (!secondUp) {
            break;
        }
        auto *engine = second.GetScriptingModule()->GetEngine();
        // A thread that never bound anything sees "tick"; so does this one, whose binding was the first server's.
        v8::Isolate *unbound = nullptr;
        std::thread other([&] {
            unbound = engine->GetIsolate();
        });
        other.join();
        EQUALS(engine->GetIsolate() == unbound, true);
        second.Stop();
    });

    IT("still lets another thread start and stop resources, once the running script is out of the way", {
        Server server;
        // Uses a binding as it loads: a runtime started from the tick thread has to have its own.
        const bool up = server.Start("fw_script_thread_enter", std::string(kBusyScript) + R"(
            touchWorld();
            fs.appendFileSync('$ROOT/loads', 'x');
        )");
        EQUALS(up, true);
        if (!up) {
            break;
        }
        server.TickFor(900ms);
        // Goes into the script side's runtimes, so it has to wait for the script; it must work and not deadlock.
        EQUALS(static_cast<bool>(server.GetScriptingModule()->GetResourceManager()->RestartResource("demo")), true);
        EQUALS(server.TickUntil([&] {
            std::string loads;
            std::ifstream(server.Marker("loads")) >> loads;
            return loads == "xx";
        }),
            true);
        EQUALS(server.registeredElsewhere.load(), 0);
        GREATER(server.ticks, 10);
        server.Stop();
    });

    IT("shuts down while a script is in the middle of computing", {
        Server server;
        const bool up = server.Start("fw_script_thread_shutdown", kBusyScript);
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
