/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_events_ut.h"

#include "scripting/resource/resource.h"

#include <string>

namespace LuaExportsTest {
    // A Lua resource that exports a function, a table of functions and data, and a function that raises.
    inline const char *kLuaProvider = R"(
        local total = 0
        Exports.register('add', function(a, b) return a + b end)
        Exports.register('bank', {
            name = 'bank',
            limits = {10, 20},
            origin = Vector3.new(1, 2, 3),
            deposit = function(amount) total = total + amount return total end,
            slow = function(n) sleep(20) return n * 2 end,
            lateFail = function() sleep(5) error('lua late failure') end,
            each = function(callback) return callback(1) + callback(2) end,
        })
        Exports.register('fail', function() error('lua export failed') end)
        function balance() return total end
    )";

    inline const char *kLuaProviderExports = R"(,"exports":["add","bank","fail"])";

    // The same from JavaScript, with a method that tells what it was called on.
    inline const char *kJsProvider = R"(
        let total = 0;
        Exports.register('add', (a, b) => a + b);
        Exports.register('api', {
            version: 3,
            list: [1, 2],
            origin: new Vector3(1, 2, 3),
            deposit(amount) { total += amount; return total; },
            later(n) { return new Promise((resolve) => setTimeout(() => resolve(n + 1), 20)); },
            refuse() { return Promise.reject(new Error('js refused')); },
            boom() { throw new Error('js boom'); },
            receiver() { 'use strict'; return this === undefined ? 'none' : typeof this; },
            each(callback) { return callback(1) + callback(2); },
        });
        globalThis.balance = () => total;
    )";

    inline const char *kJsProviderExports = R"(,"exports":["add","api"])";
} // namespace LuaExportsTest

