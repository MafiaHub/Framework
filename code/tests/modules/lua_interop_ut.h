/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_events_ut.h"
#include "scripting_catalog_golden_ut.h"

#include "integrations/server/scripting/module.h"
#include "scripting/lua/lua_engine.h"
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// JavaScript and Lua resources side by side on a real server scripting module, from files on disk: what one
// language sends the other, and what the module owes both.
namespace LuaInteropTest {
    using namespace Framework::Scripting;
    using Framework::Integrations::Server::Scripting::ServerScriptingModule;

    // Globals a server gives JavaScript that Lua does not have under the same name, each with its reason. Nothing
    // else may be missing from a Lua state.
    inline const std::set<std::string> &JavaScriptOnly() {
        static const std::set<std::string> names = {
            // The microtask queue is JavaScript's own: it orders callbacks between the turns of a Promise. Lua has
            // no promises, a coroutine that waits is resumed by the scheduler, and setTimeout(f, 0) defers a call.
            "queueMicrotask",
            // In the catalog for its type only. No script of either language names it: a bag is what
            // `entity.state` answers, in Lua as in JavaScript.
            "StateBag",
        };
        return names;
    }

    inline std::set<std::string> Split(const std::string &text) {
        std::set<std::string> names;
        std::stringstream stream(text);
        for (std::string name; std::getline(stream, name, ',');) {
            if (!name.empty()) {
                names.insert(name);
            }
        }
        return names;
    }

    inline std::string Join(const std::set<std::string> &names) {
        std::string text;
        for (const std::string &name : names) {
            text += (text.empty() ? "" : ",") + name;
        }
        return text;
    }

    inline const char *kGlobalNames = "Object.getOwnPropertyNames(globalThis).join(',')";

    // The globals of a resource runtime of a Node engine set up as the server's is, with no binding of the
    // framework in it.
    inline std::set<std::string> EngineGlobals() {
        NodeEngineOptions options;
        options.sandboxed   = false;
        options.processName = "mafiahub-server";
        NodeEngine engine(options);
        std::set<std::string> names;
        if (engine.Init() == ScriptingError::SCRIPTING_NONE && engine.CreateResourceRuntime("bare")) {
            names = Split(NodeTest::Eval(*engine.GetResourceRuntime("bare"), kGlobalNames));
            engine.DestroyResourceRuntime("bare");
        }
        engine.Shutdown();
        return names;
    }

    // The names a script reaches as globals according to the catalog the declarations are generated from: the
    // global objects, the classes and the global functions.
    inline std::set<std::string> CatalogGlobals() {
        std::set<std::string> names;
        const nlohmann::json catalog = ScriptingCatalogGolden::ReadFixture();
        if (!catalog.is_object()) {
            return names;
        }
        for (const nlohmann::json &symbol : catalog["symbols"]) {
            const std::string kind = symbol.value("kind", std::string());
            if (kind == "globalObject" || kind == "constructor") {
                names.insert(symbol.value("name", std::string()));
            }
        }
        for (const nlohmann::json &function : catalog["functions"]) {
            names.insert(function.value("name", std::string()));
        }
        return names;
    }

    // How many of `lines` have `part` in them.
    inline size_t Count(const std::vector<std::string> &lines, std::string_view part) {
        return static_cast<size_t>(std::count_if(lines.begin(), lines.end(), [part](const std::string &line) {
            return line.find(part) != std::string::npos;
        }));
    }

    inline bool Has(const std::vector<std::string> &lines, std::string_view part) {
        return Count(lines, part) != 0;
    }

    // The first of `lines` with `part` in it, or "".
    inline std::string Find(const std::vector<std::string> &lines, std::string_view part) {
        for (const std::string &line : lines) {
            if (line.find(part) != std::string::npos) {
                return line;
            }
        }
        return {};
    }

