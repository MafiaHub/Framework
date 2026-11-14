/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_events_ut.h"
#include "lua_transfer_ut.h"

#include "scripting/builtins/messages.h"
#include "scripting/lua/lua_transfer.h"

#include <string>
#include <vector>

namespace LuaMessagesTest {

    // A Lua resource that asks: try(target, type, payload, slot) leaves "<ok>|<answer or error>" in outcome[slot].
    inline const char *kLuaAsker = R"(
        outcome = {}
        function try(target, messageType, payload, slot)
            local ok, answer = pcall(Messages.request, target, messageType, payload)
            outcome[slot or 1] = tostring(ok) .. '|' .. tostring(answer)
        end
    )";

    // A JavaScript resource that asks: tryIt(target, type, payload, slot) leaves in outcome[slot] "resolved|<answer>",
    // or "rejected|<what the rejection is>|<its text>".
    inline const char *kJsAsker = R"(
        globalThis.outcome = {};
        globalThis.tryIt = (target, messageType, payload, slot = 1) => {
            outcome[slot] = 'pending';
            Messages.request(target, messageType, payload).then(
                (answer) => { outcome[slot] = 'resolved|' + answer; },
                (e) => { outcome[slot] = 'rejected|' + (e instanceof Error ? e.constructor.name : typeof e) + '|' + (e instanceof Error ? e.message : e); });
            return 'asked';
        };
    )";

    // What the manager was told its resources failed with: "<resource>: <first line>".
    inline void CollectErrors(LuaEventsTest::Bus &bus, std::vector<std::string> &errors) {
        bus.Manager().AddOnResourceError([&errors](const std::string &name, const std::string &error) {
            errors.push_back(name + ": " + LuaRuntimeTest::FirstLine(error));
        });
    }

    // The rig of the Events tests, whose scripts of both languages have stop(name) and log(text), with Messages in
    // its Lua runtimes. Its JavaScript runtimes have it already.
    struct StopRig final {
        LuaEventsTest::StopRig stops;

        StopRig() {
            stops.rig.lua.SetRuntimeSetupCallback([&rig = stops.rig](Framework::Scripting::LuaRuntime &runtime) {
                lua_State *L = runtime.State();
                lua_pushcfunction(L, LuaResourcesTest::LuaLog);
                lua_setglobal(L, "log");
                lua_pushlightuserdata(L, &rig);
                lua_pushcclosure(L, LuaResourcesTest::Rig::Stop, 1);
                lua_setglobal(L, "stop");
                Framework::Scripting::Bind::SetCallResourceManager(&runtime, &rig.manager);
                {
                    Framework::Scripting::Bind::Registrar registrar;
                    Framework::Scripting::Builtins::Messages::Register(registrar);
                    registrar.Lua().Apply(runtime);
                }
            });
        }

        // A few frames of a server: both engines tick, and the replies are delivered.
        void Pump() {
            for (int i = 0; i < 4; ++i) {
                stops.rig.node.Tick();
                stops.rig.Advance(1);
                Framework::Scripting::Builtins::Messages::ProcessPendingResponses();
            }
        }

        // Runs a chunk in a coroutine of the resource. Empty, or the first line of what it raised.
        std::string Run(const std::string &resource, const std::string &source) {
            Framework::Scripting::LuaRuntime *runtime = stops.rig.lua.GetRuntime(resource);
            return runtime != nullptr ? LuaRuntimeTest::FirstLine(LuaRuntimeTest::Run(*runtime, source)) : "<no runtime>";
        }
    };

    inline bool AnyContains(const std::vector<std::string> &lines, const std::string &part) {
        for (const std::string &line : lines) {
            if (LuaRuntimeTest::Contains(line, part)) {
                return true;
            }
        }
        return false;
    }
} // namespace LuaMessagesTest