MODULE(lua_exports, {
    using namespace Framework::Scripting;
    using LuaEventsTest::Bus;
    using LuaEventsTest::Contains;

    // ==================== Lua exports ====================

    IT("gives a Lua resource the function and the table of functions another Lua resource exports", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", LuaExportsTest::kLuaProvider}}, LuaExportsTest::kLuaProviderExports);
        bus.server.files.Resource("consumer", {{"main.lua", "add = Exports.get('provider', 'add') bank = Exports.get('provider', 'bank')"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "consumer"}), true);

        EQUALS(bus.Lua("consumer", "add(2, 3)"), "5");
        EQUALS(bus.Lua("consumer", "bank.name .. '|' .. bank.limits[2] .. '|' .. bank.origin:toString()"), "bank|20|" + bus.Lua("provider", "Vector3.new(1, 2, 3):toString()"));
        EQUALS(bus.Lua("consumer", "bank.deposit(5) + bank.deposit(5)"), "15");
        EQUALS(bus.Lua("provider", "balance()"), "10");
        // A function given to an export is called back in the resource that made it.
        EQUALS(bus.Lua("consumer", "bank.each(function(n) return n * 10 end)"), "30");

        // The table is a copy: changing it changes nothing for the owner or the next reader.
        EQUALS(bus.Lua("consumer", "(function() bank.name = 'mine' return Exports.get('provider', 'bank').name end)()"), "bank");
        // A function arrives as the same function each time.
        EQUALS(bus.Lua("consumer", "rawequal(add, Exports.get('provider', 'add'))"), "true");

        EQUALS(bus.Lua("consumer", "(function() local keys = {} for key in pairs(Imports.get('provider')) do keys[#keys + 1] = key end table.sort(keys) return table.concat(keys, ',') end)()"), "add,bank,fail");
        EQUALS(bus.Lua("consumer", "Imports.get('provider').add(4, 4)"), "8");
    });

    IT("gives a JavaScript resource what a Lua resource exports", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", LuaExportsTest::kLuaProvider}}, LuaExportsTest::kLuaProviderExports);
        bus.server.files.Resource("consumer", {{"main.js", "globalThis.add = Exports.get('provider', 'add'); globalThis.bank = Exports.get('provider', 'bank');"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "consumer"}), true);

        EQUALS(bus.Js("consumer", "add(2, 3)"), "5");
        EQUALS(bus.Js("consumer", "[bank.name, bank.limits[1], bank.origin instanceof Vector3, bank.origin.z].join('|')"), "bank|20|true|3");
        EQUALS(bus.Js("consumer", "bank.deposit(5) + bank.deposit(5)"), "15");
        EQUALS(bus.Lua("provider", "balance()"), "10");
        EQUALS(bus.Js("consumer", "bank.each((n) => n * 10)"), "30");
        EQUALS(bus.Js("consumer", "add === Exports.get('provider', 'add')"), "true");

        EQUALS(bus.Js("consumer", "Object.keys(Imports.get('provider')).sort().join(',')"), "add,bank,fail");
        EQUALS(bus.Js("consumer", "Imports.get('provider').add(4, 4)"), "8");
    });

    IT("gives a JavaScript caller a Promise for a Lua export that sleeps", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", LuaExportsTest::kLuaProvider}}, LuaExportsTest::kLuaProviderExports);
        bus.server.files.Resource("consumer", {{"main.js", R"(
            globalThis.settled = 'pending';
            globalThis.call = (name) => {
                const result = Exports.get('provider', 'bank')[name](4);
                result.then((value) => { settled = 'resolved ' + value; }, (error) => { settled = 'rejected ' + (error instanceof Error) + ' ' + error.message; });
                return result instanceof Promise;
            };
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "consumer"}), true);

        EQUALS(bus.Js("consumer", "call('slow')"), "true");
        EQUALS(bus.Js("consumer", "settled"), "pending");
        EQUALS(bus.Until([&] {
            return bus.Js("consumer", "settled") != "pending";
        }),
            true);
        EQUALS(bus.Js("consumer", "settled"), "resolved 8");

        // What the export raises after it waited rejects the promise.
        EQUALS(bus.Js("consumer", "(settled = 'pending', call('lateFail'))"), "true");
        EQUALS(bus.Until([&] {
            return bus.Js("consumer", "settled") != "pending";
        }),
            true);
        EQUALS(Contains(bus.Js("consumer", "settled"), "rejected true"), true);
        EQUALS(Contains(bus.Js("consumer", "settled"), "lua late failure"), true);
    });

    IT("makes a Lua caller wait for a Lua export that sleeps", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", LuaExportsTest::kLuaProvider}}, LuaExportsTest::kLuaProviderExports);
        bus.server.files.Resource("consumer", {{"main.lua", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "consumer"}), true);

        EQUALS(bus.Run("consumer", "result = Exports.get('provider', 'bank').slow(4)"), "");
        EQUALS(bus.Lua("consumer", "result"), "nil");
        EQUALS(bus.Until([&] {
            return bus.Lua("consumer", "result") != "nil";
        }),
            true);
        EQUALS(bus.Lua("consumer", "result"), "8");

        EQUALS(bus.Run("consumer", "ok, failure = pcall(Exports.get('provider', 'bank').lateFail)"), "");
        EQUALS(bus.Until([&] {
            return bus.Lua("consumer", "ok") != "nil";
        }),
            true);
        EQUALS(bus.Lua("consumer", "ok"), "false");
        EQUALS(Contains(bus.Lua("consumer", "failure"), "lua late failure"), true);
    });

    IT("tells the caller, in either language, what a Lua export raised", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", LuaExportsTest::kLuaProvider}}, LuaExportsTest::kLuaProviderExports);
        bus.server.files.Resource("lua-consumer", {{"main.lua", ""}});
        bus.server.files.Resource("js-consumer", {{"main.js", "globalThis.attempt = () => { try { Exports.get('provider', 'fail')(); return 'returned'; } catch (error) { return (error instanceof Error) + ' ' + error.message; } };"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "lua-consumer", "js-consumer"}), true);

        EQUALS(bus.Lua("lua-consumer", "select(1, pcall(Exports.get('provider', 'fail')))"), "false");
        EQUALS(Contains(bus.Lua("lua-consumer", "select(2, pcall(Exports.get('provider', 'fail')))"), "lua export failed"), true);
        EQUALS(Contains(bus.Js("js-consumer", "attempt()"), "true "), true);
        EQUALS(Contains(bus.Js("js-consumer", "attempt()"), "lua export failed"), true);
    });

    // ==================== JavaScript exports ====================

    IT("gives a Lua resource what a JavaScript resource exports", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.js", LuaExportsTest::kJsProvider}}, LuaExportsTest::kJsProviderExports);
        bus.server.files.Resource("consumer", {{"main.lua", "add = Exports.get('provider', 'add') api = Exports.get('provider', 'api')"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "consumer"}), true);

        EQUALS(bus.Lua("consumer", "add(2, 3)"), "5");
        EQUALS(bus.Lua("consumer", "api.version .. '|' .. api.list[2] .. '|' .. api.origin.z"), "3|2|" + bus.Lua("consumer", "Vector3.new(1, 2, 3).z"));
        EQUALS(bus.Lua("consumer", "api.deposit(5) + api.deposit(5)"), "15");
        EQUALS(bus.Js("provider", "balance()"), "10");
        EQUALS(bus.Lua("consumer", "api.each(function(n) return n * 10 end)"), "30");
        // A function called from Lua is called on nothing, however it was exported.
        EQUALS(bus.Lua("consumer", "api.receiver()"), "none");
        EQUALS(bus.Lua("consumer", "api:receiver()"), "none");

        // A copy: the owner's object is its own.
        EQUALS(bus.Lua("consumer", "(function() api.version = 9 return Exports.get('provider', 'api').version end)()"), "3");
        EQUALS(bus.Lua("consumer", "rawequal(add, Exports.get('provider', 'add'))"), "true");

        EQUALS(bus.Lua("consumer", "(function() local keys = {} for key in pairs(Imports.get('provider')) do keys[#keys + 1] = key end table.sort(keys) return table.concat(keys, ',') end)()"), "add,api");
        EQUALS(bus.Lua("consumer", "Imports.get('provider').add(4, 4)"), "8");
    });

    IT("makes a Lua caller wait for the promise of a JavaScript export, and raises its rejection", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.js", LuaExportsTest::kJsProvider}}, LuaExportsTest::kJsProviderExports);
        bus.server.files.Resource("consumer", {{"main.lua", "api = Exports.get('provider', 'api')"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "consumer"}), true);

        EQUALS(bus.Run("consumer", "result = api.later(4)"), "");
        EQUALS(bus.Lua("consumer", "result"), "nil");
        EQUALS(bus.Until([&] {
            return bus.Lua("consumer", "result") != "nil";
        }),
            true);
        EQUALS(bus.Lua("consumer", "result"), "5");

        EQUALS(bus.Run("consumer", "ok, failure = pcall(api.refuse)"), "");
        EQUALS(bus.Until([&] {
            return bus.Lua("consumer", "ok") != "nil";
        }),
            true);
        EQUALS(bus.Lua("consumer", "ok"), "false");
        EQUALS(Contains(bus.Lua("consumer", "failure"), "js refused"), true);

        // What it throws at once is raised at once.
        EQUALS(bus.Lua("consumer", "select(1, pcall(api.boom))"), "false");
        EQUALS(Contains(bus.Lua("consumer", "select(2, pcall(api.boom))"), "js boom"), true);
    });

    IT("keeps JavaScript exports between JavaScript resources as they were, next to Lua ones", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.js", LuaExportsTest::kJsProvider}, {"more.lua", "Exports.register('fromLua', function() return 'lua' end)"}}, R"(,"exports":["add","api","fromLua"])");
        bus.server.files.Resource("consumer", {{"main.js", "globalThis.api = Exports.get('provider', 'api');"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "consumer"}), true);

        // A method runs on the owner's object, as it always did between two JavaScript runtimes.
        EQUALS(bus.Js("consumer", "api.receiver()"), "object");
        EQUALS(bus.Js("consumer", "api.deposit(7)"), "7");
        // One resource, both languages: every export is reached the same way.
        EQUALS(bus.Js("consumer", "Object.keys(Imports.get('provider')).sort().join(',')"), "add,api,fromLua");
        EQUALS(bus.Js("consumer", "Imports.get('provider').fromLua()"), "lua");
        EQUALS(bus.Js("consumer", "Imports.get('provider').api.receiver()"), "object");
        EQUALS(bus.Lua("provider", "Exports.get('provider', 'add')(1, 1)"), "2");
        EQUALS(bus.Js("provider", "Exports.get('provider', 'fromLua')()"), "lua");
    });

    // ==================== Rules ====================

    IT("does not keep an export the manifest does not declare", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", "registered = Exports.register('hidden', function() end)"}});
        bus.server.files.Resource("lua-consumer", {{"main.lua", ""}});
        bus.server.files.Resource("js-consumer", {{"main.js", "globalThis.attempt = () => { try { Exports.get('provider', 'hidden'); return 'found'; } catch (error) { return error.message; } };"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "lua-consumer", "js-consumer"}), true);

        // As in JavaScript: the call answers true, and nothing is registered.
        EQUALS(bus.Lua("provider", "registered"), "true");
        EQUALS(bus.Manager().GetResource("provider")->HasRegisteredExport("hidden"), false);
        EQUALS(bus.Lua("lua-consumer", "select(2, pcall(Exports.get, 'provider', 'hidden'))"), "Exports.get: export 'hidden' not found in resource 'provider'");
        EQUALS(bus.Js("js-consumer", "attempt()"), "Exports.get: export 'hidden' not found in resource 'provider'");
        EQUALS(bus.Lua("lua-consumer", "next(Imports.get('provider'))"), "nil");
    });

    IT("refuses a second registration of a name where Lua is involved", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", R"(
            Exports.register('value', 'first')
            ok, failure = pcall(Exports.register, 'value', 'second')
        )"}},
            R"(,"exports":["value"])");
        bus.server.files.Resource("mixed", {{"first.js", "Exports.register('fromJs', 'js'); Exports.register('twice', 1); Exports.register('twice', 2);"},
                                               {"second.lua", "Exports.register('fromLua', 'lua') ok, failure = pcall(Exports.register, 'fromJs', 'lua')"},
                                               {"third.js", "try { Exports.register('fromLua', 'js'); globalThis.failure = 'registered'; } catch (error) { globalThis.failure = error.constructor.name + ' ' + error.message; }"}},
            R"(,"exports":["fromJs","fromLua","twice"])");
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "mixed"}), true);

        EQUALS(bus.Lua("provider", "ok"), "false");
        EQUALS(bus.Lua("provider", "failure"), "Exports.register: export 'value' is already registered");
        EQUALS(bus.Lua("provider", "Exports.get('provider', 'value')"), "first");

        EQUALS(bus.Lua("mixed", "failure"), "Exports.register: export 'fromJs' is already registered");
        EQUALS(bus.Js("mixed", "failure"), "Error Exports.register: export 'fromLua' is already registered");
        EQUALS(bus.Lua("mixed", "Exports.get('mixed', 'fromJs') .. Exports.get('mixed', 'fromLua')"), "jslua");
        // JavaScript over JavaScript replaces, as it always did.
        EQUALS(bus.Lua("mixed", "Exports.get('mixed', 'twice')"), "2");
    });

    IT("drops the exports of a resource that stops, in both languages", {
        Bus bus;
        bus.server.files.Resource("lua-provider", {{"main.lua", LuaExportsTest::kLuaProvider}}, LuaExportsTest::kLuaProviderExports);
        bus.server.files.Resource("js-provider", {{"main.js", LuaExportsTest::kJsProvider}}, LuaExportsTest::kJsProviderExports);
        bus.server.files.Resource("lua-consumer", {{"main.lua", "luaAdd = Exports.get('lua-provider', 'add') jsAdd = Exports.get('js-provider', 'add')"}});
        bus.server.files.Resource("js-consumer", {{"main.js", R"(
            globalThis.luaAdd = Exports.get('lua-provider', 'add');
            globalThis.attempt = (run) => { try { return String(run()); } catch (error) { return error.message; } };
        )"}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"lua-provider", "js-provider", "lua-consumer", "js-consumer"}), true);
        EQUALS(bus.Manager().GetResource("lua-provider")->GetRegisteredExportNames().size(), 3u);

        EQUALS((bool)bus.Manager().StopResource("lua-provider"), true);
        EQUALS((bool)bus.Manager().StopResource("js-provider"), true);
        EQUALS(bus.Manager().GetResource("lua-provider")->GetRegisteredExportNames().size(), 0u);
        EQUALS(bus.Manager().GetResource("js-provider")->GetRegisteredExportNames().size(), 0u);

        EQUALS(bus.Lua("lua-consumer", "select(2, pcall(Exports.get, 'lua-provider', 'add'))"), "Exports.get: resource 'lua-provider' is not running");
        EQUALS(bus.Lua("lua-consumer", "select(2, pcall(Exports.get, 'js-provider', 'add'))"), "Exports.get: resource 'js-provider' is not running");
        EQUALS(bus.Lua("lua-consumer", "select(2, pcall(Imports.get, 'js-provider'))"), "Imports.get: resource 'js-provider' is not running");
        EQUALS(bus.Js("js-consumer", "attempt(() => Exports.get('lua-provider', 'add'))"), "Exports.get: resource 'lua-provider' is not running");
        EQUALS(bus.Js("js-consumer", "attempt(() => Imports.get('lua-provider'))"), "Imports.get: resource 'lua-provider' is not running");

        // A function taken before the stop no longer runs anything.
        EQUALS(bus.Lua("lua-consumer", "select(1, pcall(luaAdd, 1, 2))"), "false");
        EQUALS(bus.Lua("lua-consumer", "select(1, pcall(jsAdd, 1, 2))"), "false");
        EQUALS(bus.Js("js-consumer", "attempt(() => luaAdd(1, 2))") != "3", true);

        // Started again, it exports again.
        EQUALS(bus.Start("lua-provider"), "");
        EQUALS(bus.Lua("lua-consumer", "Exports.get('lua-provider', 'add')(1, 2)"), "3");
        EQUALS(bus.Js("js-consumer", "attempt(() => Exports.get('lua-provider', 'add')(1, 2))"), "3");
    });

    IT("words a wrong call from Lua as JavaScript does", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.lua", ""}}, R"(,"exports":["thread"])");
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider"}), true);

        const auto failure = [&](const std::string &call) {
            return bus.Lua("provider", "select(2, pcall(" + call + "))");
        };
        EQUALS(failure("Exports.register, 'name'"), "Exports.register requires 2 arguments: name, value");
        EQUALS(failure("Exports.register, 1, 2"), "Exports.register: name must be a string");
        EQUALS(failure("Exports.get, 'provider'"), "Exports.get requires 2 arguments: resourceName, exportName");
        EQUALS(failure("Exports.get, 'provider', 2"), "Exports.get: resourceName and exportName must be strings");
        EQUALS(failure("Exports.get, 'nobody', 'x'"), "Exports.get: resource 'nobody' not found");
        EQUALS(failure("Exports.get, 'provider', 'x'"), "Exports.get: export 'x' not found in resource 'provider'");
        EQUALS(failure("Imports.get"), "Imports.get requires 1 argument: resourceName");
        EQUALS(failure("Imports.get, 1"), "Imports.get: resourceName must be a string");
        EQUALS(failure("Imports.get, 'nobody'"), "Imports.get: resource 'nobody' not found");

        // A value that cannot leave its runtime is refused when it is registered.
        EQUALS(Contains(failure("Exports.register, 'thread', coroutine.create(function() end)"), "Exports.register: value"), true);
        EQUALS(bus.Manager().GetResource("provider")->HasRegisteredExport("thread"), false);
    });

    IT("refuses a JavaScript export that cannot be copied for Lua", {
        Bus bus;
        bus.server.files.Resource("provider", {{"main.js", "Exports.register('symbol', Symbol('s')); Exports.register('fine', 1);"}}, R"(,"exports":["symbol","fine"])");
        bus.server.files.Resource("consumer", {{"main.lua", ""}});
        EQUALS(bus.Init(), true);
        EQUALS(bus.StartAll({"provider", "consumer"}), true);

        EQUALS(Contains(bus.Lua("consumer", "select(2, pcall(Exports.get, 'provider', 'symbol'))"), "Exports.get: export 'symbol' of resource 'provider'"), true);
        EQUALS(Contains(bus.Lua("consumer", "select(2, pcall(Imports.get, 'provider'))"), "Imports.get: export 'symbol' of resource 'provider'"), true);
        EQUALS(bus.Lua("consumer", "Exports.get('provider', 'fine')"), "1");
    });
})
