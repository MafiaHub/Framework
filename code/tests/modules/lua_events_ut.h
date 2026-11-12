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
#include "scripting/bind/lua_callable.h"
#include "scripting/bind/v8_callable.h"
#include "scripting/builtins/events.h"
#include "scripting/builtins/exports.h"
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

        // The functions of the resource the host holds a callable of, in each language, after a full collection.
        size_t JsCallables(const std::string &resource) {
            v8::Isolate *isolate = server.module.GetEngine()->GetResourceRuntime(resource)->GetIsolate();
            {
                v8::Locker locker(isolate);
                v8::Isolate::Scope isolateScope(isolate);
                isolate->LowMemoryNotification();
            }
            return Bind::V8CallableCount(isolate);
        }

        size_t LuaCallables(const std::string &resource) {
            LuaRuntime *runtime = server.module.GetLuaEngine()->GetRuntime(resource);
            lua_gc(runtime->State(), LUA_GCCOLLECT);
            return Bind::LuaCallableCount(*runtime);
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

    // The rig of the Lua resources tests, which gives the scripts of both languages stop(name), with Events in its
    // Lua runtimes and the callables of both engines told of a runtime that goes.
    struct StopRig final {
        LuaResourcesTest::Rig rig;

        StopRig() {
            Bind::WatchRuntimes(rig.node);
            Bind::WatchRuntimes(rig.lua);
            rig.lua.SetRuntimeSetupCallback([this](LuaRuntime &runtime) {
                lua_State *L = runtime.State();
                lua_pushcfunction(L, LuaResourcesTest::LuaLog);
                lua_setglobal(L, "log");
                lua_pushlightuserdata(L, &rig);
                lua_pushcclosure(L, LuaResourcesTest::Rig::Stop, 1);
                lua_setglobal(L, "stop");
                Bind::SetCallResourceManager(&runtime, &rig.manager);
                Builtins::Events::RegisterLua(runtime);
                Builtins::Exports::RegisterLua(runtime);
            });
            // Each runtime that is destroyed says so in the log, next to what the scripts wrote.
            rig.lua.AddRuntimeDisposingListener([](LuaRuntime &runtime) {
                LuaResourcesTest::Log().push_back("lua runtime of " + runtime.Options().resourceName + " destroyed");
                Bind::SetCallResourceManager(&runtime, nullptr);
            });
            rig.node.AddRuntimeDisposingListener([](v8::Isolate *) {
                LuaResourcesTest::Log().emplace_back("javascript runtime destroyed");
            });
        }

        // What the scripts logged, without the runtimes that were destroyed.
        static std::string Said() {
            std::string text;
            for (const std::string &entry : LuaResourcesTest::Log()) {
                if (!entry.ends_with(" destroyed")) {
                    text += (text.empty() ? "" : ",") + entry;
                }
            }
            return text;
        }

        // The value of `expression` in the JavaScript runtime of the resource, as a string.
        std::string Js(const std::string &resource, const std::string &expression) {
            NodeRuntime *runtime = rig.node.GetResourceRuntime(resource);
            return runtime != nullptr ? NodeTest::Eval(*runtime, expression) : "<no runtime>";
        }
    };

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

    IT("removes with the function on returned a handler that was registered again since", {
        Bus bus;
        bus.server.files.Resource("toggle", {{"main.lua", R"(
            function toggle()
                local function handler() end
                local off = Events.on('lua-toggle', handler)
                off()
                Events.on('lua-toggle', handler)
                local between = Events.listenerCount('lua-toggle')
                off()
                -- Every registration of the function goes, and a function that is not registered removes nothing.
                Events.on('lua-toggle', handler)
                Events.on('lua-toggle', handler)
                local doubled = Events.listenerCount('lua-toggle')
                off()
                off()
                return between .. ',' .. doubled .. ',' .. Events.listenerCount('lua-toggle')
            end
        )"},
                                                {"main.js", R"(
            globalThis.toggle = () => {
                const handler = () => {};
                const off = Events.on('js-toggle', handler);
                off();
                Events.on('js-toggle', handler);
                const between = Events.listenerCount('js-toggle');
                off();
                Events.on('js-toggle', handler);
                Events.on('js-toggle', handler);
                const doubled = Events.listenerCount('js-toggle');
                off();
                off();
                // The client table has its own: the function on returned removes nothing from it.
                const offClient = Events.onClient('js-toggle', handler);
                off();
                const client = Events.listenerCount('js-toggle');
                offClient();
                return [between, doubled, Events.listenerCount('js-toggle'), client].join(',');
            };
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("toggle"), "");

        EQUALS(bus.Lua("toggle", "toggle()"), "1,2,0");
        EQUALS(bus.Js("toggle", "toggle()"), "1,2,0,0");
        EQUALS(bus.Events().GetClientListenerCount("js-toggle"), (size_t)0);
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
        EQUALS(Contains(bus.Lua("a", "try(Events.emitLocal, 'e', coroutine.create(function() end))"), "Events.emitLocal: argument 1: "), true);
    });

    IT("uses up no once handler for an emit whose arguments cannot be copied", {
        Bus bus;
        bus.server.files.Resource("a", {{"main.lua", "calls = 0 Events.once('e', function() calls = calls + 1 end)"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("a"), "");

        EQUALS(bus.Lua("a", "tostring(pcall(Events.emit, 'e', coroutine.create(function() end)))"), "false");
        EQUALS(bus.Lua("a", "tostring(pcall(Events.emitTo, 'a', 'e', coroutine.create(function() end)))"), "false");
        EQUALS(bus.Lua("a", "calls .. ',' .. Events.listenerCount('e')"), "0,1");
        EQUALS(bus.Lua("a", "Events.emit('e')"), "nil");
        EQUALS(bus.Lua("a", "calls .. ',' .. Events.listenerCount('e')"), "1,0");
    });

    IT("names the argument a JavaScript emit cannot copy for a Lua handler, counting from one", {
        Bus bus;
        bus.server.files.Resource("listener", {{"main.lua", "Events.on('e', function() called = true end) Events.on('e', function() called = true end)"}});
        bus.server.files.Resource("sender", {{"main.js", R"(
            globalThis.settled = 'pending';
            Events.on('e', (a, b) => { globalThis.own = typeof b; });
            globalThis.fire = () => {
                Events.emit('e', 1, Symbol('s')).then(() => { settled = 'resolved'; }, (e) => { settled = e.errors.length + '|' + e.errors[0].message.startsWith('Events.emit: argument 2: ') + '|' + (e.errors[0].message === e.errors[1].message); });
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
        // The handler of the emitter's own isolate got the value itself; each Lua handler failed, and none ran.
        EQUALS(bus.Js("sender", "settled"), "2|true|true");
        EQUALS(bus.Js("sender", "own"), "symbol");
        EQUALS(bus.Lua("listener", "called"), "nil");
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

    // ==================== Handlers that change what is being dispatched ====================

    IT("lets go of a handler that unsubscribed itself, in both languages", {
        Bus bus;
        bus.server.files.Resource("churn", {{"main.lua", R"(
            alive = setmetatable({}, {__mode = 'k'})
            function churn(n)
                for i = 1, n do
                    local off
                    local function handler() off() end
                    alive[handler] = true
                    off = Events.on('lua-churn', handler)
                    if i % 2 == 0 then off() else Events.emit('lua-churn') end
                end
            end
            function survivors() local n = 0 for _ in pairs(alive) do n = n + 1 end return n end
        )"},
                                               {"main.js", R"(
            globalThis.churn = (n) => {
                for (let i = 0; i < n; i++) {
                    const off = Events.on('js-churn', () => { off(); });
                    if (i % 2 === 0) { off(); } else { Events.emit('js-churn'); }
                }
            };
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("churn"), "");
        const size_t luaBefore = bus.LuaCallables("churn");
        const size_t jsBefore  = bus.JsCallables("churn");

        EQUALS(bus.Lua("churn", "churn(200)"), "nil");
        EQUALS(bus.Events().GetListenerCount("lua-churn"), (size_t)0);
        EQUALS(bus.LuaCallables("churn"), luaBefore);
        // The functions themselves went with their callables: nothing roots a handler that holds its own
        // unsubscribe function.
        EQUALS(bus.Lua("churn", "survivors()"), "0");

        EQUALS(bus.Js("churn", "churn(200); 0"), "0");
        EQUALS(bus.Events().GetListenerCount("js-churn"), (size_t)0);
        EQUALS(bus.JsCallables("churn"), jsBefore);
    });

    IT("calls every handler picked for an emission, also one an earlier handler removed", {
        Bus bus;
        bus.server.files.Resource("lua", {{"main.lua", R"(
            seen = ''
            local offSelf
            offSelf = Events.on('self', function() seen = seen .. 's' offSelf() end)
            local function second() seen = seen .. '2' end
            Events.on('other', function() seen = seen .. '1' Events.off('other', second) end)
            Events.on('other', second)
            Events.on('again-lua', function(n) seen = seen .. n if n < 3 then Events.emit('again-lua', n + 1) end seen = seen .. 'r' end)
        )"}});
        bus.server.files.Resource("js", {{"main.js", R"(
            globalThis.seen = '';
            const offSelf = Events.on('self', () => { seen += 's'; offSelf(); });
            const second = () => { seen += '2'; };
            Events.on('other', () => { seen += '1'; Events.off('other', second); });
            Events.on('other', second);
            Events.on('again-js', (n) => { seen += n; if (n < 3) { Events.emit('again-js', n + 1); } seen += 'r'; });
        )"}});
        bus.server.files.Resource("driver", {{"main.lua", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua", "js", "driver"}), true);

        // A handler that unsubscribes itself is called once.
        EQUALS(bus.Lua("driver", "Events.emit('self')"), "nil");
        EQUALS(bus.Lua("driver", "Events.emit('self')"), "nil");
        EQUALS(bus.Lua("lua", "seen"), "s");
        EQUALS(bus.Js("js", "seen"), "s");
        EQUALS(bus.Events().GetListenerCount("self"), (size_t)0);

        // One removed while the emission runs was picked already, and is called this once.
        EQUALS(bus.Lua("driver", "Events.emit('other')"), "nil");
        EQUALS(bus.Lua("driver", "Events.emit('other')"), "nil");
        EQUALS(bus.Lua("lua", "seen"), "s121");
        EQUALS(bus.Js("js", "seen"), "s121");

        // A handler that emits its own event is entered again, inside itself, whichever language emits first.
        EQUALS(bus.Lua("lua", "(function() seen = '' end)()"), "nil");
        EQUALS(bus.Js("js", "seen = ''"), "");
        EQUALS(bus.Lua("driver", "Events.emit('again-lua', 1)"), "nil");
        EQUALS(bus.Lua("driver", "Events.emit('again-js', 1)"), "nil");
        EQUALS(bus.Lua("lua", "seen"), "123rrr");
        EQUALS(bus.Js("js", "seen"), "123rrr");
        EQUALS(bus.Js("js", "seen = ''; Events.emit('again-js', 2); seen"), "23rr");
    });

    IT("skips the handlers of a resource that a handler of the same emission stopped", {
        LuaEventsTest::StopRig stops;
        auto &rig = stops.rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("lua-self", {{"main.lua", "Events.on('halt-lua', function() log('lua first') stop('lua-self') log('lua after') end) Events.on('halt-lua', function() log('lua second') end)"}});
        rig.files.Resource("js-self", {{"main.js", "Events.on('halt-js', () => { log('js first'); stop('js-self'); log('js after'); }); Events.on('halt-js', () => { log('js second'); });"}});
        rig.files.Resource("killer", {{"main.lua", "Events.on('kill', function() log('killer') stop('victim') end)"}, {"main.js", "Events.on('kill-js', () => { log('js killer'); stop('victim'); });"}});
        rig.files.Resource("victim", {{"main.lua", "Events.on('kill', function() log('victim') end) Events.on('kill-js', function() log('victim') end)"}, {"main.js", "Events.on('kill', () => { log('js victim'); }); Events.on('kill-js', () => { log('js victim'); });"}});
        rig.files.Resource("driver", {{"main.lua", "function try(...) local ok, err = pcall(Events.emit, ...) return ok and 'ok' or err end"}, {"main.js", "globalThis.settled = 'none'; globalThis.fire = (name) => { settled = 'pending'; Events.emit(name).then(() => { settled = 'resolved'; }, (e) => { settled = e.errors.map((x) => x.message).join(';'); }); return 0; };"}});
        EQUALS(rig.Start("lua-self"), "");
        EQUALS(rig.Start("js-self"), "");
        EQUALS(rig.Start("killer"), "");
        EQUALS(rig.Start("victim"), "");
        EQUALS(rig.Start("driver"), "");
        LuaResourcesTest::Log().clear();

        // A Lua handler that stops its own resource runs on to its end; the emit that waited on it is told that
        // the resource went, and the handler after it, of a runtime that is gone, is not called.
        EQUALS(rig.Eval("driver", "try('halt-lua')"), "ok");
        EQUALS(stops.Said(), "lua first,lua after");
        EQUALS(rig.manager.IsResourceRunning("lua-self"), false);
        EQUALS(rig.manager.GetEvents().GetListenerCount("halt-lua"), (size_t)0);
        rig.Advance(1);
        EQUALS(rig.HasLuaRuntime("lua-self"), false);

        // The same from a JavaScript handler, called by a Lua emitter.
        LuaResourcesTest::Log().clear();
        EQUALS(rig.Eval("driver", "try('halt-js')"), "ok");
        EQUALS(stops.Said(), "js first,js after");
        EQUALS(rig.manager.IsResourceRunning("js-self"), false);
        EQUALS(rig.manager.GetEvents().GetListenerCount("halt-js"), (size_t)0);

        // A handler that stops another resource: the handlers of that one, of either language, are skipped,
        // and nothing fails. From a Lua emitter, then from a JavaScript one.
        LuaResourcesTest::Log().clear();
        EQUALS(rig.Eval("driver", "try('kill')"), "ok");
        EQUALS(stops.Said(), "killer");
        EQUALS(rig.manager.IsResourceRunning("victim"), false);

        EQUALS(rig.Start("victim"), "");
        LuaResourcesTest::Log().clear();
        EQUALS(stops.Js("driver", "fire('kill-js')"), "0");
        for (int i = 0; i < 8 && stops.Js("driver", "settled") == "pending"; ++i) {
            rig.node.Tick();
        }
        EQUALS(stops.Js("driver", "settled"), "resolved");
        // Between JavaScript runtimes it is as it always was: the isolate of the stopped resource lives on while
        // JavaScript is running, and its handler, picked already, is still called. Its Lua handler is not.
        EQUALS(stops.Said(), "js killer,js victim");
        EQUALS(rig.manager.IsResourceRunning("victim"), false);
        rig.Advance(1);
    });

    IT("calls none of the JavaScript handlers picked after one that cleared the bus", {
        Bus bus;
        bus.server.files.Resource("a", {{"main.js", "globalThis.seen = ''; Events.on('wipe', () => { seen += '1'; clearAll(); }); Events.on('wipe', () => { seen += '2'; });"}});
        bus.server.files.Resource("b", {{"main.js", "globalThis.seen = ''; Events.on('wipe', () => { seen += 'b'; });"}, {"main.lua", "Events.on('wipe', function() seen = 'lua' end)"}});
        bus.server.files.Resource("emitter", {{"main.js", "globalThis.settled = 'pending'; globalThis.fire = () => { Events.emit('wipe').then(() => { settled = 'resolved'; }, () => { settled = 'rejected'; }); return 0; };"}});
        EQUALS(bus.Init(), true);
        // A host function that clears every handler, as a project's session reset does, reachable from a script.
        bus.server.module.GetEngine()->SetRuntimeSetupCallback([&bus](Engine *engine) {
            bus.server.module.RegisterFrameworkBindings();
            engine->InitFrameworkSDK();
            EngineScope scope(engine);
            v8::Isolate *isolate           = scope.Isolate();
            v8::Local<v8::Context> context = scope.Context();
            const auto clear               = [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                static_cast<Builtins::Events *>(info.Data().As<v8::External>()->Value())->ClearAll();
            };
            context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "clearAll"), v8::Function::New(context, clear, v8::External::New(isolate, &bus.Events())).ToLocalChecked()).Check();
        });
        EQUALS(bus.StartAll({"emitter", "a", "b"}), true);

        EQUALS(bus.Js("emitter", "fire()"), "0");
        EQUALS(bus.Until([&] {
            return bus.Js("emitter", "settled") != "pending";
        }),
            true);
        // The handlers that had been picked lost their functions with the tables: none of them is called, in the
        // isolate of the one that cleared or in another, and the emission settles. The Lua handler, which has
        // no such function, is called through its callable as picked.
        EQUALS(bus.Js("emitter", "settled"), "resolved");
        EQUALS(bus.Js("a", "seen"), "1");
        EQUALS(bus.Js("b", "seen"), "");
        EQUALS(bus.Lua("b", "seen"), "lua");
        EQUALS(bus.Events().GetListenerCount("wipe"), (size_t)0);
    });

    IT("keeps a runtime that is stopped under a script of the other language until the next tick", {
        LuaEventsTest::StopRig stops;
        auto &rig = stops.rig;
        EQUALS(rig.ready, true);
        rig.files.Resource("js-provider", {{"main.js", "Exports.register('quit', () => { stop('js-provider'); return 'bye'; }); Exports.register('add', (a, b) => a + b);"}}, R"(,"exports":["quit","add"])");
        rig.files.Resource("js-victim", {{"main.js", "Exports.register('add', (a, b) => a + b);"}}, R"(,"exports":["add"])");
        rig.files.Resource("lua-provider", {{"main.lua", "Exports.register('quit', function() stop('lua-provider') return 'bye' end)"}}, R"(,"exports":["quit"])");
        rig.files.Resource("lua-victim", {{"main.lua", "Exports.register('add', function(a, b) return a + b end)"}}, R"(,"exports":["add"])");
        rig.files.Resource("lua-caller", {{"main.lua", R"(
            function try(f, ...) local ok, result = pcall(f, ...) return tostring(ok) .. ':' .. tostring(result) end
            function quit() local f = Exports.get('js-provider', 'quit') local said = try(f) log('lua went on') return said end
            function kill() local add = Exports.get('js-victim', 'add') local before = add(1, 2) stop('js-victim') local said = before .. '|' .. try(add, 1, 2) log('lua went on') return said end
        )"}});
        rig.files.Resource("js-caller", {{"main.js", R"(
            const attempt = (f, ...args) => { try { return 'true:' + f(...args); } catch (e) { return 'false:' + e.message; } };
            globalThis.quit = () => { const f = Exports.get('lua-provider', 'quit'); const said = attempt(f); log('js went on'); return said; };
            globalThis.kill = () => { const add = Exports.get('lua-victim', 'add'); const before = add(1, 2); stop('lua-victim'); const said = before + '|' + attempt(add, 1, 2); log('js went on'); return said; };
        )"}});
        for (const char *resource : {"js-provider", "js-victim", "lua-provider", "lua-victim", "lua-caller", "js-caller"}) {
            EQUALS(rig.Start(resource), "");
        }

        std::string said;

        // A JavaScript export that stops its own resource, called from Lua: the caller gets its answer, and the
        // runtime goes at the next tick of its engine.
        LuaResourcesTest::Log().clear();
        said = rig.Eval("lua-caller", "quit()");
        EQUALS(said, "true:bye");
        EQUALS(LuaResourcesTest::LogText(), "lua went on");
        rig.node.Tick();
        EQUALS(LuaResourcesTest::LogText(), "lua went on,javascript runtime destroyed");

        // A Lua script that stops a JavaScript resource whose function it holds: no isolate is entered, and the
        // runtime still outlives the script. The function answers that its resource stopped.
        LuaResourcesTest::Log().clear();
        said = rig.Eval("lua-caller", "kill()");
        EQUALS(said, "3|false:runtime stopped");
        EQUALS(LuaResourcesTest::LogText(), "lua went on");
        rig.node.Tick();
        EQUALS(LuaResourcesTest::LogText(), "lua went on,javascript runtime destroyed");

        // The mirror: a Lua export that stops its own resource, called from JavaScript.
        LuaResourcesTest::Log().clear();
        said = stops.Js("js-caller", "quit()");
        EQUALS(said, "true:bye");
        EQUALS(LuaResourcesTest::LogText(), "js went on");
        rig.Advance(1);
        EQUALS(LuaResourcesTest::LogText(), "js went on,lua runtime of lua-provider destroyed");

        // And JavaScript that stops a Lua resource whose function it holds.
        LuaResourcesTest::Log().clear();
        said = stops.Js("js-caller", "kill()");
        EQUALS(said, "3|false:runtime stopped");
        EQUALS(LuaResourcesTest::LogText(), "js went on");
        rig.Advance(1);
        EQUALS(LuaResourcesTest::LogText(), "js went on,lua runtime of lua-victim destroyed");
    });

    IT("drops an emission whose emitter stops while a handler is still at it", {
        Bus bus;
        bus.server.files.Resource("listener", {{"main.lua", "finished = 0 Events.on('slow', function() sleep(30) finished = finished + 1 end)"}});
        bus.server.files.Resource("lua-sender", {{"main.lua", "spawn(function() Events.emit('slow') resumed = true end)"}});
        bus.server.files.Resource("js-sender", {{"main.js", "Events.emit('slow').then(() => { globalThis.settled = true; });"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("listener"), "");
        const size_t callables = bus.LuaCallables("listener");
        EQUALS(bus.StartAll({"lua-sender", "js-sender"}), true);
        bus.server.module.Update();
        EQUALS(bus.Lua("listener", "finished"), "0");
        EQUALS(Builtins::Events::GetWaitingEmissionCount(), (size_t)1);

        // The Lua emitter waits in a runtime that goes, the JavaScript one holds a promise in an isolate that
        // goes. The handlers run on to their end, with nobody to tell.
        EQUALS((bool)bus.Manager().StopResource("lua-sender"), true);
        EQUALS((bool)bus.Manager().StopResource("js-sender"), true);
        EQUALS(Builtins::Events::GetWaitingEmissionCount(), (size_t)0);
        EQUALS(bus.Until([&] {
            return bus.Lua("listener", "finished") == "2";
        }),
            true);
        bus.server.module.Update();
        EQUALS(Builtins::Events::GetWaitingEmissionCount(), (size_t)0);
        EQUALS(bus.LuaCallables("listener"), callables);
        EQUALS(bus.Events().GetListenerCount("slow"), (size_t)1);

        // The bus still works for who is left.
        EQUALS(bus.Run("listener", "Events.emit('slow') again = true"), "");
        EQUALS(bus.Until([&] {
            return bus.Lua("listener", "again") == "true";
        }),
            true);
    });

    IT("forgets the handlers and the emissions of an isolate its engine disposes of", {
        LuaResourcesTest::Files files;
        {
            NodeEngine engine({});
            EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
            ResourceManagerConfig config;
            config.resourcesPath = files.root.string();
            ResourceManager manager(&engine, config);
            TestManagerHelper::RegisterEvents(engine, manager);
            manager.SetCurrentResourceContext("testResource");

            // Handlers of the engine's own isolate, which no runtime listener speaks for, and an emission that
            // waits on one of them.
            EQUALS(TestManagerHelper::EvalInt(engine, "Events.on('kept', () => new Promise(() => {})); Events.onClient('kept', () => {}); Events.onLocal('kept', () => {}); Events.emit('kept'); Events.listenerCount('kept')"), 1);
            EQUALS(Builtins::Events::GetWaitingEmissionCount(), (size_t)1);
            manager.SetCurrentResourceContext("");

            // Nothing cleaned the resource up: the engine goes with the handlers still registered.
            engine.Shutdown();
            EQUALS(manager.GetEvents().GetListenerCount("kept"), (size_t)0);
            EQUALS(manager.GetEvents().GetClientListenerCount("kept"), (size_t)0);
            EQUALS(Builtins::Events::GetWaitingEmissionCount(), (size_t)0);
            // No handle of the dead isolate is left for these to touch.
            manager.GetEvents().ClearAll();
            manager.GetEvents().Reset();
        }
    });

    IT("stops telling an emission with copied arguments that was cancelled", {
        Bus bus;
        bus.server.files.Resource("listener", {{"main.lua", "finished = 0 Events.on('slow', function() sleep(20) finished = finished + 1 end)"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("listener"), "");

        int told             = 0;
        const auto cancelled = bus.Events().EmitReserved("slow", {}, [&told](std::vector<std::string> &&) {
            told += 1;
        });
        const auto awaited = bus.Events().EmitReserved("slow", {}, [&told](std::vector<std::string> &&errors) {
            told += errors.empty() ? 10 : 100;
        });
        EQUALS(cancelled != nullptr && awaited != nullptr, true);
        Builtins::Events::CancelPendingEmission(cancelled);
        EQUALS(bus.Until([&] {
            return bus.Lua("listener", "finished") == "2";
        }),
            true);
        EQUALS(told, 10);

        // One that settled as it was made has nothing to cancel.
        EQUALS(bus.Events().EmitReserved("nobody", {}, [&told](std::vector<std::string> &&) {
            told += 1000;
        }) == nullptr,
            true);
        EQUALS(told, 1010);
        Builtins::Events::CancelPendingEmission(nullptr);
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
