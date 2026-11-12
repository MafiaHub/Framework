/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_builtins_core_ut.h"
#include "lua_builtins_player_ut.h"
#include "lua_resources_ut.h"

#include "core_modules.h"
#include "integrations/server/connection_gate.h"
#include "integrations/server/scripting/builtins/pending_connection.h"
#include "integrations/server/scripting/builtins/voice.h"
#include "networking/network_server.h"
#include "voice/server/voice_server.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

// What only the server gives its scripts, declared once for JavaScript and Lua: the admission gate with a gate
// resource written in Lua, held against connection_gate_ut.h, and Voice from both languages.
namespace LuaBuiltinsServerTest {
    using namespace Framework::Scripting;
    using Framework::Integrations::Server::AdmissionDecision;
    using Framework::Integrations::Server::ConnectionGate;
    using Framework::Integrations::Server::Scripting::Builtins::PendingConnection;
    using Framework::Integrations::Server::Scripting::Builtins::Voice;
    using Framework::Voice::VoiceServer;
    using Framework::Voice::VoiceTier;

    // What the gate resource of connection_gate_ut.h does, in Lua: the nickname says what the handler is to do.
    inline constexpr const char *kLuaGate = R"(
        Events.on("playerConnecting", function(connection)
            seen = connection.nickname .. "|" .. connection.ticket .. "|" .. connection.ip .. "|" .. connection.steamId
            local name = connection.nickname
            if name == "refuse" then
                connection:reject("Not on the whitelist")
            elseif name == "refuse-default" then
                connection:reject()
            elseif name == "refuse-long" then
                connection:reject(string.rep("x", 600))
            elseif name == "throw" then
                error("database is down")
            elseif name == "async-admit" then
                sleep(20)
                slept = true
            elseif name == "async-refuse" then
                sleep(20)
                connection:reject("Banned until Friday")
            elseif name == "async-throw" then
                sleep(20)
                error("database went down")
            elseif name == "hang" then
                sleep(60000)
            elseif name == "probe" then
                probe = connection
                pendingDuring = connection:isPending()
            elseif name == "status" then
                sleep(120)
                connection:update("Checking the whitelist")
                sleep(120)
                updated = true
            end
        end)
    )";

    inline constexpr const char *kJsGate = R"(
        const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
        Events.on("playerConnecting", async (connection) => {
            globalThis.seen = `${connection.nickname}|${connection.ticket}|${connection.ip}|${connection.steamId}`;
            switch (connection.nickname) {
            case "js-refuse": connection.reject("JavaScript says no"); return;
            case "js-slow": await sleep(40); globalThis.slept = true; return;
            }
        });
    )";

    // A server scripting module with its admission gate, as the server instance wires them, and no network: the
    // decisions are what the instance acts on, and those are what is asserted.
    struct Admission final {
        LuaResourcesTest::Server server;
        ConnectionGate gate;
        bool published = false;

        ~Admission() {
            // Before the module takes the engine the gate's promises belong to.
            server.module.GetResourceManager()->StopAll();
            gate.Shutdown();
            if (published) {
                Framework::CoreModules::SetConnectionGate(nullptr);
            }
        }

        // Brings the module up with the gate, starts what was written. `timeoutMs` is the admission timeout.
        bool Start(int timeoutMs = 200, Framework::Networking::NetworkServer *network = nullptr) {
            if (!server.Start()) {
                return false;
            }
            gate.Init(&server.module, network, std::chrono::milliseconds(timeoutMs));
            Framework::CoreModules::SetConnectionGate(&gate);
            published = true;
            return true;
        }

        static MafiaNet::RakNetGUID Guid(uint64_t value) {
            MafiaNet::RakNetGUID guid;
            guid.g           = value;
            guid.systemIndex = static_cast<MafiaNet::SystemIndex>(value & 0xFF);
            return guid;
        }

        bool Begin(uint64_t guid, const char *nickname, const char *address = "10.0.0.2") {
            Framework::Networking::RPC::ClientIdentity identity;
            identity.name        = nickname;
            identity.steamId     = "76561198000000000";
            identity.epicId      = "epic";
            identity.discordId   = "discord";
            identity.discordName = "discord-name";
            identity.hardwareId  = "hardware";
            identity.ticket      = "one-time-ticket";
            return gate.Begin(Guid(guid), identity, address);
        }

        // Ticks the module the way the server does until the gate settles something, or gives up.
        std::vector<AdmissionDecision> Settle(int maxMs = 1000) {
            std::vector<AdmissionDecision> out;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(maxMs);
            while (out.empty() && std::chrono::steady_clock::now() < deadline) {
                server.module.Update();
                gate.Collect(out);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return out;
        }

        // "admitted", or "refused: <reason>", for the one decision the gate settles; what else happened otherwise.
        std::string Decide(uint64_t guid, const char *nickname, int maxMs = 1000) {
            if (!Begin(guid, nickname)) {
                return "nothing listens";
            }
            const auto decisions = Settle(maxMs);
            if (decisions.size() != 1) {
                return std::to_string(decisions.size()) + " decisions";
            }
            if (decisions[0].guid.g != guid) {
                return "another connection";
            }
            return decisions[0].admitted ? "admitted" : "refused: " + decisions[0].reason;
        }

        std::string Lua(const std::string &expression, const std::string &resource = "gate") const {
            return server.Eval(resource, expression);
        }

        std::string Js(const std::string &expression, const std::string &resource = "js-gate") const {
            return server.EvalJavaScript(resource, expression);
        }
    };

    // The names of the methods and of the properties a class declares, in the order it declares them.
    inline std::string ClassMembers(const Bind::ClassDesc &described) {
        std::string names;
        for (const Bind::FunctionDesc &function : described.functions) {
            names += (names.empty() ? "" : ",") + function.name + "()";
        }
        for (const Bind::PropertyDesc &property : described.properties) {
            names += (names.empty() ? "" : ",") + property.name;
        }
        return names;
    }

    // The two runtimes of a stage of the Player tests with the Voice of the server module, over a voice server
    // that relays to nobody. `p` and `q` are the two players and `e` an entity nobody owns, in both languages.
    struct Booth final {
        LuaBuiltinsPlayerTest::Stage stage;
        VoiceServer voice;
        uint64_t p;
        uint64_t q;

        explicit Booth(bool withServer = true): p(static_cast<uint64_t>(stage.players[0]->ownerGUID)), q(static_cast<uint64_t>(stage.players[1]->ownerGUID)) {
            stage.js.js.Enter([](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                Voice::Register(isolate, context->Global());
            });
            Voice::RegisterLua(stage.lua.Runtime());
            stage.lua.Run("q = Player.new(" + stage.b + ")");
            stage.js.Eval("globalThis.q = new Player(" + stage.b + "); 0");
            if (withServer) {
                Framework::CoreModules::SetVoiceServer(&voice);
            }
        }

        ~Booth() {
            Framework::CoreModules::SetVoiceServer(nullptr);
        }

        std::string Lua(const std::string &expression) {
            return stage.lua.Eval(expression);
        }

        std::string Js(const std::string &expression) {
            return stage.js.Eval(expression);
        }
    };
} // namespace LuaBuiltinsServerTest