    // Where `part` is in the first of `lines` that has it, by that line's own count, or npos.
    inline size_t IndexOf(const std::vector<std::string> &lines, std::string_view part) {
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].find(part) != std::string::npos) {
                return i;
            }
        }
        return std::string::npos;
    }

    // Whether a log entry has a character that starts a line of its own or drives a terminal, other than the
    // line ends the log puts between the lines of one entry.
    inline bool HasControlCharacter(const std::string &entry) {
        return std::any_of(entry.begin(), entry.end(), [](char character) {
            const auto byte = static_cast<unsigned char>(character);
            return (byte < 0x20 && byte != '\t' && byte != '\n') || byte == 0x7F;
        });
    }

    // Whether every line of a log entry is under the name of the resource.
    inline bool EveryLineNames(const std::string &entry, const std::string &resource) {
        const std::string prefix = "[" + resource + "] ";
        std::stringstream stream(entry);
        for (std::string line; std::getline(stream, line);) {
            if (line.rfind(prefix, 0) != 0) {
                return false;
            }
        }
        return true;
    }
} // namespace LuaInteropTest

MODULE(lua_interop, {
    using namespace Framework::Scripting;
    using Framework::Integrations::Server::Scripting::ServerScriptingModule;
    using LuaEventsTest::Bus;
    using LuaEventsTest::Contains;
    using LuaInteropTest::Count;
    using LuaInteropTest::Find;
    using LuaInteropTest::Has;
    using LuaRuntimeTest::LogCapture;

    // ==================== Events ====================

    IT("exchanges an event with a table payload between a JavaScript and a Lua resource, both ways", {
        Bus bus;
        bus.server.files.Resource("js-side", {{"main.js", R"(
            globalThis.got = 'nothing';
            Events.on('toJs', (payload) => {
                globalThis.got = [payload.from, Array.isArray(payload.list), payload.list.length, payload.list[2], payload.nested.ok, payload.nested.origin instanceof Vector3].join('|');
                Events.emit('toLua', { from: 'js', list: [4, 5], nested: { ok: true, origin: new Vector3(1, 2, 3) } });
            });
        )"}});
        bus.server.files.Resource("lua-side", {{"main.lua", R"(
            got = 'nothing'
            Events.on('toLua', function(payload)
                got = table.concat({ payload.from, #payload.list, payload.list[2], tostring(payload.nested.ok), payload.nested.origin.z == 3 and 'vector' or 'other' }, '|')
            end)
            Events.emit('toJs', { from = 'lua', list = { 1, 2, 3 }, nested = { ok = true, origin = Vector3.new(1, 2, 3) } })
        )"}});
        EQUALS(bus.Init(), true);
        // Nothing but the two files: the Lua one emits as it loads, and the JavaScript handler answers.
        EQUALS(bus.StartAll({"js-side", "lua-side"}), true);

        EQUALS(bus.Js("js-side", "got"), "lua|true|3|3|true|true");
        EQUALS(bus.Lua("lua-side", "got"), "js|2|5|true|vector");
    });

    IT("brings what a Lua file emits to the JavaScript file of the same resource, and the answer back", {
        Bus bus;
        const char *first  = R"(
            globalThis.heard = 'nothing';
            Events.onLocal('order', (order) => {
                globalThis.heard = order.item + ' x' + order.amounts.length;
                Events.emitLocal('receipt', { item: order.item, total: order.amounts.reduce((sum, amount) => sum + amount, 0) });
            });
        )";
        const char *second = R"(
            receipt = 'nothing'
            Events.onLocal('receipt', function(r) receipt = r.item .. ' ' .. r.total end)
            Events.emitLocal('order', { item = 'bread', amounts = { 2, 3 } })
        )";
        bus.server.files.Resource("mixed", {{"first.js", first}, {"second.lua", second}});
        bus.server.files.Resource("other", {{"main.js", "globalThis.heard = 'nothing'; Events.onLocal('order', () => { globalThis.heard = 'leaked'; });"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"other", "mixed"}), true);

        EQUALS(bus.Js("mixed", "heard"), "bread x2");
        EQUALS(bus.Lua("mixed", "receipt"), "bread 5");
        // One resource, two runtimes: it stayed inside the resource.
        EQUALS(bus.Js("other", "heard"), "nothing");
    });

    // ==================== Exports ====================

    IT("lets a Lua resource import the async export of a JavaScript resource", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.js", R"(
            Exports.register('lookup', async (query) => {
                await new Promise((resolve) => setTimeout(resolve, 20));
                return { name: query.name.toUpperCase(), tags: [...query.tags, 'found'] };
            });
        )"}},
            R"(,"exports":["lookup"])");
        bus.server.files.Resource("consumer", {{"main.lua", R"(
            state = 'loading'
            setTimeout(function()
                state = 'waiting'
                local found = Imports.get('provider').lookup({ name = 'ada', tags = { 'a' } })
                state = found.name .. '|' .. table.concat(found.tags, ',')
            end, 0)
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "consumer"}), true);

        EQUALS(bus.Until([&] {
            return bus.Lua("consumer", "state") == "waiting";
        }),
            true);
        EQUALS(bus.Until([&] {
            return bus.Lua("consumer", "state") != "waiting";
        }),
            true);
        EQUALS(bus.Lua("consumer", "state"), "ADA|a,found");
    });

    IT("lets a JavaScript resource import the export of a Lua resource, one that waits included", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", R"(
            Exports.register('describe', function(item) return { label = item.name .. ' x' .. #item.sizes, origin = Vector3.new(1, 2, 3) } end)
            Exports.register('slow', function(n) sleep(20) return n * 2 end)
        )"}},
            R"(,"exports":["describe","slow"])");
        bus.server.files.Resource("consumer", {{"main.js", R"(
            const provider = Imports.get('provider');
            const described = provider.describe({ name: 'coat', sizes: [1, 2, 3] });
            globalThis.described = described.label + '|' + (described.origin instanceof Vector3) + '|' + described.origin.y;
            globalThis.slow = 'pending';
            provider.slow(21).then((value) => { globalThis.slow = 'resolved ' + value; }, (error) => { globalThis.slow = 'rejected ' + error.message; });
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "consumer"}), true);

        EQUALS(bus.Js("consumer", "described"), "coat x3|true|2");
        EQUALS(bus.Js("consumer", "slow"), "pending");
        EQUALS(bus.Until([&] {
            return bus.Js("consumer", "slow") != "pending";
        }),
            true);
        EQUALS(bus.Js("consumer", "slow"), "resolved 42");
    });

    // ==================== Stopping ====================

    IT("waits for the resourceStop handler of a Lua resource before the resource is gone", {
        Bus bus;
        bus.server.files.Resource("watcher", {{"main.js", "globalThis.farewell = 'none'; Events.on('farewell', (note) => { globalThis.farewell = note.from + '|' + note.slept; });"}});
        bus.server.files.Resource("sleeper", {{"main.lua", R"(
            Events.on('resourceStop', function(name)
                if name == 'sleeper' then
                    sleep(40)
                    Events.emit('farewell', { from = name, slept = true })
                end
            end)
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"watcher", "sleeper"}), true);

        const auto before = std::chrono::steady_clock::now();
        EQUALS((bool)bus.Manager().StopResource("sleeper"), true);
        const auto took = std::chrono::steady_clock::now() - before;

        // The stop came back once the handler had slept and said what it had to say, not before.
        EQUALS(bus.Js("watcher", "farewell"), "sleeper|true");
        EQUALS(took >= std::chrono::milliseconds(40), true);
        EQUALS(bus.server.module.GetLuaEngine()->GetRuntime("sleeper") == nullptr, true);
        EQUALS(bus.Manager().IsResourceRunning("sleeper"), false);
    });

    IT("shuts down with resources of both languages at work: the manager, then Lua, then Node", {
        LogCapture log;
        // What each Lua runtime found standing when it was closed.
        std::vector<std::string> disposals;
        {
            Bus bus;
            bus.server.files.Resource("js-worker", {{"main.js", R"(
                Exports.register('never', () => new Promise(() => {}));
                setInterval(() => {}, 5);
                Events.on('resourceStop', (name) => { if (name === 'js-worker') console.log('javascript handled its stop'); });
                globalThis.callLua = () => { Imports.get('lua-worker').slow().then(() => {}, () => {}); };
            )"}},
                R"(,"exports":["never"])");
            bus.server.files.Resource("lua-worker", {{"main.lua", R"(
                Exports.register('slow', function() sleep(60000) end)
                setInterval(function() end, 5)
                setTimeout(function() state = 'waiting' Imports.get('js-worker').never() end, 0)
                Events.on('resourceStop', function(name) if name == 'lua-worker' then sleep(10) print('lua handled its stop') end end)
            )"}},
                R"(,"exports":["slow"])");
            EQUALS(bus.Init(), true);
            EQUALS(bus.StartAll({"js-worker", "lua-worker"}), true);

            // A Lua coroutine waits on a promise of JavaScript that never settles, and JavaScript on a Lua export
            // that sleeps for a minute.
            EQUALS(bus.Until([&] {
                return bus.Lua("lua-worker", "state") == "waiting";
            }),
                true);
            EQUALS(bus.Js("js-worker", "callLua(); 'called'"), "called");
            bus.server.module.Update();

            ServerScriptingModule &module = bus.server.module;
            module.GetLuaEngine()->AddRuntimeDisposingListener([&disposals, &module](LuaRuntime &runtime) {
                const bool lua  = module.GetLuaEngine() != nullptr && module.GetLuaEngine()->IsInitialized();
                const bool node = module.GetEngine() != nullptr && module.GetEngine()->IsInitialized();
                disposals.push_back(runtime.Options().resourceName + (lua ? " lua" : "") + (node ? " node" : ""));
            });

            // No PreShutdown: the manager stops what still runs on its way out.
            module.Shutdown();

            EQUALS(module.GetResourceManager() == nullptr, true);
            EQUALS(module.GetLuaEngine() == nullptr, true);
            EQUALS(module.GetEngine() == nullptr, true);

            // A server that ticks once more on its way out finds nothing to do.
            module.Update();
            module.WaitUntil(std::chrono::steady_clock::now() + std::chrono::milliseconds(2));
        }

        // The Lua state was closed with both engines still up, so by the manager.
        EQUALS(disposals.size(), 1u);
        EQUALS((disposals.empty() ? std::string() : disposals.front()), "lua-worker lua node");

        const std::vector<std::string> lines = log.Lines();
        EQUALS(Has(lines, "[lua-worker] lua handled its stop"), true);
        EQUALS(Has(lines, "[js-worker] javascript handled its stop"), true);
    });

    IT("shuts down as the server instance does, stopping the resources first", {
        Bus bus;
        bus.server.files.Resource("js-worker", {{"main.js", "setInterval(() => {}, 5); Events.on('tick', () => {});"}});
        bus.server.files.Resource("lua-worker", {{"main.lua", "setInterval(function() Events.emit('tick') end, 5)"}});
        bus.server.files.Resource("mixed", {{"a.js", "setInterval(() => {}, 5);"}, {"b.lua", "setInterval(function() end, 5)"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"js-worker", "lua-worker", "mixed"}), true);
        bus.server.Wait(20);

        ServerScriptingModule &module = bus.server.module;
        EQUALS(module.PreShutdown(), true);
        EQUALS(module.GetLuaEngine()->GetRuntime("lua-worker") == nullptr, true);
        EQUALS(module.GetLuaEngine()->GetRuntime("mixed") == nullptr, true);
        EQUALS(module.GetEngine()->GetResourceRuntime("js-worker") == nullptr, true);
        EQUALS(module.GetEngine()->GetResourceRuntime("mixed") == nullptr, true);

        module.Shutdown();
        EQUALS(module.GetLuaEngine() == nullptr, true);
        EQUALS(module.GetEngine() == nullptr, true);
    });

    // ==================== Lua alone ====================

    IT("runs a server of Lua resources alone without a Node runtime for any of them", {
        Bus bus;
        bus.server.files.Resource("bank", {{"main.lua", R"(
            Exports.register('deposit', function(amount) sleep(5) return amount + 1 end)
            Events.on('resourceStop', function(name) if name == 'bank' then sleep(5) end end)
        )"}},
            R"(,"exports":["deposit"])");
        bus.server.files.Resource("teller", {{"main.lua", R"(
            balance = 0
            beats = 0
            setInterval(function() beats = beats + 1 end, 2)
            Events.on('paid', function(note) balance = note.balance end)
            setTimeout(function() Events.emit('paid', { balance = Imports.get('bank').deposit(41) }) end, 0)
        )"}});
        EQUALS(bus.Init(), true);

        NodeEngine *node = bus.server.module.GetEngine();
        EQUALS(node->GetRuntimeStats().size(), 1u);

        // Every resource runtime the Node engine is asked for from here on is counted.
        int nodeRuntimes = 0;
        node->SetRuntimeSetupCallback([&nodeRuntimes](Engine *) {
            ++nodeRuntimes;
        });

        EQUALS(bus.StartAll({"bank", "teller"}), true);
        // Ticks and waits as a server does, through an export that sleeps, an event and timers.
        EQUALS(bus.Until([&] {
            bus.server.Wait(2);
            return bus.Lua("teller", "balance") == "42" && bus.Lua("teller", "beats >= 3") == "true";
        }),
            true);
        EQUALS((bool)bus.Manager().RestartResource("teller"), true);
        EQUALS((bool)bus.Manager().StopResource("bank"), true);
        bus.server.module.Update();

        EQUALS(nodeRuntimes, 0);
        EQUALS(node->GetResourceRuntime("bank") == nullptr, true);
        EQUALS(node->GetResourceRuntime("teller") == nullptr, true);
        // The engine's own runtime is still the only one there is.
        EQUALS(node->GetRuntimeStats().size(), 1u);
    });

    // ==================== Globals ====================

    IT("gives Lua every global a server gives JavaScript, under the same name", {
        const std::set<std::string> engine = LuaInteropTest::EngineGlobals();
        EQUALS(engine.empty(), false);

        Bus bus;
        bus.server.files.Resource("js", {{"main.js", ""}});
        bus.server.files.Resource("lua", {{"main.lua", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"js", "lua"}), true);

        // What the framework added to the globals of a resource runtime, and what its catalog tells a script
        // author is global: the classes a game publishes and the timers, which are the engine's own, are in the
        // second only.
        std::set<std::string> expected;
        for (const std::string &name : LuaInteropTest::Split(bus.Js("js", LuaInteropTest::kGlobalNames))) {
            if (!engine.contains(name)) {
                expected.insert(name);
            }
        }
        const std::set<std::string> added = expected;
        for (const std::string &name : LuaInteropTest::CatalogGlobals()) {
            expected.insert(name);
        }

        // The comparison is of something: the bindings were seen, and so was the catalog.
        for (const char *name : {"Vector2", "Vector3", "Vector4", "Quaternion", "Color", "Events", "Messages", "Imports", "Exports", "ExecutionEnvironment", "Chat", "Voice"}) {
            EQUALS((added.contains(name) ? std::string() : std::string(name)), "");
        }
        for (const char *name : {"console", "Entity", "Player", "TextLabel", "setTimeout", "setInterval", "clearTimeout", "clearInterval"}) {
            EQUALS((expected.contains(name) ? std::string() : std::string(name)), "");
        }

        std::set<std::string> missing;
        for (const std::string &name : expected) {
            if (!LuaInteropTest::JavaScriptOnly().contains(name) && bus.Lua("lua", "_G['" + name + "'] ~= nil") != "true") {
                missing.insert(name);
            }
        }
        EQUALS(LuaInteropTest::Join(missing), "");

        // An exemption that is no longer needed, or names nothing, goes.
        std::set<std::string> stale;
        for (const std::string &name : LuaInteropTest::JavaScriptOnly()) {
            if (!expected.contains(name) || bus.Lua("lua", "_G['" + name + "'] ~= nil") == "true") {
                stale.insert(name);
            }
        }
        EQUALS(LuaInteropTest::Join(stale), "");
        // Not a global of JavaScript either, whatever the catalog files it under.
        EQUALS(bus.Js("js", "typeof StateBag"), "undefined");

        // The same kind of thing under the name: a class is made, a namespace called into, in both.
        EQUALS(bus.Js("js", "typeof Vector3 + ' ' + typeof Events.on + ' ' + typeof console.log + ' ' + typeof setTimeout"), "function function function function");
        EQUALS(bus.Lua("lua", "type(Vector3.new) .. ' ' .. type(Events.on) .. ' ' .. type(console.log) .. ' ' .. type(setTimeout)"), "function function function function");

        // A connection is a class of handles in both and a global in neither.
        EQUALS(bus.Js("js", "typeof PendingConnection"), "undefined");
        EQUALS(bus.Lua("lua", "_G['PendingConnection'] == nil"), "true");
    });

    // ==================== The log ====================

    IT("says which engines the module runs on once it is up", {
        LogCapture log;
        Bus bus;
        EQUALS(bus.Init(), true);

        const std::string line = Find(log.Lines(), "scripting module initialized");
        EQUALS(Contains(line, "Node.js"), true);
        EQUALS(Contains(line, "Lua"), true);
        EQUALS(Contains(line, "JS Server"), false);
    });

    IT("logs the error of a Lua timer without the control characters a script put in it", {
        LogCapture log;
        Bus bus;
        bus.server.files.Resource("rude", {{"main.lua", "setTimeout(function() error('first\\27[2J line\\r\\n[admin] forged line\\0 end') end, 0)"}}, R"(,"errorBehavior":"continue")");
        EQUALS(bus.Init(), true);
        EQUALS(bus.Start("rude"), "");
        EQUALS(bus.Until([&] {
            return bus.Manager().GetResource("rude")->HasError();
        }),
            true);

        const std::string entry = Find(log.Lines(), "Runtime error");
        EQUALS(Contains(entry, "[rude] Runtime error: "), true);
        EQUALS(Contains(entry, "first"), true);
        EQUALS(LuaInteropTest::HasControlCharacter(entry), false);
        // What the script wrote after a line end is under its own name, like the traceback.
        EQUALS(Contains(entry, "\n[rude] [admin] forged line"), true);
        EQUALS(Contains(entry, "traceback"), true);
        EQUALS(LuaInteropTest::EveryLineNames(entry, "rude"), true);
        // The listeners are told what the script raised, as it raised it.
        EQUALS(Contains(bus.Manager().GetResource("rude")->GetErrorMessage(), "\x1b[2J"), true);
    });

    IT("logs the failure of a Lua handler without the control characters a script put in it", {
        LogCapture log;
        Bus bus;
        bus.server.files.Resource("rude", {{"main.lua", R"(
            Events.on('poke', function() error('poked\27[31m\n[admin] forged line') end)
            Messages.handle('ask', function() error('asked\27[31m\n[admin] forged answer') end)
        )"}},
            R"(,"errorBehavior":"continue")");
        bus.server.files.Resource("js", {{"main.js", "globalThis.poke = () => { Events.emit('poke'); Messages.send('rude', 'ask', {}); return 'sent'; };"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"rude", "js"}), true);
        EQUALS(bus.Js("js", "poke()"), "sent");
        EQUALS(bus.Until([&] {
            return bus.Manager().GetResource("rude")->HasError();
        }),
            true);

        const std::vector<std::string> lines = log.Lines();
        const std::string event              = Find(lines, "Event 'poke' handler error");
        EQUALS(Contains(event, "poked"), true);
        EQUALS(LuaInteropTest::HasControlCharacter(event), false);
        EQUALS(LuaInteropTest::EveryLineNames(event, "rude"), true);

        const std::string message = Find(lines, "Message handler 'ask' error");
        EQUALS(Contains(message, "[rude] Runtime error: "), true);
        EQUALS(Contains(message, "asked"), true);
        EQUALS(LuaInteropTest::HasControlCharacter(message), false);
        EQUALS(LuaInteropTest::EveryLineNames(message, "rude"), true);
    });

    IT("logs so many errors of a resource in a second and says once that it stopped", {
        LogCapture log;
        Bus bus;
        bus.server.files.Resource("noisy", {{"main.lua", "for i = 1, 300 do setTimeout(function() error('failure ' .. i) end, 0) end"}}, R"(,"errorBehavior":"continue")");
        bus.server.files.Resource("quiet", {{"main.lua", "setTimeout(function() error('one failure') end, 0)"}}, R"(,"errorBehavior":"continue")");
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"noisy", "quiet"}), true);
        bus.server.module.Update();
        bus.server.module.Update();

        const std::vector<std::string> lines = log.Lines();
        EQUALS(Count(lines, "[noisy] Runtime error"), 200u);
        EQUALS(Count(lines, "[noisy] errors suppressed"), 1u);
        // The share is of each resource: another one is heard.
        EQUALS(Count(lines, "[quiet] Runtime error"), 1u);
    });

    // ==================== The example ====================

    IT("starts the example resources, which talk to each other across the languages", {
        LogCapture log;
        ServerScriptingModule module;
        module.SetResourcesPath(std::string(FW_TEST_FIXTURES_DIR) + "/resources");
        EQUALS(module.Init() == ScriptingError::SCRIPTING_NONE, true);
        EQUALS(module.StartAllResources(), true);
        EQUALS(module.GetResourceManager()->IsResourceRunning("lua-example"), true);
        EQUALS(module.GetResourceManager()->IsResourceRunning("js-example"), true);
        EQUALS(module.GetLuaEngine()->GetRuntime("lua-example") != nullptr, true);
        EQUALS(module.GetEngine()->GetResourceRuntime("lua-example") == nullptr, true);

        // The server's loop: a tick, then the wait for the next.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        std::vector<std::string> lines;
        while (!Has(lines, "[js-example] welcome") && std::chrono::steady_clock::now() < deadline) {
            module.Update();
            module.WaitUntil(std::chrono::steady_clock::now() + std::chrono::milliseconds(5));
            lines = log.Lines();
        }
        module.PreShutdown();
        lines = log.Lines();

        const size_t ready    = LuaInteropTest::IndexOf(lines, "[lua-example] ready after 3 beats");
        const size_t distance = LuaInteropTest::IndexOf(lines, "[js-example] distance to spawn: 5");
        const size_t welcome  = LuaInteropTest::IndexOf(lines, "[js-example] welcome js-example, number 1, spawn at (10, 0, 5)");
        const size_t greeted  = LuaInteropTest::IndexOf(lines, "[lua-example] greeted 1 resource(s)");
        EQUALS(ready != std::string::npos, true);
        EQUALS(distance != std::string::npos, true);
        EQUALS(welcome != std::string::npos, true);
        EQUALS(greeted != std::string::npos, true);
        EQUALS(ready < distance && distance < welcome && welcome < greeted, true);
    });
})
