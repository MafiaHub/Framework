/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_resources_ut.h"

#include "scripting/bind/call.h"
#include "scripting/bind/callable.h"
#include "scripting/builtins/events.h"
#include "scripting/engine_scope.h"

#include <v8pp/convert.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace LuaEventsTest {
    using namespace Framework::Scripting;
    using LuaResourcesTest::Contains;

    // A server scripting module over resources on disk: every resource in a runtime of its own for each language
    // it is written in, with Events registered the way the server registers it.
    struct Bus final {
        LuaResourcesTest::Server server;

        // Brings the module up and finds what was written. Resources are started one by one, in the order a test
        // needs them.
        bool Init() {
            server.module.SetResourcesPath(server.files.root.string());
            if (server.module.Init() != ScriptingError::SCRIPTING_NONE) {
                return false;
            }
            Manager().DiscoverResources();
            return true;
        }

        ResourceManager &Manager() {
            return *server.module.GetResourceManager();
        }

        Builtins::Events &Events() {
            return Manager().GetEvents();
        }

        // Empty when the resource started, the error otherwise.
        std::string Start(const std::string &resource) {
            const auto result = Manager().StartResource(resource);
            return result ? std::string() : result.GetError();
        }

        bool StartAll(const std::vector<std::string> &resources) {
            for (const std::string &resource : resources) {
                if (!Start(resource).empty()) {
                    return false;
                }
            }
            return true;
        }

        std::string Lua(const std::string &resource, const std::string &expression) const {
            return server.Eval(resource, expression);
        }

        // Runs a chunk in a coroutine of the resource, where it may wait. Empty, or what it raised before it
        // first waited.
        std::string Run(const std::string &resource, const std::string &source) const {
            LuaRuntime *runtime = server.module.GetLuaEngine()->GetRuntime(resource);
            return runtime != nullptr ? LuaRuntimeTest::FirstLine(LuaRuntimeTest::Run(*runtime, source)) : "<no runtime>";
        }

        std::string Js(const std::string &resource, const std::string &expression) const {
            return server.EvalJavaScript(resource, expression);
        }

        // Ticks the server until `done` says so. False when time ran out first.
        bool Until(const std::function<bool()> &done, int timeoutMs = 3000) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            for (;;) {
                server.module.Update();
                if (done()) {
                    return true;
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        // Runs `emit` in the isolate of the engine, as native code that emits does.
        template <typename F>
        auto Native(F &&emit) {
            EngineScope scope(server.module.GetEngine());
            return emit(scope.Isolate(), scope.Context(), Events());
        }
    };

    // What a Lua listener keeps of what it was given: scalars, a table, a list in it and a Vector3. It then
    // changes the table, which is its own copy.
    inline const char *kLuaListener = R"(
        Events.on('ping', function(n, s, flag, t, v)
            got = table.concat({tostring(n), s, tostring(flag), t.k, tostring(t.list[2]), v:toString()}, '|')
            t.k = 'changed'
        end)
    )";

    inline const char *kJsListener = R"(
        Events.on('ping', (n, s, flag, t, v) => {
            globalThis.got = [n, s, flag, t.k, t.list[1], v instanceof Vector3, v.x + v.y + v.z].join('|');
            t.k = 'changed';
        });
    )";
} // namespace LuaEventsTest