MODULE(lua_builtins_server, {
    using namespace LuaBuiltinsServerTest;
    using LuaBuiltinsCoreTest::Members;

    // ==================== The admission gate ====================

    IT("holds a connection for a gate written in Lua, and admits it when the handler returns", {
        Admission admission;
        admission.server.files.Resource("gate", {{"main.lua", kLuaGate}});
        EQUALS(admission.Start(), true);

        EQUALS(admission.Begin(2, "admit"), true);
        EQUALS(admission.gate.IsPending(2), true);
        const auto decisions = admission.Settle();
        EQUALS(decisions.size(), 1u);
        EQUALS(decisions[0].guid.g, 2ull);
        EQUALS(decisions[0].admitted, true);
        EQUALS(decisions[0].reason, "");
        EQUALS(admission.gate.IsPending(2), false);

        // The identity, the ticket and the address.
        EQUALS(admission.Lua("seen"), "admit|one-time-ticket|10.0.0.2|76561198000000000");
    });

    IT("admits at once when no script of either language listens", {
        Admission admission;
        admission.server.files.Resource("idle", {{"main.lua", "Events.on('somethingElse', function() end)"}});
        EQUALS(admission.Start(), true);

        EQUALS(admission.Begin(1, "admit"), false);
        EQUALS(admission.gate.IsPending(1), false);
    });

    IT("hands a Lua handler the connection as a handle with the properties of JavaScript", {
        EQUALS(ClassMembers(PendingConnection::Definition().Describe()), "reject(),update(),isPending(),nickname,steamId,epicId,discordId,discordName,hardwareId,ticket,ip");

        Admission admission;
        admission.server.files.Resource("gate", {{"main.lua", kLuaGate}});
        EQUALS(admission.Start(), true);
        EQUALS(admission.Begin(11, "probe", "192.168.1.20"), true);

        EQUALS(admission.Lua("type(probe)"), "userdata");
        EQUALS(admission.Lua("table.concat({probe.nickname, probe.steamId, probe.epicId, probe.discordId, probe.discordName, probe.hardwareId, probe.ticket, probe.ip}, '|')"), "probe|76561198000000000|epic|discord|discord-name|hardware|one-time-ticket|192.168.1.20");
        EQUALS(admission.Lua("type(probe.reject) .. ' ' .. type(probe.update) .. ' ' .. type(probe.isPending)"), "function function function");
        // Read-only, as the properties of the JavaScript object are.
        EQUALS(LuaResourcesTest::Contains(admission.Lua("(function() probe.nickname = 'other' end)()"), "<error>"), true);
        EQUALS(admission.Lua("probe.nickname"), "probe");
        // JavaScript has no PendingConnection global, and neither has Lua.
        EQUALS(admission.Lua("type(PendingConnection)"), "nil");
    });

    IT("leaves the connection of a JavaScript handler the plain object it was", {
        Admission admission;
        admission.server.files.Resource("js-gate", {{"main.js", "Events.on('playerConnecting', (connection) => { globalThis.probe = connection; });"}});
        EQUALS(admission.Start(), true);
        EQUALS(admission.Begin(11, "probe"), true);

        EQUALS(admission.Js("Object.getPrototypeOf(probe) === Object.prototype"), "true");
        EQUALS(admission.Js("Object.prototype.toString.call(probe)"), "[object Object]");
        EQUALS(admission.Js("typeof PendingConnection"), "undefined");
        // Its own keys are the members the definition declares, the properties and then the functions: one added
        // there cannot be missing here.
        std::string declared;
        for (const Bind::PropertyDesc &property : PendingConnection::Definition().Describe().properties) {
            declared += (declared.empty() ? "" : ",") + property.name;
        }
        for (const Bind::FunctionDesc &function : PendingConnection::Definition().Describe().functions) {
            declared += "," + function.name;
        }
        EQUALS(admission.Js("Object.keys(probe).join()"), declared);
        EQUALS(declared, "nickname,steamId,epicId,discordId,discordName,hardwareId,ticket,ip,reject,update,isPending");
        EQUALS(admission.Js("Object.keys(probe).map((key) => typeof probe[key]).join()"), "string,string,string,string,string,string,string,string,function,function,function");
        EQUALS(admission.Js("Object.values(Object.getOwnPropertyDescriptors(probe)).every((d) => !d.writable && !d.configurable && d.enumerable)"), "true");
        EQUALS(admission.Js("JSON.stringify(probe)"), R"({"nickname":"probe","steamId":"76561198000000000","epicId":"epic","discordId":"discord","discordName":"discord-name","hardwareId":"hardware","ticket":"one-time-ticket","ip":"10.0.0.2"})");
        EQUALS(admission.Js("`${probe.reject.name}|${probe.reject.length}|${probe.isPending()}`"), "|0|true");

        // The handler's runtime is the resource's own, and the connection is one there: an assignment changes
        // nothing, silently in sloppy code and with a TypeError in strict code.
        EQUALS(admission.Js("probe.nickname = 'other'; probe.nickname"), "probe");
        EQUALS(admission.Js("(() => { 'use strict'; try { probe.nickname = 'other'; } catch (e) { return e.name + ' ' + probe.nickname; } return 'assigned'; })()"), "TypeError probe");
        EQUALS(admission.Js("delete probe.ticket; probe.reject = () => 'mine'; `${probe.ticket} ${typeof probe.reject()}`"), "one-time-ticket undefined");
        EQUALS(admission.gate.IsPending(11), false);
    });

    IT("lets a JavaScript handler call the functions of a connection without a receiver", {
        Admission admission;
        admission.server.files.Resource("js-gate", {{"main.js", "Events.on('playerConnecting', (connection) => { globalThis.probe = connection; });"}});
        EQUALS(admission.Start(), true);
        EQUALS(admission.Begin(11, "probe"), true);

        // Each carries the connection it answers.
        EQUALS(admission.Js("const { reject, isPending } = probe; reject('Detached'); isPending()"), "false");
        const auto decisions = admission.Settle();
        EQUALS(decisions.size(), 1u);
        EQUALS(decisions[0].reason, "Detached");
    });

    IT("refuses with the reason a Lua handler gives, the generic one without, and cuts an overlong one", {
        Admission admission;
        admission.server.files.Resource("gate", {{"main.lua", kLuaGate}});
        EQUALS(admission.Start(), true);

        EQUALS(admission.Decide(3, "refuse"), "refused: Not on the whitelist");
        EQUALS(admission.Decide(4, "refuse-default"), "refused: The server refused the connection.");
        EQUALS(admission.Decide(5, "refuse-long"), "refused: " + std::string(ConnectionGate::kMaxReasonLength, 'x'));
    });

    IT("fails closed when a Lua handler raises, at once or after a sleep", {
        Admission admission;
        admission.server.files.Resource("gate", {{"main.lua", kLuaGate}});
        EQUALS(admission.Start(), true);

        // The player is not shown the script's error.
        EQUALS(admission.Decide(6, "throw"), "refused: The server could not check your connection. Try again later.");
        EQUALS(admission.Decide(7, "async-throw"), "refused: The server could not check your connection. Try again later.");
        // The failure is the connection's, not the resource's: the gate goes on deciding.
        EQUALS(admission.server.module.GetResourceManager()->IsResourceRunning("gate"), true);
        EQUALS(admission.Decide(8, "admit"), "admitted");
    });

    IT("waits for a Lua handler that sleeps before it admits or refuses", {
        Admission admission;
        admission.server.files.Resource("gate", {{"main.lua", kLuaGate}});
        EQUALS(admission.Start(), true);

        const auto begun = std::chrono::steady_clock::now();
        EQUALS(admission.Begin(7, "async-admit"), true);
        admission.server.module.Update();
        std::vector<AdmissionDecision> early;
        admission.gate.Collect(early);
        EQUALS(early.empty(), true);
        EQUALS(admission.gate.IsPending(7), true);
        const auto decisions = admission.Settle();
        EQUALS(decisions.size(), 1u);
        EQUALS(decisions[0].admitted, true);
        EQUALS(admission.Lua("slept"), "true");
        EQUALS(std::chrono::steady_clock::now() - begun >= std::chrono::milliseconds(20), true);

        EQUALS(admission.Decide(8, "async-refuse"), "refused: Banned until Friday");
    });

    IT("refuses a connection whose Lua handler has not returned once the timeout passes", {
        Admission admission;
        admission.server.files.Resource("gate", {{"main.lua", kLuaGate}});
        EQUALS(admission.Start(), true);

        const auto begun = std::chrono::steady_clock::now();
        EQUALS(admission.Decide(9, "hang", 2000), "refused: The server did not answer in time. Try again later.");
        EQUALS(std::chrono::steady_clock::now() - begun >= std::chrono::milliseconds(200), true);

        // A player who leaves while waiting is forgotten without a decision.
        EQUALS(admission.Begin(10, "hang"), true);
        admission.gate.Drop(10);
        EQUALS(admission.gate.IsPending(10), false);
        EQUALS(admission.Settle(400).empty(), true);
    });

    IT("restarts the timeout each time a Lua handler updates the player", {
        Framework::Networking::NetworkServer network;
        Admission admission;
        admission.server.files.Resource("gate", {{"main.lua", kLuaGate}});
        EQUALS(admission.Start(200, &network), true);

        // 240 ms of handler against a timeout of 200: only the update in the middle keeps it alive.
        EQUALS(admission.Decide(12, "status"), "admitted");
        EQUALS(admission.Lua("updated"), "true");
    });

    IT("reports pending while undecided and not after, and a decided connection's calls do nothing", {
        Admission admission;
        admission.server.files.Resource("gate", {{"main.lua", kLuaGate}});
        EQUALS(admission.Start(), true);

        EQUALS(admission.Begin(11, "probe"), true);
        EQUALS(admission.Lua("pendingDuring"), "true");
        EQUALS(admission.Settle().size(), 1u);
        EQUALS(admission.Lua("probe:isPending()"), "false");
        EQUALS(admission.Lua("select('#', probe:reject('late'))"), "0");
        EQUALS(admission.Lua("select('#', probe:update('late'))"), "0");
        EQUALS(admission.Lua("probe.nickname"), "probe");

        // A second refusal does not replace the first, as in JavaScript.
        admission.server.files.Resource("twice", {{"main.lua", "Events.on('playerConnecting', function(c) c:reject('first') c:reject('second') after = c:isPending() end)"}});
        admission.server.module.GetResourceManager()->DiscoverResources();
        EQUALS(static_cast<bool>(admission.server.module.GetResourceManager()->StartResource("twice")), true);
        EQUALS(admission.Decide(13, "admit"), "refused: first");
        EQUALS(admission.Lua("after", "twice"), "false");
    });

    IT("refuses a wrong argument in the words of JavaScript", {
        Admission admission;
        admission.server.files.Resource("gate", {{"main.lua", kLuaGate}});
        admission.server.files.Resource("js-gate", {{"main.js", "Events.on('playerConnecting', (connection) => { globalThis.probe = connection; });"}});
        EQUALS(admission.Start(), true);
        EQUALS(admission.Begin(11, "probe"), true);

        const auto lua = [&](const std::string &call) {
            const std::string error = admission.Lua("select(2, pcall(function() " + call + " end))");
            const size_t at         = error.find(": ");
            return at == std::string::npos ? error : error.substr(at + 2);
        };
        const auto js = [&](const std::string &call) {
            return admission.Js("(() => { try { " + call + "; } catch (e) { return e.name + ': ' + e.message; } return 'no error'; })()");
        };

        EQUALS(lua("probe:reject(5)"), "PendingConnection.reject: expected a string reason");
        EQUALS(js("probe.reject(5)"), "TypeError: PendingConnection.reject: expected a string reason");
        EQUALS(js("probe.reject(null)"), "TypeError: PendingConnection.reject: expected a string reason");
        EQUALS(lua("probe:update()"), "PendingConnection.update: expected a string message");
        EQUALS(js("probe.update()"), "TypeError: PendingConnection.update: expected a string message");
        EQUALS(lua("probe:update(5)"), "PendingConnection.update: expected a string message");
        EQUALS(admission.gate.IsPending(11), true);

        // An explicit nil is no reason, as undefined is none.
        EQUALS(lua("probe:reject(nil)"), "nil");
        EQUALS(admission.gate.IsPending(11), false);

        // Without a gate the calls say so: a state error, not a stale handle.
        Framework::CoreModules::SetConnectionGate(nullptr);
        EQUALS(lua("probe:isPending()"), "PendingConnection.isPending: the connection gate is not running");
        EQUALS(js("probe.isPending()"), "Error: PendingConnection.isPending: the connection gate is not running");
        EQUALS(lua("probe:reject('x')"), "PendingConnection.reject: the connection gate is not running");
        EQUALS(lua("probe:update('x')"), "PendingConnection.update: the connection gate is not running");
        Framework::CoreModules::SetConnectionGate(&admission.gate);
    });

    IT("asks a JavaScript gate and a Lua gate of one server, and lets either refuse", {
        Admission admission;
        admission.server.files.Resource("gate", {{"main.lua", kLuaGate}});
        admission.server.files.Resource("js-gate", {{"main.js", kJsGate}});
        EQUALS(admission.Start(), true);
        EQUALS(admission.server.module.GetResourceManager()->GetEvents().GetListenerCount(ConnectionGate::kEventName), 2u);

        // Both are asked, and both let it in.
        EQUALS(admission.Decide(20, "admit"), "admitted");
        EQUALS(admission.Lua("seen"), "admit|one-time-ticket|10.0.0.2|76561198000000000");
        EQUALS(admission.Js("seen"), "admit|one-time-ticket|10.0.0.2|76561198000000000");

        // One refusal is enough, whichever language it comes from.
        EQUALS(admission.Decide(21, "refuse"), "refused: Not on the whitelist");
        EQUALS(admission.Js("seen.split('|')[0]"), "refuse");
        EQUALS(admission.Decide(22, "js-refuse"), "refused: JavaScript says no");
        EQUALS(admission.Lua("seen"), "js-refuse|one-time-ticket|10.0.0.2|76561198000000000");

        // The connection waits for the slower of the two.
        EQUALS(admission.Decide(23, "async-refuse"), "refused: Banned until Friday");
        EQUALS(admission.Decide(24, "js-slow"), "admitted");
        EQUALS(admission.Js("slept"), "true");
        EQUALS(admission.Decide(25, "throw"), "refused: The server could not check your connection. Try again later.");
    });

    IT("carries the connection between the languages as itself", {
        Admission admission;
        // Each language hands the connection it was given on to the other, which answers it.
        admission.server.files.Resource("gate", {{"main.lua", R"(
            Events.on("playerConnecting", function(connection)
                if connection.nickname == "to-js" then
                    Events.emit("fromLua", connection)
                end
            end)
            Events.on("fromJs", function(connection)
                got = type(connection) .. ":" .. connection.nickname .. ":" .. connection.ip .. ":" .. tostring(connection:isPending())
                connection:reject("Lua was handed it")
            end)
        )"}});
        admission.server.files.Resource("js-gate", {{"main.js", R"(
            Events.on("playerConnecting", async (connection) => {
                if (connection.nickname === "to-lua") {
                    await Events.emit("fromJs", connection);
                }
            });
            Events.on("fromLua", (connection) => {
                const nickname = Object.getOwnPropertyDescriptor(connection, "nickname");
                globalThis.got = `${Object.keys(connection).join()}:${connection.nickname}:${connection.ip}:${connection.isPending()}:${nickname.writable}`;
                connection.reject("JavaScript was handed it");
            });
        )"}});
        EQUALS(admission.Start(), true);

        EQUALS(admission.Decide(30, "to-lua"), "refused: Lua was handed it");
        EQUALS(admission.Lua("got"), "userdata:to-lua:10.0.0.2:true");
        EQUALS(admission.Decide(31, "to-js"), "refused: JavaScript was handed it");
        EQUALS(admission.Js("got"), "nickname,steamId,epicId,discordId,discordName,hardwareId,ticket,ip,reject,update,isPending:to-js:10.0.0.2:true:false");
    });

    IT("raises for a method of a connection called with a dot in Lua, and leaves the connection waiting", {
        Admission admission;
        admission.server.files.Resource("gate", {{"main.lua", "Events.on('playerConnecting', function(connection) probe = connection sleep(60000) end)"}});
        EQUALS(admission.Start(2000), true);
        EQUALS(admission.Begin(11, "probe"), true);

        EQUALS(admission.Lua("select(2, pcall(function() probe.reject('banned') end))"), "eval:1: bad receiver for 'PendingConnection:reject' (PendingConnection expected, got string)");
        EQUALS(admission.Lua("select(2, pcall(function() probe.update('Checking') end))"), "eval:1: bad receiver for 'PendingConnection:update' (PendingConnection expected, got string)");
        EQUALS(admission.Lua("select(2, pcall(function() return probe.isPending() end))"), "eval:1: bad receiver for 'PendingConnection:isPending' (PendingConnection expected, got no value)");
        EQUALS(admission.gate.IsPending(11), true);

        // With the receiver passed by hand it is the method.
        EQUALS(admission.Lua("probe.reject(probe, 'banned')"), "nil");
        const auto decisions = admission.Settle();
        EQUALS(decisions.size(), 1u);
        EQUALS(decisions[0].reason, "banned");
    });

    IT("carries a connection to another runtime while it waits and once it is decided", {
        Admission admission;
        // Three runtimes: a JavaScript gate and a Lua gate that keep the connection, and a JavaScript resource
        // that only logs. `hand` sends the kept connection to the two other runtimes.
        admission.server.files.Resource("js-gate", {{"main.js", R"(
            Events.on("playerConnecting", (connection) => { globalThis.kept = connection; });
            globalThis.hand = () => { Events.emit("toLog", "js", kept); Events.emit("toLua", kept); return "handed"; };
        )"}});
        admission.server.files.Resource("gate", {{"main.lua", R"(
            Events.on("playerConnecting", function(connection) kept = connection end)
            Events.on("toLua", function(connection)
                local pending = connection:isPending()
                local quiet = select("#", connection:update("Handed over"))
                if refuse or not pending then
                    quiet = quiet + select("#", connection:reject("Handed over"))
                end
                got = type(connection) .. ":" .. connection.nickname .. ":" .. connection.steamId .. ":" .. connection.ticket .. ":" .. connection.ip .. ":" .. tostring(pending) .. ":" .. quiet
            end)
            function hand() setTimeout(function() Events.emit("toLog", "lua", kept) end, 0) return "handed" end
        )"}});
        admission.server.files.Resource("log", {{"main.js", R"(
            globalThis.got = {};
            Events.on("toLog", (from, connection) => {
                const pending = connection.isPending();
                const quiet = [connection.update("Handed over"), pending ? undefined : connection.reject("Logged")].every((result) => result === undefined);
                got[from] = `${Object.keys(connection).length}:${connection.nickname}:${connection.steamId}:${connection.ticket}:${connection.ip}:${pending}:${quiet}`;
            });
        )"}});
        EQUALS(admission.Start(2000), true);

        const auto handAround = [&] {
            admission.Js("globalThis.got = {}; 0", "log");
            admission.Lua("(function() got = nil end)()");
            const std::string handed = admission.Js("hand()") + " " + admission.Lua("hand()");
            const auto deadline      = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while ((admission.Js("String(got.js) + String(got.lua)", "log").contains("undefined") || admission.Lua("got") == "nil") && std::chrono::steady_clock::now() < deadline) {
                admission.server.module.Update();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return handed;
        };

        // Waiting: it arrives pending, JavaScript to JavaScript, JavaScript to Lua and Lua to JavaScript.
        EQUALS(admission.Begin(50, "queued"), true);
        EQUALS(handAround(), "handed handed");
        EQUALS(admission.Js("got.js", "log"), "11:queued:76561198000000000:one-time-ticket:10.0.0.2:true:true");
        EQUALS(admission.Js("got.lua", "log"), "11:queued:76561198000000000:one-time-ticket:10.0.0.2:true:true");
        EQUALS(admission.Lua("got"), "userdata:queued:76561198000000000:one-time-ticket:10.0.0.2:true:0");
        EQUALS(admission.gate.IsPending(50), true);

        // The runtime it was handed to answers it: here the Lua one refuses.
        admission.Lua("(function() refuse = true end)()");
        EQUALS(handAround(), "handed handed");
        const auto decisions = admission.Settle();
        EQUALS(decisions.size(), 1u);
        EQUALS(decisions[0].admitted, false);
        EQUALS(decisions[0].reason, "Handed over");
        EQUALS(admission.gate.IsPending(50), false);

        // Decided: it still crosses, JavaScript to JavaScript, JavaScript to Lua and Lua to JavaScript, as a
        // handle that reads as it did, is no longer pending, and whose calls do nothing.
        EQUALS(handAround(), "handed handed");
        EQUALS(admission.Js("got.js", "log"), "11:queued:76561198000000000:one-time-ticket:10.0.0.2:false:true");
        EQUALS(admission.Js("got.lua", "log"), "11:queued:76561198000000000:one-time-ticket:10.0.0.2:false:true");
        EQUALS(admission.Lua("got"), "userdata:queued:76561198000000000:one-time-ticket:10.0.0.2:false:0");
        EQUALS(admission.Settle(50).empty(), true);
    });

    // ==================== Voice ====================

    IT("gives a Lua state the Voice of JavaScript, under the same names", {
        EQUALS(Members(Voice::Definition()),
            "setRange(),getRange(),setPlayerRange(),getPlayerRange(),setTierRange(),getTierRange(),setPlayerTier(),getPlayerTier(),setPlayerMuted(),isPlayerMuted(),setPlayerDeaf(),isPlayerDeaf(),setLocalMute(),isLocallyMuted(),isPlayerVoiceEnabled(),"
            "isPlayerTalking()");

        Booth booth;
        EQUALS(booth.stage.lua.ready, true);
        std::string missing;
        for (const Bind::FunctionDesc &function : Voice::Definition().Describe().functions) {
            if (booth.Lua("type(Voice." + function.name + ")") != "function" || booth.Js("typeof Voice." + function.name) != "function") {
                missing += function.name + " ";
            }
        }
        EQUALS(missing, "");
        EQUALS(booth.Js("Object.keys(Voice).length"), "16");

        // A Lua script reads Voice and cannot replace a function of it; to JavaScript it is the plain object it was.
        EQUALS(booth.Lua("(function() Voice.setRange = print end)()"), "<error> eval:1: attempt to set 'setRange' on Voice");
        EQUALS(booth.Js("Voice.custom = () => 'mine'; Voice.custom()"), "mine");
        // Its functions are ordinary properties of it, nameless and declaring no parameter, as they were.
        EQUALS(booth.Js("Object.values(Object.getOwnPropertyDescriptors(Voice)).every((d) => d.writable && d.configurable && d.enumerable)"), "true");
        EQUALS(booth.Js("Object.keys(Voice).filter((key) => Voice[key].name !== '' || Voice[key].length !== 0).join()"), "");
        EQUALS(booth.Js("{ const original = Voice.getRange; Voice.getRange = () => -1; const replaced = Voice.getRange(); Voice.getRange = original; delete Voice.custom; `${replaced} ${typeof Voice.custom}` }"), "-1 undefined");
    });

    IT("sets how far voice carries from Lua as from JavaScript", {
        Booth booth;
        const auto &router = booth.voice.GetRouter();

        EQUALS(booth.Lua("Voice.getRange()"), booth.Js("Voice.getRange()") + ".0");
        EQUALS(booth.Lua("select('#', Voice.setRange(40))"), "0");
        EQUALS(booth.voice.GetProximityRange(), 40.0f);
        EQUALS(booth.Lua("Voice.getRange()"), "40.0");
        EQUALS(booth.Js("Voice.getRange()"), "40");
        // What JavaScript sets, Lua reads.
        EQUALS(booth.Js("Voice.setRange(12.5)"), "undefined");
        EQUALS(booth.Lua("Voice.getRange()"), "12.5");

        booth.Lua("Voice.setTierRange(0, 2)");
        EQUALS(booth.voice.GetTierRange(VoiceTier::Whisper), 2.0f);
        EQUALS(booth.Lua("Voice.getTierRange(0)"), "2.0");
        EQUALS(booth.Js("Voice.getTierRange(0)"), "2");
        booth.Js("Voice.setTierRange(2, 60)");
        EQUALS(booth.Lua("Voice.getTierRange(2)"), "60.0");
        // Normal carries the server-wide range.
        EQUALS(booth.Lua("Voice.getTierRange(1)"), "12.5");

        booth.Lua("Voice.setPlayerRange(p, 7)");
        EQUALS(router.GetEffectivePlayerRange(booth.p), 7.0f);
        EQUALS(booth.Lua("Voice.getPlayerRange(p)"), "7.0");
        EQUALS(booth.Js("Voice.getPlayerRange(p)"), "7");
        EQUALS(booth.Lua("Voice.getPlayerRange(q)"), "12.5");
        booth.Js("Voice.setPlayerRange(q, 9)");
        EQUALS(booth.Lua("Voice.getPlayerRange(q)"), "9.0");

        EQUALS(booth.Lua("Voice.getPlayerTier(p)"), "1");
        booth.Lua("Voice.setPlayerTier(p, 2)");
        EQUALS(router.GetPlayerTier(booth.p) == VoiceTier::Shout, true);
        EQUALS(booth.Lua("math.type(Voice.getPlayerTier(p))"), "integer");
        EQUALS(booth.Js("Voice.getPlayerTier(p)"), "2");
        booth.Js("Voice.setPlayerTier(q, 0)");
        EQUALS(booth.Lua("Voice.getPlayerTier(q)"), "0");
    });

    IT("mutes and deafens from Lua as from JavaScript", {
        Booth booth;
        const auto &router = booth.voice.GetRouter();

        EQUALS(booth.Lua("tostring(Voice.isPlayerMuted(p)) .. tostring(Voice.isPlayerDeaf(p)) .. tostring(Voice.isLocallyMuted(p, q))"), "falsefalsefalse");
        booth.Lua("Voice.setPlayerMuted(p, true)");
        EQUALS(router.IsPlayerMuted(booth.p), true);
        EQUALS(router.IsPlayerMuted(booth.q), false);
        EQUALS(booth.Lua("Voice.isPlayerMuted(p)"), "true");
        EQUALS(booth.Js("Voice.isPlayerMuted(p)"), "true");
        booth.Js("Voice.setPlayerMuted(p, false)");
        EQUALS(booth.Lua("Voice.isPlayerMuted(p)"), "false");

        booth.Lua("Voice.setPlayerDeaf(q, true)");
        EQUALS(router.IsPlayerDeaf(booth.q), true);
        EQUALS(booth.Js("Voice.isPlayerDeaf(q)"), "true");
        booth.Lua("Voice.setPlayerDeaf(q, false)");
        EQUALS(booth.Js("Voice.isPlayerDeaf(q)"), "false");

        booth.Lua("Voice.setLocalMute(p, q, true)");
        EQUALS(router.IsLocallyMuted(booth.p, booth.q), true);
        EQUALS(booth.Lua("tostring(Voice.isLocallyMuted(p, q)) .. tostring(Voice.isLocallyMuted(q, p))"), "truefalse");
        EQUALS(booth.Js("`${Voice.isLocallyMuted(p, q)}${Voice.isLocallyMuted(q, p)}`"), "truefalse");
        booth.Js("Voice.setLocalMute(p, q, false)");
        EQUALS(booth.Lua("Voice.isLocallyMuted(p, q)"), "false");

        // A flag left out is false, and any other value is what the language makes of it as a condition.
        booth.Lua("Voice.setPlayerMuted(p, true) Voice.setPlayerMuted(p)");
        EQUALS(router.IsPlayerMuted(booth.p), false);
        booth.Js("Voice.setPlayerMuted(p, 1)");
        EQUALS(router.IsPlayerMuted(booth.p), true);
        booth.Js("Voice.setPlayerMuted(p, 0)");
        EQUALS(router.IsPlayerMuted(booth.p), false);
        booth.Js("Voice.setPlayerMuted(p, 'yes')");
        EQUALS(router.IsPlayerMuted(booth.p), true);
        booth.Js("Voice.setPlayerMuted(p, '')");
        EQUALS(router.IsPlayerMuted(booth.p), false);
        booth.Js("Voice.setPlayerMuted(p, {})");
        EQUALS(router.IsPlayerMuted(booth.p), true);
        booth.Js("Voice.setPlayerMuted(p, NaN)");
        EQUALS(router.IsPlayerMuted(booth.p), false);
        booth.Lua("Voice.setPlayerMuted(p, 0)");
        EQUALS(router.IsPlayerMuted(booth.p), true);
        booth.Lua("Voice.setPlayerMuted(p, false)");
        EQUALS(router.IsPlayerMuted(booth.p), false);

        // The zero of a BigInt is false to JavaScript, and any other is true.
        booth.Js("Voice.setPlayerMuted(p, 1n)");
        EQUALS(router.IsPlayerMuted(booth.p), true);
        booth.Js("Voice.setPlayerMuted(p, 0n)");
        EQUALS(router.IsPlayerMuted(booth.p), false);
        booth.Js("Voice.setPlayerMuted(p, 1)");
        booth.Js("Voice.setPlayerMuted(p, null)");
        EQUALS(router.IsPlayerMuted(booth.p), false);

        // What a player chose and does, which only their client changes.
        EQUALS(booth.Lua("tostring(Voice.isPlayerVoiceEnabled(p)) .. tostring(Voice.isPlayerTalking(p))"), "truefalse");
        EQUALS(booth.Js("`${Voice.isPlayerVoiceEnabled(p)}${Voice.isPlayerTalking(p)}`"), "truefalse");
        booth.voice.GetRouter().SetPlayerVoiceDisabled(booth.p, true);
        EQUALS(booth.Lua("Voice.isPlayerVoiceEnabled(p)"), "false");
        EQUALS(booth.Js("Voice.isPlayerVoiceEnabled(p)"), "false");
    });

    IT("refuses a wrong argument of Voice in the words of JavaScript", {
        Booth booth;
        struct Case final {
            const char *call;
            const char *error; // as JavaScript names it; Lua raises what follows the name
        };
        const Case cases[] = {
            {"Voice.setRange()", "TypeError: Voice.setRange: expected a numeric range"},
            {"Voice.setRange('far')", "TypeError: Voice.setRange: expected a numeric range"},
            {"Voice.setPlayerRange(5, 5)", "TypeError: Voice.setPlayerRange: expected a Player"},
            {"Voice.setPlayerRange(e, 5)", "Error: Voice.setPlayerRange: player has no owning connection"},
            {"Voice.setPlayerRange(p)", "TypeError: Voice.setPlayerRange: expected a numeric range"},
            {"Voice.getPlayerRange()", "TypeError: Voice.getPlayerRange: expected a Player"},
            {"Voice.getPlayerRange(e)", "Error: Voice.getPlayerRange: player has no owning connection"},
            {"Voice.setTierRange(3, 5)", "RangeError: Voice.setTierRange: expected a tier, 0 (whisper), 1 (normal) or 2 (shout)"},
            {"Voice.setTierRange(0.5, 5)", "RangeError: Voice.setTierRange: expected a tier, 0 (whisper), 1 (normal) or 2 (shout)"},
            {"Voice.setTierRange('1', 5)", "RangeError: Voice.setTierRange: expected a tier, 0 (whisper), 1 (normal) or 2 (shout)"},
            {"Voice.setTierRange(1)", "TypeError: Voice.setTierRange: expected a numeric range"},
            {"Voice.getTierRange()", "RangeError: Voice.getTierRange: expected a tier, 0 (whisper), 1 (normal) or 2 (shout)"},
            {"Voice.setPlayerTier({}, 1)", "TypeError: Voice.setPlayerTier: expected a Player"},
            {"Voice.setPlayerTier(p, -1)", "RangeError: Voice.setPlayerTier: expected a tier, 0 (whisper), 1 (normal) or 2 (shout)"},
            {"Voice.getPlayerTier('p')", "TypeError: Voice.getPlayerTier: expected a Player"},
            {"Voice.setPlayerMuted(true)", "TypeError: Voice.setPlayerMuted: expected a Player"},
            {"Voice.setPlayerMuted(e, true)", "Error: Voice.setPlayerMuted: player has no owning connection"},
            {"Voice.isPlayerMuted()", "TypeError: Voice.isPlayerMuted: expected a Player"},
            {"Voice.setPlayerDeaf(1, true)", "TypeError: Voice.setPlayerDeaf: expected a Player"},
            {"Voice.isPlayerDeaf(e)", "Error: Voice.isPlayerDeaf: player has no owning connection"},
            {"Voice.setLocalMute(p)", "TypeError: Voice.setLocalMute: expected a Player"},
            {"Voice.setLocalMute(e, p, true)", "Error: Voice.setLocalMute: player has no owning connection"},
            {"Voice.setLocalMute(p, e, true)", "Error: Voice.setLocalMute: player has no owning connection"},
            {"Voice.isLocallyMuted(p)", "TypeError: Voice.isLocallyMuted: expected a Player"},
            {"Voice.isPlayerVoiceEnabled()", "TypeError: Voice.isPlayerVoiceEnabled: expected a Player"},
            {"Voice.isPlayerTalking(e)", "Error: Voice.isPlayerTalking: player has no owning connection"},
        };
        std::string wrong;
        for (const Case &entry : cases) {
            const std::string error = entry.error;
            const std::string js    = booth.stage.js.Thrown(entry.call);
            const std::string lua   = booth.stage.lua.Raised(entry.call);
            if (js != error || lua != error.substr(error.find(": ") + 2)) {
                wrong += std::string(entry.call) + " -> " + js + " / " + lua + "\n";
            }
        }
        EQUALS(wrong, "");
        // Nothing was changed by a call that was refused.
        EQUALS(booth.voice.GetProximityRange(), Framework::Voice::kDefaultProximityRange);
        EQUALS(booth.voice.GetRouter().IsPlayerMuted(booth.p), false);
    });

    IT("answers with the defaults, and sets nothing, on a server without voice", {
        Booth booth(false);
        const std::string range = std::to_string(static_cast<int>(Framework::Voice::kDefaultProximityRange));

        EQUALS(booth.Lua("math.tointeger(Voice.getRange())"), range);
        EQUALS(booth.Js("Voice.getRange()"), range);
        EQUALS(booth.Lua("math.tointeger(Voice.getPlayerRange(p))"), range);
        EQUALS(booth.Lua("math.tointeger(Voice.getTierRange(0))"), range);
        EQUALS(booth.Lua("Voice.getPlayerTier(p)"), "1");
        EQUALS(booth.Js("Voice.getPlayerTier(p)"), "1");
        EQUALS(booth.stage.lua.Raised("Voice.setRange(40) Voice.setPlayerRange(p, 4) Voice.setTierRange(0, 4) Voice.setPlayerTier(p, 2) Voice.setPlayerMuted(p, true) Voice.setPlayerDeaf(p, true) Voice.setLocalMute(p, q, true)"), "no error");
        EQUALS(booth.Lua("tostring(Voice.isPlayerMuted(p)) .. tostring(Voice.isPlayerDeaf(p)) .. tostring(Voice.isLocallyMuted(p, q)) .. tostring(Voice.isPlayerVoiceEnabled(p)) .. tostring(Voice.isPlayerTalking(p))"), "falsefalsefalsefalsefalse");
        EQUALS(booth.Js("`${Voice.isPlayerMuted(p)}${Voice.isPlayerDeaf(p)}${Voice.isLocallyMuted(p, q)}${Voice.isPlayerVoiceEnabled(p)}${Voice.isPlayerTalking(p)}`"), "falsefalsefalsefalsefalse");
        // The arguments are checked all the same.
        EQUALS(booth.stage.lua.Raised("Voice.setRange('far')"), "Voice.setRange: expected a numeric range");
    });

    IT("gives the Lua resources of a server Voice, and no PendingConnection global", {
        LuaResourcesTest::Server server;
        server.files.Resource("talk", {{"main.lua", "kinds = type(Voice) .. ' ' .. type(Voice.setRange) .. ' ' .. type(PendingConnection) range = Voice.getRange()"}});
        EQUALS(server.Start(), true);
        EQUALS(server.Eval("talk", "kinds"), "table function nil");
        EQUALS(server.Eval("talk", "math.tointeger(range)"), std::to_string(static_cast<int>(Framework::Voice::kDefaultProximityRange)));
    });
});