MODULE(lua_messages, {
    // ==================== Request and reply ====================

    IT("answers a Lua request from a Lua handler, with copies, once the reply is processed", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", R"(
            Messages.handle('next', function(payload, reply)
                payload.seen = true
                kind = type(reply)
                reply({n = payload.n + 1})
            end)
        )"}});
        bus.server.files.Resource("asker", {{"main.lua", R"(
            function ask()
                local p = {n = 1}
                local answer = Messages.request('provider', 'next', p)
                first = answer.n .. '|' .. tostring(p.seen)
                -- A request made from where the reply of another arrived.
                chained = Messages.request('provider', 'next', answer).n
            end
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "asker"}), true);

        // The handler answered at once, and the requester still waits: a reply is delivered by the server's update,
        // as it is to a promise.
        EQUALS(bus.Run("asker", "ask()"), "");
        EQUALS(bus.Lua("provider", "kind"), "function");
        EQUALS(bus.Lua("asker", "first"), "nil");
        EQUALS(bus.Until([&] {
            return bus.Lua("asker", "chained") != "nil";
        }),
            true);
        EQUALS(bus.Lua("asker", "first"), "2|nil");
        EQUALS(bus.Lua("asker", "chained"), "3");
    });

    IT("answers across the languages, in both directions", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("lua-provider", {{"main.lua", "Messages.handle('next', function(payload, reply) payload.seen = true reply({n = payload.n + 1, list = {1, 2}}) end)"}});
        bus.server.files.Resource("js-provider", {{"main.js", "Messages.handle('next', (payload, reply) => { payload.seen = true; reply({n: payload.n + 1, list: [1, 2]}); });"}});
        bus.server.files.Resource("lua-asker", {{"main.lua", R"(
            function ask(target)
                local p = {n = 1}
                local answer = Messages.request(target, 'next', p)
                result = answer.n .. '|' .. #answer.list .. '|' .. tostring(p.seen)
            end
        )"}});
        bus.server.files.Resource("js-asker", {{"main.js", R"(
            globalThis.answer = 'pending';
            globalThis.ask = (target) => {
                const p = {n: 1};
                answer = 'pending';
                Messages.request(target, 'next', p).then((a) => { answer = a.n + '|' + a.list.length + '|' + ('seen' in p); }, (e) => { answer = 'rejected ' + e; });
                return 'asked';
            };
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-provider", "js-provider", "lua-asker", "js-asker"}), true);

        // Lua asks JavaScript.
        EQUALS(bus.Run("lua-asker", "ask('js-provider')"), "");
        EQUALS(bus.Until([&] {
            return bus.Lua("lua-asker", "result") != "nil";
        }),
            true);
        EQUALS(bus.Lua("lua-asker", "result"), "2|2|nil");

        // JavaScript asks Lua.
        EQUALS(bus.Js("js-asker", "ask('lua-provider')"), "asked");
        EQUALS(bus.Js("js-asker", "answer"), "pending");
        EQUALS(bus.Until([&] {
            return bus.Js("js-asker", "answer") != "pending";
        }),
            true);
        EQUALS(bus.Js("js-asker", "answer"), "2|2|false");

        // JavaScript asks JavaScript of another runtime, as it always could.
        EQUALS(bus.Js("js-asker", "ask('js-provider')"), "asked");
        EQUALS(bus.Until([&] {
            return bus.Js("js-asker", "answer") != "pending";
        }),
            true);
        EQUALS(bus.Js("js-asker", "answer"), "2|2|false");
    });

    IT("hands a JavaScript handler of the requester's own isolate the payload itself", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("own", {{"main.js", R"(
            Messages.handle('same', (payload, reply) => { payload.touched = true; reply(payload); });
            globalThis.same = 'pending';
            globalThis.probe = () => {
                const p = {};
                Messages.request('own', 'same', p).then((a) => { same = (a === p) + '|' + p.touched; });
                return 'asked';
            };
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("own"), "");

        EQUALS(bus.Js("own", "probe()"), "asked");
        EQUALS(bus.Until([&] {
            return bus.Js("own", "same") != "pending";
        }),
            true);
        EQUALS(bus.Js("own", "same"), "true|true");
    });

    IT("keeps one handler for a type of a resource, whichever language registered it last", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("mixed", {{"a.js", "Messages.handle('who', (payload, reply) => reply('js')); Messages.handle('js-only', (payload, reply) => reply('js:' + payload));"},
                                               {"b.lua", "Messages.handle('who', function(payload, reply) reply('lua') end) function ask(messageType) answer = Messages.request('mixed', messageType, 'x') end"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("mixed"), "");

        EQUALS(bus.Run("mixed", "ask('who')"), "");
        EQUALS(bus.Until([&] {
            return bus.Lua("mixed", "answer") != "nil";
        }),
            true);
        EQUALS(bus.Lua("mixed", "answer"), "lua");

        // The Lua of a resource reaches the JavaScript handler of the same resource.
        EQUALS(bus.Run("mixed", "answer = nil ask('js-only')"), "");
        EQUALS(bus.Until([&] {
            return bus.Lua("mixed", "answer") != "nil";
        }),
            true);
        EQUALS(bus.Lua("mixed", "answer"), "js:x");
    });

    IT("waits for a handler that answers later", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("lua-provider", {{"main.lua", "Messages.handle('slow', function(payload, reply) sleep(30) answered = true reply(payload * 2) end)"}});
        bus.server.files.Resource("js-provider", {{"main.js", R"(
            globalThis.answered = false;
            Messages.handle('slow', (payload, reply) => { setTimeout(() => { answered = true; reply(payload * 2); }, 30); });
            Messages.handle('async', async (payload, reply) => { await new Promise((resolve) => setTimeout(resolve, 20)); reply(payload * 3); });
        )"}});
        bus.server.files.Resource("lua-asker", {{"main.lua", LuaMessagesTest::kLuaAsker}});
        bus.server.files.Resource("js-asker", {{"main.js", LuaMessagesTest::kJsAsker}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-provider", "js-provider", "lua-asker", "js-asker"}), true);

        EQUALS(bus.Run("lua-asker", "try('lua-provider', 'slow', 21, 1)"), "");
        EQUALS(bus.Run("lua-asker", "try('js-provider', 'slow', 4, 2)"), "");
        EQUALS(bus.Run("lua-asker", "try('js-provider', 'async', 5, 3)"), "");
        EQUALS(bus.Js("js-asker", "tryIt('lua-provider', 'slow', 50, 1)"), "asked");

        bool early = false;
        EQUALS(bus.Until([&] {
            const bool luaAnswered = bus.Lua("lua-provider", "answered") == "true";
            const bool jsAnswered  = bus.Js("js-provider", "answered") == "true";
            early                  = early || (!luaAnswered && (bus.Lua("lua-asker", "outcome[1]") != "nil" || bus.Js("js-asker", "outcome[1]") != "pending"));
            early                  = early || (!jsAnswered && bus.Lua("lua-asker", "outcome[2]") != "nil");
            return bus.Lua("lua-asker", "outcome[1] and outcome[2] and outcome[3] and true") == "true" && bus.Js("js-asker", "outcome[1]") != "pending";
        }),
            true);
        EQUALS(early, false);
        EQUALS(bus.Lua("lua-asker", "outcome[1]"), "true|42");
        EQUALS(bus.Lua("lua-asker", "outcome[2]"), "true|8");
        EQUALS(bus.Lua("lua-asker", "outcome[3]"), "true|15");
        EQUALS(bus.Js("js-asker", "outcome[1]"), "resolved|100");
    });

    IT("counts the first reply only, and takes one from a handler that then goes on", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("lua-provider", {{"main.lua", "Messages.handle('twice', function(payload, reply) reply('first') reply('second') sleep(10) reply('third') done = true end)"}});
        bus.server.files.Resource("js-provider", {{"main.js", "Messages.handle('twice', (payload, reply) => { reply('first'); reply('second'); });"}});
        bus.server.files.Resource("lua-asker", {{"main.lua", LuaMessagesTest::kLuaAsker}});
        bus.server.files.Resource("js-asker", {{"main.js", LuaMessagesTest::kJsAsker}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-provider", "js-provider", "lua-asker", "js-asker"}), true);

        EQUALS(bus.Run("lua-asker", "try('lua-provider', 'twice', nil, 1)"), "");
        EQUALS(bus.Run("lua-asker", "try('js-provider', 'twice', nil, 2)"), "");
        EQUALS(bus.Js("js-asker", "tryIt('lua-provider', 'twice')"), "asked");
        EQUALS(bus.Until([&] {
            return bus.Lua("lua-provider", "done") == "true" && bus.Lua("lua-asker", "outcome[1] and outcome[2] and true") == "true" && bus.Js("js-asker", "outcome[1]") != "pending";
        }),
            true);
        EQUALS(bus.Lua("lua-asker", "outcome[1]"), "true|first");
        EQUALS(bus.Lua("lua-asker", "outcome[2]"), "true|first");
        EQUALS(bus.Js("js-asker", "outcome[1]"), "resolved|first");
    });

    IT("says that a Lua script cannot wait for a reply where it cannot wait", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", "calls = 0 Messages.handle('ping', function(payload, reply) calls = calls + 1 reply(1) end)"}});
        bus.server.files.Resource("asker", {{"main.lua", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "asker"}), true);

        // On the main thread: the handler was called, and its reply has nobody to go to.
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("asker", "select(2, pcall(Messages.request, 'provider', 'ping'))"), "cannot wait here"), true);
        EQUALS(bus.Lua("provider", "calls"), "1");
        bus.server.module.Update();
        EQUALS(bus.Lua("provider", "calls"), "1");
    });

    // ==================== What fails a request ====================

    IT("fails a request nobody handles the way a promise is rejected", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("lua-provider", {{"main.lua", "Messages.handle('known', function(payload, reply) reply(1) end)"}});
        bus.server.files.Resource("lua-asker", {{"main.lua", ""}});
        bus.server.files.Resource("js-asker", {{"main.js", LuaMessagesTest::kJsAsker}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-provider", "lua-asker", "js-asker"}), true);

        // Known at once, so raised at once, also where a script cannot wait.
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "select(2, pcall(Messages.request, 'nobody', 'known'))"), "Target resource not found"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "select(2, pcall(Messages.request, 'lua-provider', 'unknown'))"), "No handler for message type"), true);
        EQUALS(bus.Lua("lua-asker", "(pcall(Messages.request, 'js-asker', 'known'))"), "false");

        EQUALS(bus.Js("js-asker", "tryIt('lua-provider', 'unknown', 1, 1)"), "asked");
        EQUALS(bus.Js("js-asker", "tryIt('lua-asker', 'known', 1, 2)"), "asked");
        EQUALS(bus.Until([&] {
            return bus.Js("js-asker", "outcome[1]") != "pending" && bus.Js("js-asker", "outcome[2]") != "pending";
        }),
            true);
        EQUALS(bus.Js("js-asker", "outcome[1]"), "rejected|string|No handler for message type");
        EQUALS(bus.Js("js-asker", "outcome[2]"), "rejected|string|Target resource not found");
    });

    IT("fails the request of a handler that fails before it answers", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("lua-provider", {{"main.lua", R"(
            Messages.handle('explode', function() error('lua boom') end)
            Messages.handle('late', function() sleep(10) error('late boom') end)
        )"}},
            R"(,"errorBehavior":"continue")");
        bus.server.files.Resource("js-provider", {{"main.js", R"(
            Messages.handle('explode', () => { throw new Error('js boom'); });
            Messages.handle('late', async () => { await new Promise((resolve) => setTimeout(resolve, 10)); throw new Error('late js boom'); });
        )"}},
            R"(,"errorBehavior":"continue")");
        bus.server.files.Resource("lua-asker", {{"main.lua", LuaMessagesTest::kLuaAsker}});
        bus.server.files.Resource("js-asker", {{"main.js", LuaMessagesTest::kJsAsker}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-provider", "js-provider", "lua-asker", "js-asker"}), true);
        std::vector<std::string> errors;
        LuaMessagesTest::CollectErrors(bus, errors);

        // A handler that failed at once fails the request at once.
        EQUALS(bus.Run("lua-asker", "try('lua-provider', 'explode', nil, 1)"), "");
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "outcome[1]"), "false|"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "outcome[1]"), "lua boom"), true);
        EQUALS(bus.Run("lua-asker", "try('js-provider', 'explode', nil, 2)"), "");
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "outcome[2]"), "false|"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "outcome[2]"), "js boom"), true);

        EQUALS(bus.Run("lua-asker", "try('lua-provider', 'late', nil, 3)"), "");
        EQUALS(bus.Run("lua-asker", "try('js-provider', 'late', nil, 4)"), "");
        EQUALS(bus.Js("js-asker", "tryIt('lua-provider', 'explode', undefined, 1)"), "asked");
        EQUALS(bus.Js("js-asker", "tryIt('lua-provider', 'late', undefined, 2)"), "asked");
        EQUALS(bus.Until([&] {
            return bus.Lua("lua-asker", "outcome[3] and outcome[4] and true") == "true" && bus.Js("js-asker", "outcome[1]") != "pending" && bus.Js("js-asker", "outcome[2]") != "pending";
        }),
            true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "outcome[3]"), "false|"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "outcome[3]"), "late boom"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "outcome[4]"), "false|"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "outcome[4]"), "late js boom"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Js("js-asker", "outcome[1]"), "rejected|Error|"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Js("js-asker", "outcome[1]"), "lua boom"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Js("js-asker", "outcome[2]"), "rejected|Error|"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Js("js-asker", "outcome[2]"), "late boom"), true);

        // The requester was told: the handler's resource is not failed for it.
        bus.server.module.Update();
        EQUALS(errors.size(), static_cast<size_t>(0));
    });

    IT("reports a handler that fails once nobody waits for it", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("lua-provider", {{"main.lua", R"(
            Messages.handle('after', function(payload, reply) reply('fine') error('after boom') end)
            Messages.handle('note', function() error('note boom') end)
        )"}},
            R"(,"errorBehavior":"continue")");
        bus.server.files.Resource("js-provider", {{"main.js", "Messages.handle('note', () => { throw new Error('js note boom'); });"}}, R"(,"errorBehavior":"continue")");
        bus.server.files.Resource("lua-asker", {{"main.lua", LuaMessagesTest::kLuaAsker}});
        bus.server.files.Resource("js-asker", {{"main.js", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-provider", "js-provider", "lua-asker", "js-asker"}), true);
        std::vector<std::string> errors;
        LuaMessagesTest::CollectErrors(bus, errors);

        // The reply made before the failure still arrives.
        EQUALS(bus.Run("lua-asker", "try('lua-provider', 'after')"), "");
        EQUALS(bus.Until([&] {
            return bus.Lua("lua-asker", "outcome[1]") != "nil";
        }),
            true);
        EQUALS(bus.Lua("lua-asker", "outcome[1]"), "true|fine");
        EQUALS(LuaMessagesTest::AnyContains(errors, "lua-provider: Message handler 'after' error: "), true);
        EQUALS(LuaMessagesTest::AnyContains(errors, "after boom"), true);

        // A notification has nobody to tell, whoever sent it.
        errors.clear();
        EQUALS(bus.Lua("lua-asker", "Messages.send('lua-provider', 'note')"), "nil");
        EQUALS(bus.Js("js-asker", "Messages.send('lua-provider', 'note')"), "undefined");
        EQUALS(bus.Lua("lua-asker", "Messages.send('js-provider', 'note')"), "nil");
        bus.server.module.Update();
        EQUALS(errors.size(), static_cast<size_t>(3));
        EQUALS(LuaMessagesTest::AnyContains(errors, "lua-provider: Message handler 'note' error: "), true);
        EQUALS(LuaMessagesTest::AnyContains(errors, "js-provider: Message handler 'note' error: "), true);
        EQUALS(LuaMessagesTest::AnyContains(errors, "js note boom"), true);
        // Left running, as its manifest asks.
        EQUALS(bus.Lua("lua-provider", "type(Messages)"), "table");
    });

    IT("refuses a payload that cannot be copied before anything happens", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("lua-provider", {{"main.lua", "calls = 0 Messages.handle('count', function(payload, reply) calls = calls + 1 reply(calls) end)"}});
        bus.server.files.Resource("lua-asker", {{"main.lua", LuaMessagesTest::kLuaAsker}});
        bus.server.files.Resource("js-asker", {{"main.js", LuaMessagesTest::kJsAsker}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-provider", "lua-asker", "js-asker"}), true);

        // A Lua caller is told as it calls, whoever would have handled it, and also when nobody would.
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "select(2, pcall(Messages.request, 'lua-provider', 'count', coroutine.create(function() end)))"), "Messages.request: payload: "), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "select(2, pcall(Messages.send, 'lua-provider', 'count', coroutine.create(function() end)))"), "Messages.send: payload: "), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "select(2, pcall(Messages.send, 'nobody', 'count', coroutine.create(function() end)))"), "Messages.send: payload: "), true);

        // A JavaScript caller's promise is rejected, as it is when the handler is JavaScript of another runtime;
        // its notification is dropped.
        EQUALS(bus.Js("js-asker", "tryIt('lua-provider', 'count', Symbol('no'))"), "asked");
        EQUALS(bus.Js("js-asker", "String(Messages.send('lua-provider', 'count', Symbol('no')))"), "undefined");
        EQUALS(bus.Until([&] {
            return bus.Js("js-asker", "outcome[1]") != "pending";
        }),
            true);
        EQUALS(LuaRuntimeTest::Contains(bus.Js("js-asker", "outcome[1]"), "rejected|TypeError|Messages.request: payload: "), true);
        EQUALS(bus.Lua("lua-provider", "calls"), "0");

        // Nothing was left behind: the next request is the first the handler sees, and is answered.
        EQUALS(bus.Run("lua-asker", "try('lua-provider', 'count', {})"), "");
        EQUALS(bus.Until([&] {
            return bus.Lua("lua-asker", "outcome[1]") != "nil";
        }),
            true);
        EQUALS(bus.Lua("lua-asker", "outcome[1]"), "true|1");
    });

    IT("words the errors of a wrong call as JavaScript does", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("script", {{"main.lua", "function why(f, ...) return select(2, pcall(f, ...)) end"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("script"), "");

        EQUALS(LuaRuntimeTest::Contains(bus.Lua("script", "why(Messages.handle, 'x')"), "Messages.handle requires 2 arguments: messageType, handler"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("script", "why(Messages.handle, 1, function() end)"), "Messages.handle: messageType must be a string"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("script", "why(Messages.handle, 'x', 1)"), "Messages.handle: handler must be a function"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("script", "why(Messages.request, 'x')"), "Messages.request requires at least 2 arguments: resourceName, messageType"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("script", "why(Messages.request, 1, 'x')"), "Messages.request: resourceName must be a string"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("script", "why(Messages.request, 'x', 1)"), "Messages.request: messageType must be a string"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("script", "why(Messages.send, 'x')"), "Messages.send requires at least 2 arguments: resourceName, messageType"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("script", "why(Messages.send, 1, 'x')"), "Messages.send: resourceName must be a string"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("script", "why(Messages.send, 'x', 1)"), "Messages.send: messageType must be a string"), true);
        EQUALS(bus.Lua("script", "Messages.handle('x', function() end)"), "nil");
    });

    // ==================== Notifications ====================

    IT("sends a notification to a handler of either language", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("lua-listener", {{"main.lua", "Messages.handle('note', function(payload, reply) got = payload.text .. '|' .. type(reply) .. '|' .. tostring(reply('ignored')) end)"}});
        bus.server.files.Resource("js-listener", {{"main.js", "Messages.handle('note', (payload, reply) => { globalThis.got = payload.text + '|' + typeof reply + '|' + reply('ignored'); });"}});
        bus.server.files.Resource("lua-sender", {{"main.lua", ""}});
        bus.server.files.Resource("js-sender", {{"main.js", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-listener", "js-listener", "lua-sender", "js-sender"}), true);

        EQUALS(bus.Lua("lua-sender", "Messages.send('lua-listener', 'note', {text = 'from lua'})"), "nil");
        EQUALS(bus.Lua("lua-listener", "got"), "from lua|function|nil");
        EQUALS(bus.Lua("lua-sender", "Messages.send('js-listener', 'note', {text = 'from lua'})"), "nil");
        EQUALS(bus.Js("js-listener", "got"), "from lua|function|undefined");
        EQUALS(bus.Js("js-sender", "String(Messages.send('lua-listener', 'note', {text: 'from js'}))"), "undefined");
        EQUALS(bus.Lua("lua-listener", "got"), "from js|function|nil");

        // Nobody to tell is not an error.
        EQUALS(bus.Lua("lua-sender", "Messages.send('nobody', 'note')"), "nil");
        EQUALS(bus.Lua("lua-sender", "Messages.send('lua-listener', 'unknown')"), "nil");
        EQUALS(bus.Js("js-sender", "String(Messages.send('lua-listener', 'unknown'))"), "undefined");
    });

    // ==================== Lifetime ====================

    IT("answers the next request with the handler a handler put in its own place", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("lua-provider", {{"main.lua", R"(
            Messages.handle('swap', function(payload, reply)
                Messages.handle('swap', function(payload, reply) reply('second') end)
                reply('first')
            end)
        )"}});
        bus.server.files.Resource("js-provider", {{"main.js", R"(
            Messages.handle('swap', (payload, reply) => {
                Messages.handle('swap', (payload, reply) => reply('second'));
                reply('first');
            });
        )"}});
        bus.server.files.Resource("asker", {{"main.lua", LuaMessagesTest::kLuaAsker}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-provider", "js-provider", "asker"}), true);
        EQUALS(bus.LuaCallables("lua-provider"), static_cast<size_t>(1));
        EQUALS(bus.JsCallables("js-provider"), static_cast<size_t>(1));

        for (const char *round : {"first", "second", "second"}) {
            EQUALS(bus.Run("asker", "outcome = {} try('lua-provider', 'swap', nil, 1) "), "");
            EQUALS(bus.Run("asker", "try('js-provider', 'swap', nil, 2)"), "");
            EQUALS(bus.Until([&] {
                return bus.Lua("asker", "outcome[1] and outcome[2] and true") == "true";
            }),
                true);
            EQUALS(bus.Lua("asker", "outcome[1]"), std::string("true|") + round);
            EQUALS(bus.Lua("asker", "outcome[2]"), std::string("true|") + round);
        }

        // The handler that was replaced is let go of.
        EQUALS(bus.LuaCallables("lua-provider"), static_cast<size_t>(1));
        EQUALS(bus.JsCallables("js-provider"), static_cast<size_t>(1));
    });

    IT("fails the requests a resource owes an answer to when it stops", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("lua-provider", {{"main.lua", "Messages.handle('never', function(payload, reply) saved = reply end) Messages.handle('asleep', function() sleep(60000) end)"}});
        bus.server.files.Resource("js-provider", {{"main.js", "Messages.handle('never', (payload, reply) => { globalThis.saved = reply; });"}});
        bus.server.files.Resource("lua-asker", {{"main.lua", LuaMessagesTest::kLuaAsker}});
        bus.server.files.Resource("js-asker", {{"main.js", LuaMessagesTest::kJsAsker}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-provider", "js-provider", "lua-asker", "js-asker"}), true);

        EQUALS(bus.Run("lua-asker", "try('lua-provider', 'never', nil, 1)"), "");
        EQUALS(bus.Run("lua-asker", "try('lua-provider', 'asleep', nil, 2)"), "");
        EQUALS(bus.Run("lua-asker", "try('js-provider', 'never', nil, 3)"), "");
        EQUALS(bus.Js("js-asker", "tryIt('lua-provider', 'never', 1, 1)"), "asked");
        for (int i = 0; i < 5; ++i) {
            bus.server.module.Update();
        }
        EQUALS(bus.Lua("lua-asker", "outcome[1] or outcome[2] or outcome[3]"), "nil");
        EQUALS(bus.Js("js-asker", "outcome[1]"), "pending");

        EQUALS((bool)bus.Manager().StopResource("lua-provider"), true);
        EQUALS(bus.Until([&] {
            return bus.Lua("lua-asker", "outcome[1] and outcome[2] and true") == "true" && bus.Js("js-asker", "outcome[1]") != "pending";
        }),
            true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "outcome[1]"), "false|"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "outcome[1]"), "Messages.request: resource 'lua-provider' stopped before reply"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "outcome[2]"), "Messages.request: resource 'lua-provider' stopped before reply"), true);
        EQUALS(bus.Js("js-asker", "outcome[1]"), "rejected|string|Messages.request: resource 'lua-provider' stopped before reply");
        EQUALS(bus.Lua("lua-asker", "outcome[3]"), "nil");

        EQUALS((bool)bus.Manager().StopResource("js-provider"), true);
        EQUALS(bus.Until([&] {
            return bus.Lua("lua-asker", "outcome[3]") != "nil";
        }),
            true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "outcome[3]"), "Messages.request: resource 'js-provider' stopped before reply"), true);

        // Its handlers went with it.
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("lua-asker", "select(2, pcall(Messages.request, 'lua-provider', 'never'))"), "Target resource not found"), true);
        EQUALS(bus.Lua("lua-asker", "Messages.send('lua-provider', 'never')"), "nil");
    });

    IT("lets a handler answer a requester that has stopped, to no effect", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("lua-provider", {{"main.lua", R"(
            saved = {}
            Messages.handle('hold', function(payload, reply) saved[payload] = reply end)
            function answer(who) return tostring(pcall(saved[who], 'late')) end
            Messages.handle('ping', function(payload, reply) reply('pong') end)
        )"}});
        bus.server.files.Resource("js-provider", {{"main.js", R"(
            globalThis.saved = {};
            Messages.handle('hold', (payload, reply) => { saved[payload] = reply; });
            globalThis.answer = (who) => { try { return String(saved[who]('late')); } catch (e) { return 'threw ' + e.message; } };
        )"}});
        bus.server.files.Resource("lua-asker", {{"main.lua", LuaMessagesTest::kLuaAsker}});
        bus.server.files.Resource("js-asker", {{"main.js", LuaMessagesTest::kJsAsker}});
        bus.server.files.Resource("other", {{"main.lua", LuaMessagesTest::kLuaAsker}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-provider", "js-provider", "lua-asker", "js-asker", "other"}), true);

        EQUALS(bus.Run("lua-asker", "try('lua-provider', 'hold', 'lua', 1)"), "");
        EQUALS(bus.Run("lua-asker", "try('js-provider', 'hold', 'lua', 2)"), "");
        EQUALS(bus.Js("js-asker", "tryIt('lua-provider', 'hold', 'js')"), "asked");
        EQUALS(bus.Lua("lua-provider", "type(saved.lua) .. type(saved.js)"), "functionfunction");

        EQUALS((bool)bus.Manager().StopResource("lua-asker"), true);
        EQUALS((bool)bus.Manager().StopResource("js-asker"), true);
        bus.server.module.Update();

        // The request went with its requester: the reply finds nothing and raises nothing.
        EQUALS(bus.Lua("lua-provider", "answer('lua')"), "true");
        EQUALS(bus.Lua("lua-provider", "answer('js')"), "true");
        EQUALS(bus.Js("js-provider", "answer('lua')"), "undefined");
        bus.server.module.Update();

        EQUALS(bus.Manager().IsResourceRunning("lua-provider"), true);
        EQUALS(bus.Manager().IsResourceRunning("js-provider"), true);
        EQUALS(bus.Run("other", "try('lua-provider', 'ping')"), "");
        EQUALS(bus.Until([&] {
            return bus.Lua("other", "outcome[1]") != "nil";
        }),
            true);
        EQUALS(bus.Lua("other", "outcome[1]"), "true|pong");
    });

    IT("survives a resource that stops in the middle of a request", {
        LuaMessagesTest::StopRig messages;
        auto &stops = messages.stops;
        auto &rig   = stops.rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("lua-self", {{"main.lua", "Messages.handle('halt', function(payload, reply) log('lua handler') stop('lua-self') reply('bye') log('lua after') end)"}});
        rig.files.Resource("js-self", {{"main.js", "Messages.handle('halt', (payload, reply) => { log('js handler'); stop('js-self'); reply('bye'); log('js after'); });"}});
        rig.files.Resource("killer", {{"main.lua", "Messages.handle('kill', function(who, reply) stop(who) reply('done') log('lua killed ' .. who) end)"}, {"main.js", "Messages.handle('kill-js', (who, reply) => { stop(who); reply('done'); log('js killed ' + who); });"}});
        rig.files.Resource("lua-asker", {{"main.lua", LuaMessagesTest::kLuaAsker}});
        rig.files.Resource("js-asker", {{"main.js", LuaMessagesTest::kJsAsker}});
        for (const char *resource : {"lua-self", "js-self", "killer", "lua-asker", "js-asker"}) {
            EQUALS(rig.Start(resource), "");
        }
        const std::string stopped = "Messages.request: resource '";

        // A handler that stops its own resource runs on to its end. The request was failed as the resource
        // stopped, so the reply it then makes finds nothing. Lua asks Lua, then JavaScript.
        LuaResourcesTest::Log().clear();
        EQUALS(messages.Run("lua-asker", "try('lua-self', 'halt', nil, 1)"), "");
        EQUALS(LuaRuntimeTest::Contains(rig.Eval("lua-asker", "outcome[1]"), stopped + "lua-self' stopped before reply"), true);
        EQUALS(stops.Said(), "lua handler,lua after");
        EQUALS(messages.Run("lua-asker", "try('js-self', 'halt', nil, 2)"), "");
        EQUALS(LuaRuntimeTest::Contains(rig.Eval("lua-asker", "outcome[2]"), stopped + "js-self' stopped before reply"), true);
        EQUALS(stops.Said(), "lua handler,lua after,js handler,js after");
        messages.Pump();
        EQUALS(rig.HasLuaRuntime("lua-self"), false);
        EQUALS(rig.HasJavaScriptRuntime("js-self"), false);

        // JavaScript asks Lua.
        EQUALS(rig.Start("lua-self"), "");
        EQUALS(stops.Js("js-asker", "tryIt('lua-self', 'halt')"), "asked");
        messages.Pump();
        EQUALS(stops.Js("js-asker", "outcome[1]"), "rejected|string|" + stopped + "lua-self' stopped before reply");

        // A handler that stops the resource that asked: nobody is left to tell, and the reply finds nothing.
        // A Lua handler under a Lua requester, then under a JavaScript one; a JavaScript handler under a Lua one.
        LuaResourcesTest::Log().clear();
        EQUALS(messages.Run("lua-asker", "try('killer', 'kill', 'lua-asker', 3)"), "");
        messages.Pump();
        EQUALS(rig.HasLuaRuntime("lua-asker"), false);
        EQUALS(stops.Js("js-asker", "tryIt('killer', 'kill', 'js-asker')"), "asked");
        messages.Pump();
        EQUALS(rig.HasJavaScriptRuntime("js-asker"), false);
        EQUALS(rig.Start("lua-asker"), "");
        EQUALS(messages.Run("lua-asker", "try('killer', 'kill-js', 'lua-asker', 1)"), "");
        messages.Pump();
        EQUALS(rig.HasLuaRuntime("lua-asker"), false);
        EQUALS(stops.Said(), "lua killed lua-asker,lua killed js-asker,js killed lua-asker");
        EQUALS(rig.manager.IsResourceRunning("killer"), true);

        // A runtime that goes without its resource being stopped leaves a handler nobody can call: it is not
        // called, and not found.
        EQUALS(rig.Start("lua-asker"), "");
        EQUALS(rig.Start("js-asker"), "");
        rig.lua.DestroyResourceRuntime("killer");
        EQUALS(LuaRuntimeTest::Contains(rig.Eval("lua-asker", "select(2, pcall(Messages.request, 'killer', 'kill', 'js-asker'))"), "No handler for message type"), true);
        EQUALS(rig.Eval("lua-asker", "Messages.send('killer', 'kill', 'js-asker')"), "nil");
        EQUALS(stops.Js("js-asker", "tryIt('killer', 'kill', 'lua-asker')"), "asked");
        messages.Pump();
        EQUALS(stops.Js("js-asker", "outcome[1]"), "rejected|string|No handler for message type");
        EQUALS(rig.manager.IsResourceRunning("lua-asker"), true);
        EQUALS(rig.manager.IsResourceRunning("js-asker"), true);
    });

    IT("keeps no request of a Lua runtime that went, or of a call that could not wait", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", "calls = 0 Messages.handle('never', function() calls = calls + 1 end) Messages.handle('ping', function(payload, reply) calls = calls + 1 reply(1) end)"}});
        bus.server.files.Resource("asker", {{"main.lua", LuaMessagesTest::kLuaAsker}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "asker"}), true);
        EQUALS(Framework::Scripting::Builtins::Messages::GetPendingRequestCount(), static_cast<size_t>(0));

        // On the main thread a request cannot wait. Its handler was called all the same; whether it answered or
        // not, nothing is kept for the request once the server has updated.
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("asker", "select(2, pcall(Messages.request, 'provider', 'never'))"), "cannot wait here"), true);
        EQUALS(LuaRuntimeTest::Contains(bus.Lua("asker", "select(2, pcall(Messages.request, 'provider', 'ping'))"), "cannot wait here"), true);
        EQUALS(bus.Lua("provider", "calls"), "2");
        bus.server.module.Update();
        EQUALS(Framework::Scripting::Builtins::Messages::GetPendingRequestCount(), static_cast<size_t>(0));

        // A request that waits is kept, until its runtime goes: here without its resource being stopped.
        EQUALS(bus.Run("asker", "try('provider', 'never')"), "");
        bus.server.module.Update();
        EQUALS(Framework::Scripting::Builtins::Messages::GetPendingRequestCount(), static_cast<size_t>(1));
        bus.server.module.GetLuaEngine()->DestroyResourceRuntime("asker");
        EQUALS(Framework::Scripting::Builtins::Messages::GetPendingRequestCount(), static_cast<size_t>(0));
        EQUALS(bus.Manager().IsResourceRunning("provider"), true);
    });

    IT("rejects the promise of a reply its runtime cannot be given", {
        LuaEventsTest::Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", ""}});
        bus.server.files.Resource("asker", {{"main.js", LuaMessagesTest::kJsAsker}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "asker"}), true);

        // A handle the provider's Lua runtime has and the requester's JavaScript one does not.
        LuaTransferTest::TicketDefinition().RegisterLuaGlobal(bus.server.module.GetLuaEngine()->GetRuntime("provider")->State(), Framework::Scripting::Bind::Side::Server);
        // It leaves Lua as a host type JavaScript was never told of, or is told of no more.
        Framework::Scripting::ValueTransfer::UnregisterHostType("TransferTicket");
        Framework::Scripting::LuaTransfer::RegisterHostType(
            "TransferTicket",
            [](lua_State *, int, std::vector<uint8_t> &bytes) {
                bytes = {7};
                return true;
            },
            [](lua_State *, const std::vector<uint8_t> &) {
                return false;
            });
        EQUALS(bus.Lua("provider", "Messages.handle('ticket', function(payload, reply) reply(TransferTicket.new(7)) answered = true end)"), "nil");

        EQUALS(bus.Js("asker", "tryIt('provider', 'ticket')"), "asked");
        EQUALS(bus.Until([&] {
            return bus.Js("asker", "outcome[1]") != "pending";
        }),
            true);
        Framework::Scripting::LuaTransfer::UnregisterHostType("TransferTicket");
        EQUALS(bus.Lua("provider", "answered"), "true");
        EQUALS(bus.Js("asker", "outcome[1]"), "rejected|Error|Unknown host type TransferTicket");
        EQUALS(Framework::Scripting::Builtins::Messages::GetPendingRequestCount(), static_cast<size_t>(0));
    });

    IT("needs a resource manager to handle and to request, in a runtime that was given none", {
        Framework::Scripting::NodeEngine engine({});
        EQUALS(engine.Init(), Framework::Scripting::ScriptingError::SCRIPTING_NONE);
        {
            Framework::Scripting::EngineScope scope(&engine);
            {
                Framework::Scripting::Bind::Registrar registrar(scope.Isolate(), scope.Context(), scope.Context()->Global());
                Framework::Scripting::Builtins::Messages::Register(registrar);
            }
        }
        const auto why = [&engine](const std::string &call) {
            return RunJSString(engine, ("(() => { try { " + call + "; return 'ok'; } catch (e) { return e.constructor.name + ': ' + e.message; } })()").c_str());
        };

        EQUALS(why("Messages.handle('x', () => {})"), "Error: Messages.handle: resource manager not available");
        EQUALS(why("Messages.request('nobody', 'x')"), "Error: Messages.request: resource manager not available");
        // A notification needs none.
        EQUALS(why("Messages.send('nobody', 'x')"), "ok");
        // The shape of the call is checked first.
        EQUALS(why("Messages.handle('x')"), "TypeError: Messages.handle requires 2 arguments: messageType, handler");

        engine.Shutdown();
    });

    IT("reports a failure nobody waits for to the manager of the handler, also once the sender's is gone", {
        // Two servers' worth of scripting in one process, Lua alone: each a manager over an engine of its own.
        struct Host final {
            LuaResourcesTest::Files files;
            Framework::Scripting::LuaEngine lua;
            std::unique_ptr<Framework::Scripting::ResourceManager> manager;
            std::vector<std::string> errors;
            std::chrono::steady_clock::time_point clock = std::chrono::steady_clock::now();

            bool Init() {
                if (lua.Init() != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
                    return false;
                }
                Framework::Scripting::Bind::WatchRuntimes(lua);
                Framework::Scripting::ResourceManagerConfig config;
                config.resourcesPath = files.root.string();
                manager              = std::make_unique<Framework::Scripting::ResourceManager>(nullptr, config);
                manager->SetLuaEngine(&lua);
                lua.SetRuntimeSetupCallback([this](Framework::Scripting::LuaRuntime &runtime) {
                    Framework::Scripting::Bind::SetCallResourceManager(&runtime, manager.get());
                    {
                        Framework::Scripting::Bind::Registrar registrar;
                        Framework::Scripting::Builtins::Messages::Register(registrar);
                        registrar.Lua().Apply(runtime);
                    }
                });
                lua.AddRuntimeDisposingListener([](Framework::Scripting::LuaRuntime &runtime) {
                    Framework::Scripting::Bind::SetCallResourceManager(&runtime, nullptr);
                });
                manager->AddOnResourceError([this](const std::string &name, const std::string &error) {
                    errors.push_back(name + ": " + LuaRuntimeTest::FirstLine(error));
                });
                manager->DiscoverResources();
                return true;
            }

            // A frame: time moves on by `ms`, and the manager acts on what its handlers failed with.
            void Frame(int ms) {
                // A script's sleep starts from the real clock: a frame never falls behind it.
                clock = std::max(clock, std::chrono::steady_clock::now()) + std::chrono::milliseconds(ms);
                lua.Tick(clock);
                if (manager != nullptr) {
                    manager->ProcessHandlerFailures();
                }
            }
        };

        Host handlers;
        handlers.files.Resource("provider", {{"main.lua", "Messages.handle('note', function(payload) sleep(20) failed = payload error('late ' .. payload) end)"}}, R"(,"errorBehavior":"continue")");
        EQUALS(handlers.Init(), true);
        EQUALS((bool)handlers.manager->StartResource("provider"), true);
        {
            Host senders;
            senders.files.Resource("sender", {{"main.lua", ""}});
            EQUALS(senders.Init(), true);
            EQUALS((bool)senders.manager->StartResource("sender"), true);

            // The handler sleeps, so it fails after the call that woke it has returned: its own manager hears
            // of it, and the sender's does not.
            EQUALS(LuaRuntimeTest::Eval(*senders.lua.GetRuntime("sender"), "Messages.send('provider', 'note', 'one')"), "nil");
            handlers.Frame(30);
            senders.Frame(30);
            EQUALS(LuaRuntimeTest::Eval(*handlers.lua.GetRuntime("provider"), "failed"), "one");
            EQUALS(handlers.errors.size(), static_cast<size_t>(1));
            EQUALS(LuaMessagesTest::AnyContains(handlers.errors, "provider: Message handler 'note' error: "), true);
            EQUALS(LuaMessagesTest::AnyContains(handlers.errors, "late one"), true);
            EQUALS(senders.errors.size(), static_cast<size_t>(0));

            // The sender's manager goes while the handler sleeps.
            EQUALS(LuaRuntimeTest::Eval(*senders.lua.GetRuntime("sender"), "Messages.send('provider', 'note', 'two')"), "nil");
            senders.manager.reset();
        }
        handlers.Frame(30);
        EQUALS(LuaRuntimeTest::Eval(*handlers.lua.GetRuntime("provider"), "failed"), "two");
        EQUALS(handlers.errors.size(), static_cast<size_t>(2));
        EQUALS(LuaMessagesTest::AnyContains(handlers.errors, "late two"), true);

        // A manager that goes takes its handlers with it, and what still fails then is told to nobody.
        EQUALS(LuaRuntimeTest::Eval(*handlers.lua.GetRuntime("provider"), "Messages.send('provider', 'note', 'three')"), "nil");
        handlers.manager.reset();
        handlers.Frame(30);
        EQUALS(handlers.errors.size(), static_cast<size_t>(2));
        Framework::Scripting::Builtins::Messages::Shutdown();
    });

    IT("answers and cleans up under a manager that has no JavaScript engine", {
        LuaResourcesTest::Files files;
        files.Resource("provider", {{"main.lua", "Messages.handle('next', function(payload, reply) sleep(5) reply(payload + 1) end) Messages.handle('never', function() end)"}});
        files.Resource("asker", {{"main.lua", LuaMessagesTest::kLuaAsker}});

        Framework::Scripting::LuaEngine lua;
        EQUALS(lua.Init(), Framework::Scripting::ScriptingError::SCRIPTING_NONE);
        Framework::Scripting::Bind::WatchRuntimes(lua);
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath = files.root.string();
        Framework::Scripting::ResourceManager manager(nullptr, config);
        manager.SetLuaEngine(&lua);
        lua.SetRuntimeSetupCallback([&manager](Framework::Scripting::LuaRuntime &runtime) {
            Framework::Scripting::Bind::SetCallResourceManager(&runtime, &manager);
            {
                Framework::Scripting::Bind::Registrar registrar;
                Framework::Scripting::Builtins::Messages::Register(registrar);
                registrar.Lua().Apply(runtime);
            }
        });
        lua.AddRuntimeDisposingListener([](Framework::Scripting::LuaRuntime &runtime) {
            Framework::Scripting::Bind::SetCallResourceManager(&runtime, nullptr);
        });
        EQUALS(manager.DiscoverResources(), 2u);
        EQUALS((bool)manager.StartResource("provider"), true);
        EQUALS((bool)manager.StartResource("asker"), true);

        const auto outcome = [&lua](const std::string &slot) {
            return LuaRuntimeTest::Eval(*lua.GetRuntime("asker"), "outcome[" + slot + "]");
        };
        const auto until = [&lua, &outcome](const std::string &slot) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (outcome(slot) == "nil" && std::chrono::steady_clock::now() < deadline) {
                lua.Tick();
                Framework::Scripting::Builtins::Messages::ProcessPendingResponses();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return outcome(slot);
        };

        EQUALS(LuaRuntimeTest::Run(*lua.GetRuntime("asker"), "try('provider', 'next', 41, 1)"), "");
        EQUALS(until("1"), "true|42");

        EQUALS(LuaRuntimeTest::Run(*lua.GetRuntime("asker"), "try('provider', 'never', nil, 2)"), "");
        EQUALS((bool)manager.StopResource("provider"), true);
        EQUALS(LuaRuntimeTest::Contains(until("2"), "Messages.request: resource 'provider' stopped before reply"), true);
        EQUALS(LuaRuntimeTest::Contains(LuaRuntimeTest::Eval(*lua.GetRuntime("asker"), "select(2, pcall(Messages.request, 'provider', 'next', 1))"), "Target resource not found"), true);

        manager.StopAll();
        Framework::Scripting::Builtins::Messages::Shutdown();
    });
})