MODULE(lua_events, {
    using namespace Framework::Scripting;
    using LuaEventsTest::Bus;
    using LuaEventsTest::Contains;

    // ==================== Dispatch across languages ====================

    IT("delivers a Lua emit to Lua and JavaScript handlers, as copies", {
        Bus bus;
        bus.server.files.Resource("lua-listener", {{"main.lua", LuaEventsTest::kLuaListener}});
        bus.server.files.Resource("js-listener", {{"main.js", LuaEventsTest::kJsListener}});
        bus.server.files.Resource("sender", {{"main.lua", R"(
            function fire()
                local t = {k = 'y', list = {1, 2}}
                local results = table.pack(Events.emit('ping', 1, 'x', true, t, Vector3.new(1, 2, 3)))
                return t.k .. '|' .. results.n
            end
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-listener", "js-listener", "sender"}), true);

        // Every handler finished at once, so the emit did not wait: it returned on the main thread, with one nil.
        EQUALS(bus.Lua("sender", "fire()"), "y|1");
        EQUALS(bus.Lua("lua-listener", "got"), "1|x|true|y|2|Vector3(1, 2, 3)");
        EQUALS(bus.Js("js-listener", "got"), "1|x|true|y|2|true|6");
    });

    IT("delivers a JavaScript emit to Lua handlers as copies, and resolves its promise", {
        Bus bus;
        bus.server.files.Resource("lua-listener", {{"main.lua", LuaEventsTest::kLuaListener}});
        bus.server.files.Resource("js-listener", {{"main.js", LuaEventsTest::kJsListener}});
        bus.server.files.Resource("sender", {{"main.js", R"(
            globalThis.settled = 'pending';
            globalThis.fire = () => {
                const t = {k: 'y', list: [1, 2]};
                Events.emit('ping', 1, 'x', true, t, new Vector3(1, 2, 3)).then((value) => { settled = 'resolved|' + t.k + '|' + value; }, () => { settled = 'rejected'; });
                return 'emitted';
            };
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-listener", "js-listener", "sender"}), true);

        EQUALS(bus.Js("sender", "fire()"), "emitted");
        EQUALS(bus.Lua("lua-listener", "got"), "1|x|true|y|2|Vector3(1, 2, 3)");
        EQUALS(bus.Js("js-listener", "got"), "1|x|true|y|2|true|6");
        EQUALS(bus.Until([&] {
            return bus.Js("sender", "settled") != "pending";
        }),
            true);
        EQUALS(bus.Js("sender", "settled"), "resolved|y|undefined");
    });

    IT("passes the same objects to the JavaScript handlers of the emitter's own isolate", {
        Bus bus;
        bus.server.files.Resource("lua-listener", {{"main.lua", "Events.on('share', function(o) o.lua = true seen = o.first end)"}});
        bus.server.files.Resource("sharing", {{"main.js", R"(
            Events.on('share', (o) => { o.first = true; globalThis.seen = o; });
            Events.on('share', (o) => { globalThis.sawFirst = o.first === true; o.second = true; });
            globalThis.probe = () => {
                const o = {};
                Events.emit('share', o);
                return [seen === o, sawFirst, o.second === true, 'lua' in o].join('|');
            };
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"sharing", "lua-listener"}), true);

        // The handlers and the emitter share one object; the Lua handler, called last, got a copy of it as it
        // was by then, and what it wrote stayed in its copy.
        EQUALS(bus.Js("sharing", "probe()"), "true|true|true|false");
        EQUALS(bus.Lua("lua-listener", "seen"), "true");
    });

    IT("hands a function across as a function that calls the original", {
        Bus bus;
        bus.server.files.Resource("js-listener", {{"main.js", "Events.on('callback', (f) => { f(21); });"}});
        bus.server.files.Resource("lua-listener", {{"main.lua", "Events.on('callback', function(f) f(100) end)"}});
        bus.server.files.Resource("sender", {{"main.lua", "answers = {} function fire() Events.emit('callback', function(x) answers[#answers + 1] = x * 2 end) return table.concat(answers, ',') end"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"js-listener", "lua-listener", "sender"}), true);

        EQUALS(bus.Lua("sender", "fire()"), "42,200");
    });

    IT("calls handlers of both languages in the order they were registered", {
        Bus bus;
        bus.server.files.Resource("mixed", {{"a.lua", "Events.on('order', function(list) order = (order or '') .. 'lua1,' end)"},
                                               {"b.js", "Events.on('order', () => { Events.emit('note', 'js'); });"},
                                               {"c.lua", "Events.on('order', function() order = order .. 'lua2' end) Events.on('note', function(who) order = order .. who .. ',' end)"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("mixed"), "");

        EQUALS(bus.Lua("mixed", "Events.emit('order')"), "nil");
        EQUALS(bus.Lua("mixed", "order"), "lua1,js,lua2");
    });

    // ==================== The surface in Lua ====================

    IT("fires a once handler one time", {
        Bus bus;
        bus.server.files.Resource("listener", {{"main.lua", "count = 0 Events.once('one', function() count = count + 1 end)"}});
        bus.server.files.Resource("sender", {{"main.lua", ""}, {"main.js", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"listener", "sender"}), true);

        EQUALS(bus.Lua("sender", "Events.listenerCount('one')"), "1");
        EQUALS(bus.Lua("sender", "Events.emit('one')"), "nil");
        EQUALS(bus.Lua("sender", "Events.emit('one')"), "nil");
        EQUALS(bus.Lua("listener", "count"), "1");
        EQUALS(bus.Lua("sender", "Events.listenerCount('one')"), "0");
    });

    IT("removes a handler by its function and by the function on returned", {
        Bus bus;
        bus.server.files.Resource("listener", {{"main.lua", R"(
            hits = 0
            function handler() hits = hits + 1 end
            Events.on('e', handler)
            Events.on('e', handler)
            unsubscribe = Events.on('f', function() hits = hits + 10 end)
            Events.on('f', function() hits = hits + 100 end)
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("listener"), "");

        EQUALS(bus.Lua("listener", "type(unsubscribe)"), "function");
        EQUALS(bus.Lua("listener", "Events.listenerCount('e') .. ',' .. Events.listenerCount('f')"), "2,2");

        // A function that was never registered removes nothing; the one that was goes, every time it was.
        EQUALS(bus.Lua("listener", "Events.off('e', function() end)"), "nil");
        EQUALS(bus.Lua("listener", "Events.listenerCount('e')"), "2");
        EQUALS(bus.Lua("listener", "Events.off('e', handler)"), "nil");
        EQUALS(bus.Lua("listener", "Events.listenerCount('e')"), "0");

        EQUALS(bus.Lua("listener", "unsubscribe()"), "nil");
        EQUALS(bus.Lua("listener", "unsubscribe()"), "nil");
        EQUALS(bus.Lua("listener", "Events.listenerCount('f')"), "1");
        EQUALS(bus.Lua("listener", "Events.emit('e')"), "nil");
        EQUALS(bus.Lua("listener", "Events.emit('f')"), "nil");
        EQUALS(bus.Lua("listener", "hits"), "100");
        EQUALS(bus.Events().GetListenerCount("f"), (size_t)1);
    });

    IT("emits to the handlers of one resource with emitTo", {
        Bus bus;
        bus.server.files.Resource("a", {{"main.lua", "Events.on('targeted', function(x) got = x end)"}});
        bus.server.files.Resource("b", {{"main.lua", "Events.on('targeted', function(x) got = x end)"}, {"main.js", "Events.on('targeted', (x) => { globalThis.got = x; });"}});
        bus.server.files.Resource("c", {{"main.js", "Events.on('targeted', (x) => { globalThis.got = x; }); globalThis.got = 'none';"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"a", "b", "c"}), true);

        EQUALS(bus.Lua("a", "Events.emitTo('b', 'targeted', 5)"), "nil");
        EQUALS(bus.Lua("a", "got"), "nil");
        EQUALS(bus.Lua("b", "got"), "5");
        EQUALS(bus.Js("b", "got"), "5");
        EQUALS(bus.Js("c", "got"), "none");

        // And from JavaScript to a Lua resource.
        EQUALS(bus.Js("c", "Events.emitTo('a', 'targeted', 7); 0"), "0");
        EQUALS(bus.Lua("a", "got"), "7");
        EQUALS(bus.Lua("b", "got"), "5");
    });

    IT("keeps local events inside their resource, across its languages", {
        Bus bus;
        const std::string lua = "Events.onLocal('private', function(x) seen = x end) Events.on('private', function(x) shared = x end) function fire(x) Events.emitLocal('private', x) end";
        bus.server.files.Resource("a", {{"main.lua", lua}, {"main.js", "Events.onLocal('private', (x) => { globalThis.seen = x; }); globalThis.fire = (x) => { Events.emitLocal('private', x); };"}});
        bus.server.files.Resource("b", {{"main.lua", lua}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"a", "b"}), true);

        EQUALS(bus.Lua("a", "fire(1)"), "nil");
        EQUALS(bus.Lua("a", "seen"), "1");
        EQUALS(bus.Js("a", "seen"), "1");
        EQUALS(bus.Lua("b", "seen"), "nil");
        // A local event is not the shared one of the same name, either way.
        EQUALS(bus.Lua("a", "shared"), "nil");
        EQUALS(bus.Lua("b", "Events.emit('private', 9)"), "nil");
        EQUALS(bus.Lua("a", "seen .. ',' .. shared"), "1,9");
        EQUALS(bus.Lua("a", "Events.listenerCount('private')"), "2");

        // The JavaScript of the resource emits to its Lua.
        EQUALS(bus.Js("a", "fire(2); 0"), "0");
        EQUALS(bus.Lua("a", "seen"), "2");
        EQUALS(bus.Lua("b", "seen"), "nil");
    });

    IT("counts the handlers of both languages", {
        Bus bus;
        bus.server.files.Resource("a", {{"main.lua", "Events.on('counted', function() end) Events.once('counted', function() end) Events.onClient('counted', function() end)"}});
        bus.server.files.Resource("b", {{"main.js", "Events.on('counted', () => {});"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"a", "b"}), true);

        EQUALS(bus.Lua("a", "Events.listenerCount('counted')"), "3");
        EQUALS(bus.Lua("a", "math.type(Events.listenerCount('counted'))"), "integer");
        EQUALS(bus.Js("b", "Events.listenerCount('counted')"), "3");
        EQUALS(bus.Lua("a", "Events.listenerCount('nobody') .. Events.listenerCount() .. Events.listenerCount(1)"), "000");
        EQUALS(bus.Events().GetListenerCount("counted"), (size_t)3);
        EQUALS(bus.Events().GetClientListenerCount("counted"), (size_t)1);
    });

    IT("has the server members in a server runtime and not the client one", {
        Bus bus;
        bus.server.files.Resource("a", {{"main.lua", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("a"), "");

        const std::string members = "(function() local names = {} for _, name in ipairs({'on', 'once', 'off', 'emit', 'emitTo', 'onLocal', 'emitLocal', 'listenerCount', 'onClient', 'onceClient', 'offClient', 'emitAllClients', 'emitServer'}) do "
                                    "names[#names + 1] = name .. '=' .. type(Events[name]) end return table.concat(names, ' ') end)()";
        EQUALS(bus.Lua("a", members),
            "on=function once=function off=function emit=function emitTo=function onLocal=function emitLocal=function listenerCount=function onClient=function onceClient=function offClient=function emitAllClients=function emitServer=nil");
    });

    IT("raises the errors of JavaScript for a wrong call", {
        Bus bus;
        bus.server.files.Resource("a", {{"main.lua", "function try(f, ...) local ok, err = pcall(f, ...) return ok and 'ok' or err end"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("a"), "");

        EQUALS(bus.Lua("a", "try(Events.on)"), "Events.on requires 2 arguments: eventName, handler");
        EQUALS(bus.Lua("a", "try(Events.on, 1, function() end)"), "Events.on: eventName must be a string");
        EQUALS(bus.Lua("a", "try(Events.on, 'e', 1)"), "Events.on: handler must be a function");
        EQUALS(bus.Lua("a", "try(Events.onClient, 'e')"), "Events.onClient requires 2 arguments: eventName, handler");
        EQUALS(bus.Lua("a", "try(Events.once, 'e')"), "Events.once requires 2 arguments: eventName, handler");
        EQUALS(bus.Lua("a", "try(Events.once, 'e', 1)"), "Events.once: invalid arguments");
        EQUALS(bus.Lua("a", "try(Events.onceClient, 1, 1)"), "Events.onceClient: invalid arguments");
        EQUALS(bus.Lua("a", "try(Events.off, 'e')"), "Events.off requires 2 arguments: eventName, handler");
        EQUALS(bus.Lua("a", "try(Events.off, 'e', 1)"), "Events.off: eventName must be a string and handler a function");
        EQUALS(bus.Lua("a", "try(Events.offClient, 'e', 1)"), "Events.offClient: eventName must be a string and handler a function");
        EQUALS(bus.Lua("a", "try(Events.emit)"), "Events.emit requires at least 1 argument: eventName");
        EQUALS(bus.Lua("a", "try(Events.emit, 1)"), "Events.emit: eventName must be a string");
        EQUALS(bus.Lua("a", "try(Events.emitTo, 'r')"), "Events.emitTo requires at least 2 arguments: resourceName, eventName");
        EQUALS(bus.Lua("a", "try(Events.emitTo, 'r', 1)"), "Events.emitTo: resourceName and eventName must be strings");
        EQUALS(bus.Lua("a", "try(Events.onLocal, 'e')"), "Events.onLocal requires 2 arguments: eventName, handler");
        EQUALS(bus.Lua("a", "try(Events.emitLocal)"), "Events.emitLocal requires at least 1 argument: eventName");
        EQUALS(bus.Lua("a", "try(Events.emitAllClients)"), "Events.emitAllClients requires an eventName string");

        // What cannot be copied is the emitter's mistake, whether or not anybody listens.
        EQUALS(Contains(bus.Lua("a", "try(Events.emit, 'e', 1, coroutine.create(function() end))"), "Events.emit: argument 2: "), true);
        EQUALS(Contains(bus.Lua("a", "try(Events.emitTo, 'a', 'e', coroutine.create(function() end))"), "Events.emitTo: argument 1: "), true);
    });

    IT("sends a network payload as JSON, and a string as it is", {
        Bus bus;
        bus.server.files.Resource("a", {{"main.lua", "function try(f, ...) local ok, err = pcall(f, ...) return ok and 'ok' or err end"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("a"), "");

        // No peer in a test: the call gets as far as needing one once the payload has its JSON.
        EQUALS(bus.Lua("a", "try(Events.emitAllClients, 'named', {a = 1})"), "Events.emitAllClients: network peer unavailable");
        EQUALS(bus.Lua("a", "try(Events.emitAllClients, 'named', '{\"a\":1}')"), "Events.emitAllClients: network peer unavailable");
        EQUALS(bus.Lua("a", "try(Events.emitAllClients, 'named')"), "Events.emitAllClients: network peer unavailable");
        // An empty name is nothing to send.
        EQUALS(bus.Lua("a", "try(Events.emitAllClients, '', {a = 1})"), "ok");
        EQUALS(Contains(bus.Lua("a", "try(Events.emitAllClients, 'named', {f = function() end})"), "Events.emitAllClients: "), true);
        EQUALS(Contains(bus.Lua("a", "try(Events.emitAllClients, 'named', {f = function() end})"), "network peer"), false);
    });

    IT("refuses a JavaScript payload that has no JSON before it sends anything", {
        Bus bus;
        bus.server.files.Resource("a", {{"main.js", "globalThis.attempt = (payload) => { try { Events.emitAllClients('named', payload); return 'sent'; } catch (e) { return e.constructor.name + ': ' + e.message; } };"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("a"), "");

        EQUALS(bus.Js("a", "attempt({a: 1})"), "Error: Events.emitAllClients: network peer unavailable");
        EQUALS(bus.Js("a", "attempt('text')"), "Error: Events.emitAllClients: network peer unavailable");
        EQUALS(bus.Js("a", "attempt(null)"), "Error: Events.emitAllClients: network peer unavailable");
        EQUALS(bus.Js("a", "(() => { const o = {}; o.self = o; return attempt(o); })()").starts_with("TypeError: Events.emitAllClients: "), true);
        EQUALS(bus.Js("a", "attempt(() => {})"), "TypeError: Events.emitAllClients: a value of this kind has no JSON form");
        EQUALS(bus.Js("a", "attempt(Symbol('s'))"), "TypeError: Events.emitAllClients: a value of this kind has no JSON form");
        EQUALS(bus.Js("a", "attempt({ toJSON() { throw new Error('mine'); } })"), "TypeError: Events.emitAllClients: mine");
    });

    // ==================== Outcomes ====================

    IT("lets a Lua handler veto a synchronous emit by returning false", {
        Bus bus;
        bus.server.files.Resource("a", {{"main.lua", R"(
            Events.on('allow', function() calls = (calls or 0) + 1 end)
            Events.on('allow', function() calls = calls + 1 return true end)
            Events.on('allow', function() calls = calls + 1 return 0 end)
            Events.on('deny', function(why) reason = why return false end)
            Events.on('deny', function() after = true end)
            Events.on('late', function() sleep(0) late = true return false end)
            Events.on('broken', function() error('no verdict') end)
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("a"), "");

        const auto ask = [&](const char *event) {
            return bus.Native([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Builtins::Events &events) {
                return events.EmitReservedSync(isolate, context, event, {v8pp::to_v8(isolate, "because")});
            });
        };
        EQUALS(ask("allow"), true);
        EQUALS(bus.Lua("a", "calls"), "3");
        // Every handler runs, whatever an earlier one answered.
        EQUALS(ask("deny"), false);
        EQUALS(bus.Lua("a", "reason .. tostring(after)"), "becausetrue");
        // A handler that waits answers too late, as one that returns a Promise does.
        EQUALS(ask("late"), true);
        EQUALS(bus.Until([&] {
            return bus.Lua("a", "late") == "true";
        }),
            true);
        // One that fails is logged, and vetoes nothing.
        EQUALS(ask("broken"), true);
        EQUALS(ask("nobody"), true);
    });

    IT("makes the emitter wait for a Lua handler that sleeps", {
        Bus bus;
        bus.server.files.Resource("listener", {{"main.lua", "Events.on('slow', function() sleep(30) finished = true end)"}});
        bus.server.files.Resource("js-sender", {{"main.js", "globalThis.settled = 'pending'; globalThis.fire = () => { Events.emit('slow').then(() => { settled = 'resolved'; }, () => { settled = 'rejected'; }); return 'emitted'; };"}});
        bus.server.files.Resource("lua-sender", {{"main.lua", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"listener", "js-sender", "lua-sender"}), true);

        // A JavaScript emitter: its promise is pending for as long as the handler sleeps.
        EQUALS(bus.Js("js-sender", "fire()"), "emitted");
        bool early = false;
        EQUALS(bus.Until([&] {
            const bool finished = bus.Lua("listener", "finished") == "true";
            const bool settled  = bus.Js("js-sender", "settled") != "pending";
            early               = early || (settled && !finished);
            return settled;
        }),
            true);
        EQUALS(early, false);
        EQUALS(bus.Js("js-sender", "settled"), "resolved");

        // A Lua emitter: the coroutine that emitted goes on once the handler is done.
        EQUALS(bus.Lua("listener", "(function() finished = nil end)()"), "nil");
        EQUALS(bus.Run("lua-sender", "Events.emit('slow') resumed = true"), "");
        EQUALS(bus.Lua("lua-sender", "resumed"), "nil");
        early = false;
        EQUALS(bus.Until([&] {
            const bool finished = bus.Lua("listener", "finished") == "true";
            const bool resumed  = bus.Lua("lua-sender", "resumed") == "true";
            early               = early || (resumed && !finished);
            return resumed;
        }),
            true);
        EQUALS(early, false);

        // Where a Lua script cannot wait, an emit that would have to says so.
        EQUALS(Contains(bus.Lua("lua-sender", "select(2, pcall(Events.emit, 'slow'))"), "cannot wait here"), true);
    });

    IT("makes a Lua emitter wait for a JavaScript handler that returns a promise", {
        Bus bus;
        bus.server.files.Resource("listener", {{"main.js", "globalThis.finished = false; Events.on('slow', () => new Promise((resolve) => setTimeout(() => { finished = true; resolve(); }, 30)));"}});
        bus.server.files.Resource("sender", {{"main.lua", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"listener", "sender"}), true);

        EQUALS(bus.Run("sender", "Events.emit('slow') resumed = true"), "");
        EQUALS(bus.Lua("sender", "resumed"), "nil");
        bool early = false;
        EQUALS(bus.Until([&] {
            const bool finished = bus.Js("listener", "finished") == "true";
            const bool resumed  = bus.Lua("sender", "resumed") == "true";
            early               = early || (resumed && !finished);
            return resumed;
        }),
            true);
        EQUALS(early, false);
    });

    IT("rejects the promise of a JavaScript emitter when a Lua handler raises", {
        Bus bus;
        bus.server.files.Resource("listener", {{"main.lua", "Events.on('explode', function() error('boom') end) Events.on('explode', function() ran = true end)"}});
        bus.server.files.Resource("sender", {{"main.js", R"(
            globalThis.settled = 'pending';
            globalThis.fire = () => {
                Events.emit('explode').then(() => { settled = 'resolved'; }, (e) => {
                    settled = [e.constructor.name, e.message, e.errors.length, e.errors[0] instanceof Error, e.errors[0].message.includes('boom')].join('|');
                });
                return 'emitted';
            };
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"listener", "sender"}), true);

        EQUALS(bus.Js("sender", "fire()"), "emitted");
        EQUALS(bus.Until([&] {
            return bus.Js("sender", "settled") != "pending";
        }),
            true);
        EQUALS(bus.Js("sender", "settled"), "AggregateError|One or more event handlers failed|1|true|true");
        // The handlers after the one that failed ran all the same.
        EQUALS(bus.Lua("listener", "ran"), "true");
    });

    IT("raises in a Lua emitter what the handlers failed with", {
        Bus bus;
        bus.server.files.Resource("lua-listener", {{"main.lua", "Events.on('explode', function() error('lua boom') end) Events.on('explode', function() ran = true end) Events.on('later', function() sleep(10) error('late boom') end)"}});
        bus.server.files.Resource("js-listener", {{"main.js", "Events.on('explode', () => { throw new Error('js boom'); });"}});
        bus.server.files.Resource("sender", {{"main.lua", "Events.onLocal('mine', function() error('local boom') end)"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-listener", "js-listener", "sender"}), true);

        const std::string raised = bus.Lua("sender", "select(2, pcall(Events.emit, 'explode'))");
        EQUALS(raised.starts_with("One or more event handlers failed: "), true);
        EQUALS(Contains(raised, "lua boom"), true);
        EQUALS(Contains(raised, "js boom"), true);
        EQUALS(raised.find("lua boom") < raised.find("js boom"), true);
        EQUALS(bus.Lua("lua-listener", "ran"), "true");
        EQUALS(bus.Lua("sender", "tostring(pcall(Events.emit, 'explode'))"), "false");

        const std::string local = bus.Lua("sender", "select(2, pcall(Events.emitLocal, 'mine'))");
        EQUALS(local.starts_with("One or more local event handlers failed: "), true);
        EQUALS(Contains(local, "local boom"), true);

        // A handler that fails after it waited fails the emit that waits on it.
        EQUALS(bus.Run("sender", "local ok, err = pcall(Events.emit, 'later') outcome = tostring(ok) .. '|' .. err"), "");
        EQUALS(bus.Until([&] {
            return bus.Lua("sender", "outcome") != "nil";
        }),
            true);
        EQUALS(bus.Lua("sender", "outcome").starts_with("false|One or more event handlers failed: "), true);
        EQUALS(Contains(bus.Lua("sender", "outcome"), "late boom"), true);
    });

    // ==================== Resources that go ====================

    IT("drops the Lua handlers of a resource that stops", {
        Bus bus;
        bus.server.files.Resource("listener", {{"main.lua", "Events.on('e', function() end) Events.once('e', function() end) Events.onClient('e', function() end) Events.onLocal('e', function() end)"}});
        bus.server.files.Resource("other", {{"main.lua", "Events.on('e', function() count = (count or 0) + 1 end)"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"listener", "other"}), true);
        EQUALS(bus.Events().GetListenerCount("e"), (size_t)3);
        EQUALS(bus.Events().GetClientListenerCount("e"), (size_t)1);

        EQUALS((bool)bus.Manager().StopResource("listener"), true);
        EQUALS(bus.Events().GetListenerCount("e"), (size_t)1);
        EQUALS(bus.Events().GetClientListenerCount("e"), (size_t)0);
        EQUALS(bus.Lua("other", "Events.emit('e')"), "nil");
        EQUALS(bus.Lua("other", "count"), "1");

        // The entry point external code calls does the same for a resource that still runs.
        bus.Events().CleanupResource("other");
        EQUALS(bus.Events().GetListenerCount("e"), (size_t)0);
    });

    IT("fails the emit that waits on a handler whose resource stops", {
        Bus bus;
        bus.server.files.Resource("sleeper", {{"main.lua", "Events.on('wait', function() sleep(60000) end)"}});
        bus.server.files.Resource("js-sender", {{"main.js", "globalThis.settled = 'pending'; Events.emit('wait').then(() => { settled = 'resolved'; }, (e) => { settled = e.errors[0].message; });"}});
        bus.server.files.Resource("lua-sender", {{"main.lua", "spawn(function() local ok, err = pcall(Events.emit, 'wait') outcome = tostring(ok) .. '|' .. err end)"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"sleeper", "js-sender", "lua-sender"}), true);
        bus.server.module.Update();
        EQUALS(bus.Js("js-sender", "settled"), "pending");
        EQUALS(bus.Lua("lua-sender", "outcome"), "nil");

        EQUALS((bool)bus.Manager().StopResource("sleeper"), true);
        EQUALS(bus.Until([&] {
            return bus.Js("js-sender", "settled") != "pending" && bus.Lua("lua-sender", "outcome") != "nil";
        }),
            true);
        EQUALS(bus.Js("js-sender", "settled"), "sleeper stopped before the call finished");
        EQUALS(bus.Lua("lua-sender", "outcome"), "false|One or more event handlers failed: sleeper stopped before the call finished");
    });

    // ==================== Native events ====================

    IT("fires the onClient handlers of Lua for a client event only", {
        Bus bus;
        bus.server.files.Resource("a", {{"main.lua", "Events.onClient('c', function(x) client = x end) Events.on('c', function(x) shared = x end) Events.onceClient('once', function() onces = (onces or 0) + 1 end)"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("a"), "");

        // A script reaches the shared table only.
        EQUALS(bus.Lua("a", "Events.emit('c', 1)"), "nil");
        EQUALS(bus.Lua("a", "tostring(client) .. '|' .. tostring(shared)"), "nil|1");

        const auto emit = [&](bool client, const char *event, int value) {
            bus.Native([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Builtins::Events &events) {
                const std::vector<v8::Local<v8::Value>> args = {v8pp::to_v8(isolate, value)};
                if (client) {
                    events.EmitClient(isolate, context, event, args);
                }
                else {
                    events.EmitReserved(isolate, context, event, args);
                }
                return 0;
            });
        };
        emit(true, "c", 2);
        EQUALS(bus.Lua("a", "client .. '|' .. shared"), "2|1");
        emit(false, "c", 3);
        EQUALS(bus.Lua("a", "client .. '|' .. shared"), "2|3");

        emit(true, "once", 0);
        emit(true, "once", 0);
        EQUALS(bus.Lua("a", "onces"), "1");
        EQUALS(bus.Lua("a", "Events.offClient('c', function() end)"), "nil");
        EQUALS(bus.Events().GetClientListenerCount("c"), (size_t)1);
    });

    IT("brings a console command to a Lua handler", {
        Bus bus;
        bus.server.files.Resource("a", {{"main.lua", "Events.on('consoleCommand', function(command, args) said = command .. ':' .. table.concat(args, ',') .. ':' .. #args end)"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("a"), "");

        // As the server instance emits it: the command, then the rest of the line as an array.
        bus.Native([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Builtins::Events &events) {
            v8::Local<v8::Array> rest = v8::Array::New(isolate, 2);
            rest->Set(context, 0, v8pp::to_v8(isolate, "gold")).Check();
            rest->Set(context, 1, v8pp::to_v8(isolate, "5")).Check();
            events.EmitReserved(isolate, context, "consoleCommand", {v8pp::to_v8(isolate, "give"), rest});
            return 0;
        });
        EQUALS(bus.Lua("a", "said"), "give:gold,5:2");
    });

    IT("emits a native event to the Lua handlers of one resource", {
        Bus bus;
        bus.server.files.Resource("a", {{"main.lua", "Events.on('owned', function(x) got = x end)"}});
        bus.server.files.Resource("b", {{"main.lua", "Events.on('owned', function(x) got = x end)"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"a", "b"}), true);

        bus.Native([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Builtins::Events &events) {
            events.EmitReservedTo(isolate, context, "owned", {v8pp::to_v8(isolate, 4)}, "b");
            return 0;
        });
        EQUALS(bus.Lua("a", "got"), "nil");
        EQUALS(bus.Lua("b", "got"), "4");
    });

    // ==================== Lifecycle ====================

    IT("tells Lua handlers of resourceStart and resourceStop, a Lua-only resource of its own", {
        Bus bus;
        bus.server.files.Resource("watcher", {{"main.lua", "seen = '' Events.on('resourceStart', function(name) seen = seen .. 'start ' .. name .. ',' end) Events.on('resourceStop', function(name) seen = seen .. 'stop ' .. name .. ',' end)"}});
        bus.server.files.Resource("solo", {{"main.lua", "Events.on('resourceStart', function(name) own = (own or '') .. name end)"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"watcher", "solo"}), true);

        EQUALS(bus.server.module.GetEngine()->GetResourceRuntime("solo") == nullptr, true);
        EQUALS(bus.Lua("solo", "own"), "solo");
        EQUALS((bool)bus.Manager().StopResource("solo"), true);
        EQUALS(bus.Lua("watcher", "seen"), "start watcher,start solo,stop solo,");
    });

    IT("waits for a Lua handler of resourceStart and resourceStop that sleeps", {
        Bus bus;
        bus.server.files.Resource("watcher", {{"main.lua", R"(
            Events.on('resourceStart', function(name) if name == 'late' then sleep(40) started = name end end)
            Events.on('resourceStop', function(name) if name == 'late' then sleep(40) stopped = name end end)
        )"}});
        bus.server.files.Resource("late", {{"main.lua", "loaded = true"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("watcher"), "");

        // The handler is done by the time the transition is: a sleep counts from the last tick of its runtime, so
        // the wait itself is somewhat shorter than what the handler asked for.
        auto before = std::chrono::steady_clock::now();
        EQUALS(bus.Start("late"), "");
        EQUALS(std::chrono::steady_clock::now() - before >= std::chrono::milliseconds(20), true);
        EQUALS(bus.Lua("watcher", "started"), "late");
        EQUALS(bus.Manager().IsResourceRunning("late"), true);

        before = std::chrono::steady_clock::now();
        EQUALS((bool)bus.Manager().StopResource("late"), true);
        EQUALS(std::chrono::steady_clock::now() - before >= std::chrono::milliseconds(20), true);
        EQUALS(bus.Lua("watcher", "stopped"), "late");
    });

    IT("fails a start that a Lua handler of resourceStart refuses or holds up too long", {
        Bus bus;
        bus.server.files.Resource("watcher", {{"main.lua", R"(
            Events.on('resourceStart', function(name)
                if name == 'refused' then error('not this one') end
                if name == 'stuck' then sleep(60000) end
            end)
        )"}});
        bus.server.files.Resource("refused", {{"main.lua", ""}});
        bus.server.files.Resource("stuck", {{"main.lua", ""}});
        EQUALS(bus.Init(), true);
        auto config                   = bus.Manager().GetConfig();
        config.resourceStartTimeoutMs = 50;
        bus.Manager().SetConfig(config);
        EQUALS(bus.Start("watcher"), "");

        const std::string refused = bus.Start("refused");
        EQUALS(refused.starts_with("resourceStart rejected: "), true);
        EQUALS(Contains(refused, "One or more event handlers failed"), true);
        EQUALS(Contains(refused, "not this one"), true);
        EQUALS(bus.Manager().IsResourceRunning("refused"), false);

        EQUALS(bus.Start("stuck"), "resourceStart timed out after 50ms");
        EQUALS(bus.Manager().IsResourceRunning("stuck"), false);
    });

    IT("emits the lifecycle events of a manager that has no JavaScript engine", {
        LuaResourcesTest::Files files;
        files.Resource("watcher", {{"main.lua", R"(
            Events.on('resourceStart', function(name) if name ~= 'watcher' then sleep(30) end log('start ' .. name) if name == 'refused' then error('not this one') end if name == 'stuck' then sleep(60000) end end)
            Events.on('resourceStop', function(name) sleep(30) log('stop ' .. name) end)
        )"}});
        files.Resource("solo", {{"main.lua", "Events.on('resourceStart', function(name) log('own ' .. name) end)"}});
        files.Resource("refused", {{"main.lua", ""}});
        files.Resource("stuck", {{"main.lua", ""}});
        LuaResourcesTest::Log().clear();

        LuaEngine lua;
        EQUALS(lua.Init(), ScriptingError::SCRIPTING_NONE);
        Bind::WatchRuntimes(lua);
        ResourceManagerConfig config;
        config.resourcesPath          = files.root.string();
        config.resourceStartTimeoutMs = 400;
        ResourceManager manager(nullptr, config);
        manager.SetLuaEngine(&lua);
        lua.SetRuntimeSetupCallback([&manager](LuaRuntime &runtime) {
            Bind::SetCallResourceManager(&runtime, &manager);
            Builtins::Events::RegisterLua(runtime);
            lua_pushcfunction(runtime.State(), LuaResourcesTest::LuaLog);
            lua_setglobal(runtime.State(), "log");
        });
        lua.AddRuntimeDisposingListener([](LuaRuntime &runtime) {
            Bind::SetCallResourceManager(&runtime, nullptr);
        });
        EQUALS(manager.DiscoverResources(), 4u);

        EQUALS((bool)manager.StartResource("watcher"), true);
        EQUALS((bool)manager.StartResource("solo"), true);
        // The start came back once the handler that slept was done.
        EQUALS(LuaResourcesTest::LogText(), "start watcher,own solo,start solo");
        EQUALS((bool)manager.StopResource("solo"), true);
        EQUALS(LuaResourcesTest::LogText(), "start watcher,own solo,start solo,stop solo");

        const auto refused = manager.StartResource("refused");
        EQUALS((bool)refused, false);
        EQUALS(refused.GetError().starts_with("resourceStart rejected: One or more event handlers failed: "), true);
        EQUALS(Contains(refused.GetError(), "not this one"), true);
        EQUALS(manager.StartResource("stuck").GetError(), "resourceStart timed out after 400ms");
        EQUALS(manager.IsResourceRunning("stuck"), false);

        manager.StopAll();
    });
});
