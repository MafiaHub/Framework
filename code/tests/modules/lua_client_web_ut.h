/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_client_ut.h"

#include "core_modules.h"
#include "gui/manager.h"
#include "integrations/client/scripting/builtins/web.h"
#include "scripting/bind/descriptors.h"
#include "scripting/engine_scope.h"
#include "scripting/lua/lua_runtime.h"

#include <v8pp/convert.hpp>

#include <lua.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

// The client's Web from JavaScript and from Lua, registered the way ClientScriptingModule registers it. The
// manager of the web views and the views are the stand-ins of client_doubles/gui: they keep what the builtin
// told them, and a test plays the page and the browser.
namespace ClientWebTest {
    // What each language's resource starts with: `show` spells a value the same way in both, `rec(name)` makes
    // a handler that notes what it was given, and `seen()` gives the notes and forgets them.
    inline const char *kJavaScript = "globalThis.show = (v) => v === undefined || v === null ? 'nil' : typeof v === 'string' ? '\"' + v + '\"' : typeof v !== 'object' ? String(v)"
                                     "    : '{' + Object.keys(v).sort().map(k => (Array.isArray(v) ? Number(k) + 1 : k) + '=' + show(v[k])).join() + '}';"
                                     "globalThis.log = [];"
                                     "globalThis.rec = (name) => function (p) { log.push(name + ':' + show(p) + (this === globalThis ? '' : ':this')); };"
                                     "globalThis.seen = () => log.splice(0).join('|');";

    inline const char *kLua = R"(
        function show(v)
            if type(v) == 'nil' then return 'nil' end
            if type(v) == 'string' then return '"' .. v .. '"' end
            if type(v) ~= 'table' then return tostring(v) end
            local keys, parts = {}, {}
            for key in pairs(v) do keys[#keys + 1] = tostring(key) end
            table.sort(keys)
            for _, key in ipairs(keys) do parts[#parts + 1] = key .. '=' .. show(v[key] == nil and v[tonumber(key)] or v[key]) end
            return '{' .. table.concat(parts, ',') .. '}'
        end
        log = {}
        function rec(name) return function(...) log[#log + 1] = name .. ':' .. show((...)) .. (select('#', ...) == 1 and '' or ':args') end end
        function seen() local all = table.concat(log, '|') log = {} return all end
    )";

    // The scripting of a client with Web in its isolate and in every Lua state, a JavaScript resource `js` and
    // a Lua resource `lua`.
    struct Stage final {
        // On the heap: the tests of a module share one stack frame.
        std::unique_ptr<Framework::GUI::Manager> gui = std::make_unique<Framework::GUI::Manager>();
        LuaClientTest::Client client;
        std::vector<std::string> mounted;
        bool ready = false;

        // Without `cleanupOnStop` nobody tells Web that a resource stopped, as the module does.
        explicit Stage(bool cleanupOnStop = true, Framework::Scripting::LuaEngineOptions options = Framework::Scripting::LuaEngineOptions::Client()): client(Framework::Utils::Vfs::kResourceMountRoot, options) {
            current = this;
            Framework::CoreModules::SetWebManager(gui.get());
            gui->Start();
            client.lua.AddRuntimeSetup([](Framework::Scripting::LuaRuntime &runtime) {
                Framework::Integrations::Client::Scripting::Builtins::Web::RegisterLua(runtime);
                // What the host can do to the scripting while a handler runs.
                lua_pushcfunction(runtime.State(), [](lua_State *L) {
                    current->client.manager->StopResource(lua_tostring(L, 1));
                    return 0;
                });
                lua_setglobal(runtime.State(), "stopResource");
                lua_pushcfunction(runtime.State(), [](lua_State *) {
                    Framework::Integrations::Client::Scripting::Builtins::Web::Shutdown();
                    return 0;
                });
                lua_setglobal(runtime.State(), "shutdownWeb");
                lua_pushcfunction(runtime.State(), [](lua_State *) {
                    current->client.manager->Reset();
                    Framework::Integrations::Client::Scripting::Builtins::Web::Shutdown();
                    return 0;
                });
                lua_setglobal(runtime.State(), "resetScripting");
            });
            ready = Register();
            if (ready && cleanupOnStop) {
                client.manager->AddOnResourceStopped([](const std::string &resourceName) {
                    Framework::Integrations::Client::Scripting::Builtins::Web::CleanupResource(resourceName);
                });
            }
            ready = ready && Start("js", "client.js", kJavaScript).empty() && Start("lua", "client.lua", kLua).empty();
        }

        // Brings the engines up and registers Web in the isolate, as the module's Init does: at the start and
        // after a Reset.
        bool Register() {
            if (!client.Init()) {
                return false;
            }
            Framework::Scripting::EngineScope scope(&client.v8);
            const auto context = scope.Context();
            Framework::Integrations::Client::Scripting::Builtins::Web::Register(scope.Isolate(), context, context->Global(), client.manager.get());
            const auto expose = [&](const char *name, v8::FunctionCallback callback) {
                context->Global()->Set(context, v8pp::to_v8(scope.Isolate(), name), v8::Function::New(context, callback).ToLocalChecked()).Check();
            };
            expose("stopResource", [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                current->client.manager->StopResource(v8pp::from_v8<std::string>(info.GetIsolate(), info[0]));
            });
            expose("shutdownWeb", [](const v8::FunctionCallbackInfo<v8::Value> &) {
                Framework::Integrations::Client::Scripting::Builtins::Web::Shutdown();
            });
            expose("resetScripting", [](const v8::FunctionCallbackInfo<v8::Value> &) {
                current->client.manager->Reset();
                Framework::Integrations::Client::Scripting::Builtins::Web::Shutdown();
            });
            return true;
        }

        // In the order of ClientScriptingModule::Reset.
        void Reset() {
            client.manager->Reset();
            client.lua.Shutdown();
            Framework::Integrations::Client::Scripting::Builtins::Web::Shutdown();
            client.v8.ResetContext();
        }

        // In the order of ClientScriptingModule::Shutdown.
        ~Stage() {
            if (client.manager) {
                client.manager->StopAll();
            }
            Framework::Integrations::Client::Scripting::Builtins::Web::Shutdown();
            client.manager.reset();
            client.lua.Shutdown();
            client.v8.Shutdown();
            Framework::CoreModules::SetWebManager(nullptr);
            current = nullptr;
            for (const std::string &name : mounted) {
                Framework::Utils::Vfs::Get().Unmount(name + ".zip");
            }
        }

        // Mounts and starts a resource of one client script. Empty when it started.
        std::string Start(const std::string &name, const std::string &file, const std::string &source, const std::string &manifest = "") {
            if (const std::string error = LuaClientTest::Mount(name, "\"clientScripts\":[\"" + file + "\"]" + manifest, {{file, source}}); !error.empty()) {
                return error;
            }
            mounted.push_back(name);
            return client.Start(name);
        }

        // A tick of the scripting: the browser events are drained, then the engines are updated.
        void Frame() {
            Framework::Integrations::Client::Scripting::Builtins::Web::Update();
            client.Update();
        }

        // What a JavaScript expression gives as a script of `resource`, as String() spells it, or "<error> "
        // with the name and the message of what it threw.
        std::string Js(const std::string &expression, const std::string &resource = "js") {
            const std::string origin = resource.empty() ? std::string("outside.js") : "/resources/" + resource + "/client.js";
            if (!client.v8.Execute("globalThis.__result = (() => { try { return String(" + expression + "); } catch (e) { return '<error> ' + e.name + ': ' + e.message; } })();", origin)) {
                return "<did not run>";
            }
            Framework::Scripting::EngineScope scope(&client.v8);
            const auto context = scope.Context();
            return v8pp::from_v8<std::string>(scope.Isolate(), context->Global()->Get(context, v8pp::to_v8(scope.Isolate(), "__result")).ToLocalChecked());
        }

        // What a Lua expression gives in the state of `resource`, as tostring() spells it, or "<error> " and
        // what it raised.
        std::string Lua(const std::string &expression, const std::string &resource = "lua") const {
            return client.Eval(resource, expression);
        }

        Framework::GUI::View *View(int id) const {
            return gui->GetView(id);
        }

        // Where a view is and how it shows, as the builtin left it.
        std::string Shape(int id) const {
            const auto *view = View(id);
            if (view == nullptr) {
                return "<no view>";
            }
            return std::to_string(view->width) + "x" + std::to_string(view->height) + "+" + std::to_string(view->x) + "+" + std::to_string(view->y) + " z=" + std::to_string(view->z) + (view->shouldDisplay ? " shown" : " hidden") + (view->hasFocus ? " focused" : "")
                 + (view->offscreen ? " offscreen" : "") + (view->autoResize ? " auto" : "");
        }

        // The last script a view was told to run.
        std::string Script(int id) const {
            const auto *view = View(id);
            return view != nullptr && !view->scripts.empty() ? view->scripts.back() : std::string();
        }

        // The names the SDK of a view has a listener for.
        std::string Listeners(int id) const {
            std::string names;
            if (const auto *view = View(id)) {
                for (const auto &[name, listener] : view->listeners) {
                    names += name + "|";
                }
            }
            return names;
        }

        // The page of a view calls callEvent(name, payload).
        void Page(int id, const std::string &name, const std::string &payload) const {
            if (const auto *view = View(id)) {
                view->Page(name, payload);
            }
        }

        // The browser of a view reports something.
        void Raise(int id, const Framework::GUI::ViewEventData &data) const {
            if (const auto *view = View(id)) {
                view->Raise(data);
            }
        }

        void Raise(int id, Framework::GUI::ViewEvent event) const {
            Framework::GUI::ViewEventData data;
            data.event = event;
            Raise(id, data);
        }

        // The stage of the running test, for the functions a script calls.
        static inline Stage *current = nullptr;

        // The errors the manager told its listeners of, each followed by a bar.
        std::string Errors() const {
            std::string all;
            for (const std::string &error : client.errors) {
                all += error + "|";
            }
            return all;
        }
    };

    // The same answer from Lua and from JavaScript.
    inline std::string Pair(const std::string &answer) {
        return answer + " / " + answer;
    }

    // What a call that fails raised, when Lua raised it at the script and JavaScript threw it as an Error:
    // the message they share. Anything else is spelled out.
    inline std::string Raised(Stage &stage, const std::string &lua, const std::string &js) {
        const std::string fromLua = stage.Lua(lua);
        const std::string fromJs  = stage.Js(js);
        const std::string luaHead = "<error> eval:1: ";
        const std::string jsHead  = "<error> Error: ";
        if (fromLua.rfind(luaHead, 0) == 0 && fromJs.rfind(jsHead, 0) == 0 && fromLua.substr(luaHead.size()) == fromJs.substr(jsHead.size())) {
            return fromJs.substr(jsHead.size());
        }
        return fromLua + " / " + fromJs;
    }

    // An event of each kind with every field set, for a view of each language.
    inline Framework::GUI::ViewEventData Filled(Framework::GUI::ViewEvent event) {
        Framework::GUI::ViewEventData data;
        data.event       = event;
        data.url         = "https://x.test/p";
        data.openerUrl   = "https://x.test/o";
        data.origin      = "https://x.test";
        data.domain      = "x.test";
        data.description = "ERR_X";
        data.tooltip     = "tip";
        data.message     = "msg";
        data.source      = "src.js";
        data.errorCode   = -3;
        data.cursorType  = CT_HAND;
        data.line        = 12;
        data.severity    = LOGSEVERITY_ERROR;
        data.reason      = Framework::GUI::ViewBlockReason::HostFilter;
        data.isMainFrame = true;
        data.blocked     = true;
        data.focused     = true;
        data.editable    = true;
        return data;
    }
} // namespace ClientWebTest

MODULE(lua_client_web, {
    // ==================== Surface ====================

    IT("gives both languages the same Web, read-only in Lua and absent from a server state", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);

        // The order the hand-written builtin attached them in.
        const std::string members = "createView,destroyView,showView,hideView,focusView,isViewVisible,setViewOffscreen,isViewOffscreen,loadURL,resizeView,setViewPosition,on,off,emit,getScreenSize";
        EQUALS(stage.Js("Object.keys(Web).join()"), members);
        EQUALS(stage.Js("Object.keys(Web).every(name => typeof Web[name] === 'function')"), "true");

        // A namespace of Lua cannot be listed with pairs: every name JavaScript has is asked for.
        EQUALS(stage.Lua("(function() local missing = {} for name in ('" + members + "'):gmatch('[^,]+') do if type(Web[name]) ~= 'function' then missing[#missing + 1] = name end end return table.concat(missing, ',') end)()"), "");
        EQUALS(stage.Lua("pcall(function() Web.createView = print end)"), "false");
        EQUALS(stage.Lua("pcall(function() Web.extra = 1 end)"), "false");
        EQUALS(stage.Lua("Web.evaluate"), "nil");

        // And the definition declares exactly those, for the client alone.
        const Framework::Scripting::Bind::ClassDesc *described = Framework::Scripting::Bind::ApiCatalog::Get().Find("Web", Framework::Scripting::Bind::Side::Client);
        EQUALS(described != nullptr, true);
        EQUALS(described == &Framework::Integrations::Client::Scripting::Builtins::Web::Definition().Describe(), true);
        EQUALS(Framework::Scripting::Bind::ApiCatalog::Get().Find("Web", Framework::Scripting::Bind::Side::Server) == nullptr, true);
        std::string declared;
        for (const Framework::Scripting::Bind::FunctionDesc &function : described->functions) {
            EQUALS(function.side == Framework::Scripting::Bind::Side::Client, true);
            declared += (declared.empty() ? "" : ",") + function.name;
        }
        EQUALS(declared, members);
        EQUALS(described->properties.empty(), true);

        Framework::Scripting::LuaRuntime server(LuaRuntimeTest::Options());
        EQUALS(server.Options().side == Framework::Scripting::Bind::Side::Server, true);
        Framework::Integrations::Client::Scripting::Builtins::Web::RegisterLua(server);
        EQUALS(LuaRuntimeTest::Eval(server, "type(Web)"), "nil");
    });

    // ==================== Views ====================

    IT("creates a view from both languages: locked to its origin, collected with its resource, shaped by the options", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);

        EQUALS(stage.Js("Web.createView('https://x.test/index.html')"), "1");
        EQUALS(stage.Lua("Web.createView('https://y.test/index.html')"), "2");
        EQUALS(stage.Lua("math.type(Web.createView('fw://ui/menu.html', { width = 800, height = 600, x = 10, y = 20, zIndex = 5, visible = false, focus = true }))"), "integer");
        EQUALS(stage.Js("Web.createView('fw://ui/menu.html', { width: 800, height: 600, x: 10, y: 20, zIndex: 5, visible: false, focus: true })"), "4");
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount(), 4u);

        // Without a size a view fills the client area and follows it; it shows, and has no focus.
        EQUALS(stage.Shape(1), "1920x1080+0+0 z=0 shown auto");
        EQUALS(stage.Shape(2), stage.Shape(1));
        EQUALS(stage.Shape(3), "800x600+10+20 z=5 hidden focused");
        EQUALS(stage.Shape(4), stage.Shape(3));

        // Every view is the framework's to collect and is locked to the origin of its URL, after the events
        // of its browser are listened to, so that the lock is one of them.
        for (const int id : {1, 2, 3, 4}) {
            const auto *view = stage.View(id);
            EQUALS(view->garbageCollected, true);
            EQUALS(view->lockedTo, view->url);
            EQUALS(view->calls.size() >= 5, true);
            EQUALS(view->calls[0], "SetGarbageCollected");
            EQUALS(view->calls[1], "SetOnViewEventCallback");
            EQUALS(view->calls[2], "LockToOrigin");
            EQUALS(view->calls[3], "SetZIndex");
            EQUALS(view->calls[4], "Display");
        }
        EQUALS(stage.View(2)->url, "https://y.test/index.html");
        EQUALS(stage.View(3)->url, "fw://ui/menu.html");

        // An option of another type than its own is left out; a number is wrapped to 32 bits, as in JavaScript.
        EQUALS(stage.Lua("Web.createView('https://y.test/a', { width = '800', height = 600.9, x = -5.5, y = 0/0, zIndex = 2^32 + 7, visible = 0, focus = 1 })"), "5");
        EQUALS(stage.Js("Web.createView('https://y.test/a', { width: '800', height: 600.9, x: -5.5, y: NaN, zIndex: 2**32 + 7, visible: 0, focus: 1 })"), "6");
        EQUALS(stage.Shape(5), "1920x600+-5+0 z=7 shown");
        EQUALS(stage.Shape(6), stage.Shape(5));
        // What is not an object or a table has no option.
        EQUALS(stage.Lua("Web.createView('https://y.test/b', 'options')"), "7");
        EQUALS(stage.Lua("Web.createView('https://y.test/b', false)"), "8");
        EQUALS(stage.Js("Web.createView('https://y.test/b', null)"), "9");
        EQUALS(stage.Shape(7), "1920x1080+0+0 z=0 shown auto");
        EQUALS(stage.Shape(8), stage.Shape(7));
        EQUALS(stage.Shape(9), stage.Shape(7));
        // JavaScript reads its object as a script would, and Lua its table raw.
        EQUALS(stage.Js("Web.createView('https://y.test/c', { get width() { return 320; }, height: 200, __proto__: { x: 9 } })"), "10");
        EQUALS(stage.Shape(10), "320x200+9+0 z=0 shown");
        EQUALS(stage.Lua("Web.createView('https://y.test/c', setmetatable({ height = 200 }, { __index = { width = 320, x = 9 } }))"), "11");
        EQUALS(stage.Shape(11), "1920x200+0+0 z=0 shown");
    });

    IT("refuses to create a view without a URL, a resource or a manager of the views", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);

        for (const char *arguments : {"", "1", "nil", "{}", "true"}) {
            EQUALS(stage.Lua(std::string("Web.createView(") + arguments + ")"), "<error> eval:1: Web.createView requires a url string");
        }
        for (const char *arguments : {"", "1", "null", "{}", "true"}) {
            EQUALS(stage.Js(std::string("Web.createView(") + arguments + ")"), "<error> Error: Web.createView requires a url string");
        }
        // A Lua string holds any bytes; what is not UTF-8 is no URL.
        EQUALS(stage.Lua("Web.createView('https://x.test/\\xff')"), "<error> eval:1: Web.createView requires a url string");
        EQUALS(stage.Lua("Web.createView('https://x.test/\\xed\\xa0\\x80')"), "<error> eval:1: Web.createView requires a url string");
        // A script of no resource owns nothing.
        EQUALS(stage.Js("Web.createView('https://x.test/')", ""), "<error> Error: Web.createView: must be called from within a resource");
        // What a getter of the options throws is the result, and no view is made.
        EQUALS(stage.Js("Web.createView('https://x.test/', { get width() { throw new RangeError('getter'); } })"), "<error> RangeError: getter");
        EQUALS(stage.gui->ViewCount(), 0u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount(), 0u);

        stage.gui->failCreate = true;
        EQUALS(stage.Lua("Web.createView('https://x.test/')"), "<error> eval:1: Web.createView: failed to create web view");
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "<error> Error: Web.createView: failed to create web view");
        stage.gui->failCreate = false;

        stage.gui->Shutdown();
        EQUALS(stage.Lua("Web.createView('https://x.test/')"), "<error> eval:1: Web.createView: web view manager is not available");
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "<error> Error: Web.createView: web view manager is not available");
        stage.gui->Start();

        Framework::CoreModules::SetWebManager(nullptr);
        EQUALS(stage.Lua("Web.createView('https://x.test/')"), "<error> eval:1: Web.createView: web view manager is not available");
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "<error> Error: Web.createView: web view manager is not available");
        EQUALS(stage.Lua("show(Web.getScreenSize())"), "{height=0,width=0}");
        Framework::CoreModules::SetWebManager(stage.gui.get());
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount(), 0u);
        EQUALS(stage.Lua("Web.createView('https://x.test/')"), "1");
    });

    IT("shows, hides, focuses and keeps a view painting from both languages", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "1");
        EQUALS(stage.Lua("Web.createView('https://y.test/')"), "2");

        const auto both = [&stage](const std::string &lua, const std::string &js) {
            return stage.Lua(lua) + " / " + stage.Js(js);
        };

        EQUALS(both("Web.focusView(2)", "Web.focusView(1)"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.isViewVisible(2)", "Web.isViewVisible(1)"), ClientWebTest::Pair("true"));
        EQUALS(stage.Shape(2), "1920x1080+0+0 z=0 shown focused auto");
        EQUALS(stage.Shape(1), stage.Shape(2));
        // Hiding a view takes its focus too.
        EQUALS(both("Web.hideView(2)", "Web.hideView(1)"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.isViewVisible(2)", "Web.isViewVisible(1)"), ClientWebTest::Pair("false"));
        EQUALS(stage.Shape(2), "1920x1080+0+0 z=0 hidden auto");
        EQUALS(stage.Shape(1), stage.Shape(2));
        EQUALS(both("Web.showView(2)", "Web.showView(1)"), ClientWebTest::Pair("true"));
        EQUALS(stage.Shape(2), "1920x1080+0+0 z=0 shown auto");
        EQUALS(stage.Shape(1), stage.Shape(2));

        // The flag of focusView and of setViewOffscreen is a boolean or is left out: anything else is true.
        EQUALS(both("Web.focusView(2, false)", "Web.focusView(1, false)"), ClientWebTest::Pair("true"));
        EQUALS((stage.View(1)->hasFocus || stage.View(2)->hasFocus), false);
        EQUALS(both("Web.focusView(2, 0)", "Web.focusView(1, 0)"), ClientWebTest::Pair("true"));
        EQUALS((stage.View(1)->hasFocus && stage.View(2)->hasFocus), true);
        EQUALS(both("Web.focusView(2, false)", "Web.focusView(1, false)"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.focusView(2, nil)", "Web.focusView(1, null)"), ClientWebTest::Pair("true"));
        EQUALS((stage.View(1)->hasFocus && stage.View(2)->hasFocus), true);

        EQUALS(both("Web.isViewOffscreen(2)", "Web.isViewOffscreen(1)"), ClientWebTest::Pair("false"));
        EQUALS(both("Web.setViewOffscreen(2)", "Web.setViewOffscreen(1)"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.isViewOffscreen(2)", "Web.isViewOffscreen(1)"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.setViewOffscreen(2, false)", "Web.setViewOffscreen(1, false)"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.isViewOffscreen(2)", "Web.isViewOffscreen(1)"), ClientWebTest::Pair("false"));
        EQUALS(both("Web.setViewOffscreen(2, 'yes')", "Web.setViewOffscreen(1, 'yes')"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.isViewOffscreen(2)", "Web.isViewOffscreen(1)"), ClientWebTest::Pair("true"));

        // The id is read as JavaScript reads one: the whole part, wrapped to 32 bits.
        EQUALS(both("Web.isViewVisible(2.9)", "Web.isViewVisible(1.9)"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.isViewVisible(2^32 + 2)", "Web.isViewVisible(2**32 + 1)"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.isViewVisible(0/0)", "Web.isViewVisible(NaN)"), ClientWebTest::Pair("false"));
        EQUALS(both("Web.isViewVisible(-1)", "Web.isViewVisible(-1)"), ClientWebTest::Pair("false"));
        EQUALS(both("Web.showView(999)", "Web.showView(999)"), ClientWebTest::Pair("false"));

        for (const char *method : {"destroyView", "showView", "hideView", "focusView", "isViewVisible", "setViewOffscreen", "isViewOffscreen"}) {
            const std::string message = std::string("Web.") + method + ": first argument must be a view id";
            for (const char *arguments : {"", "'1'", "{}", "true"}) {
                EQUALS(stage.Lua(std::string("Web.") + method + "(" + arguments + ")"), "<error> eval:1: " + message);
                EQUALS(stage.Js(std::string("Web.") + method + "(" + arguments + ")"), "<error> Error: " + message);
            }
        }
        EQUALS(stage.gui->ViewCount(), 2u);
    });

    IT("navigates, sizes and places a view from both languages", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "1");
        EQUALS(stage.Lua("Web.createView('https://x.test/')"), "2");

        const auto both = [&stage](const std::string &lua, const std::string &js) {
            return stage.Lua(lua) + " / " + stage.Js(js);
        };

        // A view is locked again to the origin of what it is sent to: the URL goes to the view as it was given.
        EQUALS(both("Web.loadURL(2, 'https://z.test/next')", "Web.loadURL(1, 'https://z.test/next')"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.loadURL(2, 'javascript:alert(1)')", "Web.loadURL(1, 'javascript:alert(1)')"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.loadURL(2, '')", "Web.loadURL(1, '')"), ClientWebTest::Pair("true"));
        for (const int id : {1, 2}) {
            const auto *view = stage.View(id);
            EQUALS(view->loaded.size(), 3u);
            EQUALS(view->loaded[0], "https://z.test/next");
            EQUALS(view->loaded[1], "javascript:alert(1)");
            EQUALS(view->loaded[2], "");
            EQUALS(view->lockedTo, "");
            EQUALS(std::count(view->calls.begin(), view->calls.end(), "LockToOrigin"), 4);
        }
        EQUALS(both("Web.loadURL(999, 'https://z.test/')", "Web.loadURL(999, 'https://z.test/')"), ClientWebTest::Pair("false"));
        for (const char *arguments : {"", "2", "2, 5", "'2', 'https://z.test/'", "2, nil"}) {
            EQUALS(stage.Lua(std::string("Web.loadURL(") + arguments + ")"), "<error> eval:1: Web.loadURL requires 2 arguments: viewId, url");
        }
        for (const char *arguments : {"", "1", "1, 5", "'1', 'https://z.test/'", "1, null"}) {
            EQUALS(stage.Js(std::string("Web.loadURL(") + arguments + ")"), "<error> Error: Web.loadURL requires 2 arguments: viewId, url");
        }
        EQUALS(stage.Lua("Web.loadURL(2, 'https://z.test/\\xc3')"), "<error> eval:1: Web.loadURL requires 2 arguments: viewId, url");
        EQUALS(stage.View(2)->loaded.size(), 3u);

        // A size is two positive numbers, and ends the following of the client area.
        EQUALS(both("Web.resizeView(2, 640, 480)", "Web.resizeView(1, 640, 480)"), ClientWebTest::Pair("true"));
        EQUALS(stage.Shape(2), "640x480+0+0 z=0 shown");
        EQUALS(stage.Shape(1), stage.Shape(2));
        EQUALS(both("Web.resizeView(2, 0, 480)", "Web.resizeView(1, 0, 480)"), ClientWebTest::Pair("false"));
        EQUALS(both("Web.resizeView(2, 640, -1)", "Web.resizeView(1, 640, -1)"), ClientWebTest::Pair("false"));
        EQUALS(both("Web.resizeView(2, 2^32, 480)", "Web.resizeView(1, 2**32, 480)"), ClientWebTest::Pair("false"));
        EQUALS(both("Web.resizeView(999, 10, 10)", "Web.resizeView(999, 10, 10)"), ClientWebTest::Pair("false"));
        EQUALS(both("Web.resizeView(2, 100.7, 2^32 + 50)", "Web.resizeView(1, 100.7, 2**32 + 50)"), ClientWebTest::Pair("true"));
        EQUALS(stage.Shape(2), "100x50+0+0 z=0 shown");
        EQUALS(stage.Shape(1), stage.Shape(2));
        for (const char *arguments : {"", "1, 100", "1, '100', 100", "1, 100, {}", "'1', 100, 100"}) {
            EQUALS(stage.Lua(std::string("Web.resizeView(") + arguments + ")"), "<error> eval:1: Web.resizeView requires 3 arguments: viewId, width, height");
            EQUALS(stage.Js(std::string("Web.resizeView(") + arguments + ")"), "<error> Error: Web.resizeView requires 3 arguments: viewId, width, height");
        }

        EQUALS(both("Web.setViewPosition(2, 5, 6)", "Web.setViewPosition(1, 5, 6)"), ClientWebTest::Pair("true"));
        EQUALS(stage.Shape(2), "100x50+5+6 z=0 shown");
        EQUALS(both("Web.setViewPosition(2, -5.9, 0/0)", "Web.setViewPosition(1, -5.9, NaN)"), ClientWebTest::Pair("true"));
        EQUALS(stage.Shape(2), "100x50+-5+0 z=0 shown");
        EQUALS(stage.Shape(1), stage.Shape(2));
        EQUALS(both("Web.setViewPosition(999, 1, 1)", "Web.setViewPosition(999, 1, 1)"), ClientWebTest::Pair("false"));
        for (const char *arguments : {"", "1, 5", "1, 5, '6'", "'1', 5, 6"}) {
            EQUALS(stage.Lua(std::string("Web.setViewPosition(") + arguments + ")"), "<error> eval:1: Web.setViewPosition requires 3 arguments: viewId, x, y");
            EQUALS(stage.Js(std::string("Web.setViewPosition(") + arguments + ")"), "<error> Error: Web.setViewPosition requires 3 arguments: viewId, x, y");
        }

        EQUALS(both("show(Web.getScreenSize())", "show(Web.getScreenSize())"), ClientWebTest::Pair("{height=1080,width=1920}"));
        EQUALS(stage.Lua("math.type(Web.getScreenSize().width)"), "integer");
        stage.gui->viewport = {1280, 720};
        EQUALS(both("show(Web.getScreenSize(1, 2))", "show(Web.getScreenSize(1, 2))"), ClientWebTest::Pair("{height=720,width=1280}"));

        EQUALS(both("Web.destroyView(2)", "Web.destroyView(1)"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.destroyView(2)", "Web.destroyView(1)"), ClientWebTest::Pair("false"));
        EQUALS(both("Web.showView(2)", "Web.showView(1)"), ClientWebTest::Pair("false"));
        EQUALS(stage.gui->ViewCount(), 0u);
        EQUALS(stage.gui->destroyed.size(), 2u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount(), 0u);
    });

    // ==================== Ownership ====================

    IT("lets a resource reach its own views only, whatever its language", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Start("other", "client.lua", ClientWebTest::kLua), "");
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "1");
        EQUALS(stage.Lua("Web.createView('https://y.test/')"), "2");
        EQUALS(stage.Lua("Web.on(2, 'ev', rec('mine'))"), "nil");
        EQUALS(stage.Js("Web.on(1, 'ev', rec('mine'))"), "undefined");

        // What a resource asks of a view it does not own is answered as for a view that does not exist.
        const auto asked = [&stage](const std::string &resource, bool lua, int id) {
            const std::string view = std::to_string(id);
            std::string answers;
            const auto ask = [&](const std::string &luaCall, const std::string &jsCall) {
                answers += (lua ? stage.Lua(luaCall, resource) : stage.Js(jsCall, resource)) + ",";
            };
            ask("Web.showView(" + view + ")", "Web.showView(" + view + ")");
            ask("Web.hideView(" + view + ")", "Web.hideView(" + view + ")");
            ask("Web.focusView(" + view + ", false)", "Web.focusView(" + view + ", false)");
            ask("Web.isViewVisible(" + view + ")", "Web.isViewVisible(" + view + ")");
            ask("Web.setViewOffscreen(" + view + ")", "Web.setViewOffscreen(" + view + ")");
            ask("Web.isViewOffscreen(" + view + ")", "Web.isViewOffscreen(" + view + ")");
            ask("Web.loadURL(" + view + ", 'https://evil.test/')", "Web.loadURL(" + view + ", 'https://evil.test/')");
            ask("Web.resizeView(" + view + ", 10, 10)", "Web.resizeView(" + view + ", 10, 10)");
            ask("Web.setViewPosition(" + view + ", 1, 1)", "Web.setViewPosition(" + view + ", 1, 1)");
            ask("Web.emit(" + view + ", 'ev', { evil = true })", "Web.emit(" + view + ", 'ev', { evil: true })");
            ask("Web.off(" + view + ", 'ev')", "Web.off(" + view + ", 'ev')");
            ask("Web.destroyView(" + view + ")", "Web.destroyView(" + view + ")");
            ask("Web.on(" + view + ", 'ev', function() end)", "Web.on(" + view + ", 'ev', () => {})");
            return answers;
        };
        const std::string refused = "false,false,false,false,false,false,false,false,false,false,false,false,";
        stage.View(1)->Focus(true);
        stage.View(2)->Focus(true);
        const size_t calls1 = stage.View(1)->calls.size();
        const size_t calls2 = stage.View(2)->calls.size();

        EQUALS(asked("lua", true, 1), refused + "<error> eval:1: Web.on: unknown web view 1,");
        EQUALS(asked("other", true, 1), refused + "<error> eval:1: Web.on: unknown web view 1,");
        EQUALS(asked("other", true, 2), refused + "<error> eval:1: Web.on: unknown web view 2,");
        EQUALS(asked("js", false, 2), refused + "<error> Error: Web.on: unknown web view 2,");
        // A script of no resource reaches none.
        EQUALS(stage.Js("Web.showView(1)", ""), "false");
        EQUALS(stage.Js("Web.destroyView(2)", ""), "false");
        EQUALS(stage.Js("Web.emit(1, 'ev')", ""), "false");

        // Nothing was asked of either view, and each kept its handler.
        EQUALS(stage.View(1)->calls.size(), calls1);
        EQUALS(stage.View(2)->calls.size(), calls2);
        EQUALS(stage.Shape(1), "1920x1080+0+0 z=0 shown focused auto");
        EQUALS(stage.Shape(2), stage.Shape(1));
        EQUALS((stage.View(1)->scripts.empty() && stage.View(2)->scripts.empty()), true);
        EQUALS((stage.View(1)->loaded.empty() && stage.View(2)->loaded.empty()), true);
        EQUALS(stage.gui->destroyed.empty(), true);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 2u);
        stage.Page(1, "ev", "1");
        stage.Page(2, "ev", "2");
        EQUALS(stage.Js("seen()"), "mine:1");
        EQUALS(stage.Lua("seen()"), "mine:2");
    });

    // ==================== Script to page ====================

    IT("dispatches an event into a page as the same script from both languages", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "1");
        EQUALS(stage.Lua("Web.createView('https://x.test/')"), "2");

        // The same call from each language runs the same script in its view.
        const auto same = [&stage](const std::string &lua, const std::string &js) {
            const std::string answers = stage.Lua("Web.emit(2, " + lua + ")") + " / " + stage.Js("Web.emit(1, " + js + ")");
            if (answers != "true / true" || stage.Script(2) != stage.Script(1)) {
                return answers + ": " + stage.Script(2) + " / " + stage.Script(1);
            }
            return stage.Script(1);
        };
        EQUALS(same("'ev'", "'ev'"), "window.dispatchEvent(new CustomEvent('ev'));");
        EQUALS(same("'ev', nil", "'ev', undefined"), "window.dispatchEvent(new CustomEvent('ev'));");
        EQUALS(same("'ev', 5", "'ev', 5"), "window.dispatchEvent(new CustomEvent('ev', { detail: 5 }));");
        EQUALS(same("'ev', 1.5", "'ev', 1.5"), "window.dispatchEvent(new CustomEvent('ev', { detail: 1.5 }));");
        EQUALS(same("'ev', true", "'ev', true"), "window.dispatchEvent(new CustomEvent('ev', { detail: true }));");
        EQUALS(same("'ev', false", "'ev', false"), "window.dispatchEvent(new CustomEvent('ev', { detail: false }));");
        EQUALS(same("'ev', 'str'", "'ev', 'str'"), "window.dispatchEvent(new CustomEvent('ev', { detail: \"str\" }));");
        EQUALS(same("'ev', 'undefined'", "'ev', 'undefined'"), "window.dispatchEvent(new CustomEvent('ev', { detail: \"undefined\" }));");
        EQUALS(same("'ev', { a = 1, b = { 1, 'two' }, c = { d = 'q\\'uo\"te' } }", "'ev', { a: 1, b: [1, 'two'], c: { d: 'q\\'uo\"te' } }"), "window.dispatchEvent(new CustomEvent('ev', { detail: {\"a\":1,\"b\":[1,\"two\"],\"c\":{\"d\":\"q'uo\\\"te\"}} }));");
        EQUALS(same("'ev', '</script>'", "'ev', '</script>'"), "window.dispatchEvent(new CustomEvent('ev', { detail: \"</script>\" }));");
        EQUALS(same("'ev', 1, 2, 3", "'ev', 1, 2, 3"), "window.dispatchEvent(new CustomEvent('ev', { detail: 1 }));");
        EQUALS(same("''", "''"), "window.dispatchEvent(new CustomEvent(''));");
        // A function has no JSON form in either language and is no detail, as JSON.stringify drops it.
        EQUALS(same("'ev', function() end", "'ev', () => 1"), "window.dispatchEvent(new CustomEvent('ev'));");

        // The name is a string of the script that runs in the page, and cannot end it.
        EQUALS(same("\"it's\", 1", "\"it's\", 1"), "window.dispatchEvent(new CustomEvent('it\\'s', { detail: 1 }));");
        EQUALS(same("'a\"b\\\\c\\nd\\re\\tf\\1g\\0h', 1", "'a\"b\\\\c\\nd\\re\\tf\\u0001g\\u0000h', 1"), "window.dispatchEvent(new CustomEvent('a\\\"b\\\\c\\nd\\re\\tf\\u0001g\\u0000h', { detail: 1 }));");
        EQUALS(same("\"'));alert(1);//\", { x = \"'));alert(2);//\" }", "\"'));alert(1);//\", { x: \"'));alert(2);//\" }"), "window.dispatchEvent(new CustomEvent('\\'));alert(1);//', { detail: {\"x\":\"'));alert(2);//\"} }));");
        EQUALS(same("'\\u{e9}\\u{2603}', 1", "'\\u00e9\\u2603', 1"), "window.dispatchEvent(new CustomEvent('\xc3\xa9\xe2\x98\x83', { detail: 1 }));");
        const size_t scripts = stage.View(2)->scripts.size();

        // What Lua can name and JavaScript cannot: bytes that are no text are no event name.
        EQUALS(stage.Lua("Web.emit(2, 'ev\\xff', 1)"), "<error> eval:1: Web.emit requires at least 2 arguments: viewId, eventName");
        EQUALS(stage.Lua("Web.emit(2, '\\xe2\\x28', 1)"), "<error> eval:1: Web.emit requires at least 2 arguments: viewId, eventName");
        for (const char *arguments : {"", "2", "2, 5", "'2', 'ev'", "2, nil, 1"}) {
            EQUALS(stage.Lua(std::string("Web.emit(") + arguments + ")"), "<error> eval:1: Web.emit requires at least 2 arguments: viewId, eventName");
        }
        for (const char *arguments : {"", "1", "1, 5", "'1', 'ev'", "1, null, 1"}) {
            EQUALS(stage.Js(std::string("Web.emit(") + arguments + ")"), "<error> Error: Web.emit requires at least 2 arguments: viewId, eventName");
        }
        // What has no JSON form is refused, and nothing runs in the page.
        for (const char *payload : {"{ f = function() end }", "(function() local t = {} t.t = t return t end)()", "'\\xff'", "{ [true] = 1 }", "coroutine.create(function() end)"}) {
            EQUALS(stage.Lua(std::string("Web.emit(2, 'ev', ") + payload + ")"), "<error> eval:1: Web.emit: payload is not JSON-serializable");
        }
        for (const char *payload : {"10n", "(() => { const o = {}; o.o = o; return o; })()", "{ toJSON() { throw new Error('no'); } }"}) {
            EQUALS(stage.Js(std::string("Web.emit(1, 'ev', ") + payload + ")"), "<error> Error: Web.emit: payload is not JSON-serializable");
        }
        EQUALS(stage.Lua("Web.emit(999, 'ev')"), "false");
        EQUALS(stage.View(2)->scripts.size(), scripts);
        EQUALS(stage.View(1)->scripts.size(), scripts);

        // The differences are the languages' own: null is a value of JavaScript alone, an empty table is a
        // list, and JavaScript writes nothing for what Lua refuses inside a value.
        EQUALS(stage.Js("Web.emit(1, 'ev', null)"), "true");
        EQUALS(stage.Script(1), "window.dispatchEvent(new CustomEvent('ev', { detail: null }));");
        EQUALS(stage.Lua("Web.emit(2, 'ev', {})"), "true");
        EQUALS(stage.Script(2), "window.dispatchEvent(new CustomEvent('ev', { detail: [] }));");
        EQUALS(stage.Js("Web.emit(1, 'ev', {})"), "true");
        EQUALS(stage.Script(1), "window.dispatchEvent(new CustomEvent('ev', { detail: {} }));");
        EQUALS(stage.Js("Web.emit(1, 'ev', [1, undefined, () => 1])"), "true");
        EQUALS(stage.Script(1), "window.dispatchEvent(new CustomEvent('ev', { detail: [1,null,null] }));");
        // And what JSON.stringify gives nothing for is no detail, as it always was.
        for (const char *payload : {"Symbol('s')", "{ toJSON() { return undefined; } }", "{ toJSON() { return () => 1; } }"}) {
            EQUALS(stage.Js(std::string("Web.emit(1, 'ev', ") + payload + ")"), "true");
            EQUALS(stage.Script(1), "window.dispatchEvent(new CustomEvent('ev'));");
        }
        EQUALS(stage.Js("Web.emit(1, 'ev', { toJSON() { return 'undefined'; } })"), "true");
        EQUALS(stage.Script(1), "window.dispatchEvent(new CustomEvent('ev', { detail: \"undefined\" }));");
    });

    // ==================== Page to script ====================

    IT("gives a Lua handler the payload of a page event under the rules a JavaScript handler has", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "1");
        EQUALS(stage.Lua("Web.createView('https://x.test/')"), "2");
        EQUALS(stage.Js("Web.on(1, 'ev', rec('h'))"), "undefined");
        EQUALS(stage.Lua("Web.on(2, 'ev', rec('h'))"), "nil");
        EQUALS(stage.Listeners(1), "ev|");
        EQUALS(stage.Listeners(2), "ev|");

        // JSON arrives as its value, anything else as the text itself, and an empty payload as nothing.
        const auto given = [&stage](const std::string &payload) {
            stage.Page(1, "ev", payload);
            stage.Page(2, "ev", payload);
            return stage.Lua("seen()") + " / " + stage.Js("seen()");
        };
        EQUALS(given(""), ClientWebTest::Pair("h:"
                                              "nil"));
        EQUALS(given("null"), ClientWebTest::Pair("h:"
                                                  "nil"));
        EQUALS(given("{\"a\":1,\"b\":[true,\"x\"],\"c\":{\"d\":1.5}}"), ClientWebTest::Pair("h:"
                                                                                            "{a=1,b={1=true,2=\"x\"},c={d=1.5}}"));
        EQUALS(given("not json"), ClientWebTest::Pair("h:"
                                                      "\"not json\""));
        EQUALS(given("{\"cut\":"), ClientWebTest::Pair("h:"
                                                       "\"{\"cut\":\""));
        EQUALS(given("\"text\""), ClientWebTest::Pair("h:"
                                                      "\"text\""));
        EQUALS(given("12.5"), ClientWebTest::Pair("h:"
                                                  "12.5"));
        EQUALS(given("3"), ClientWebTest::Pair("h:"
                                               "3"));
        EQUALS(given("true"), ClientWebTest::Pair("h:"
                                                  "true"));
        EQUALS(given("[1,2,3]"), ClientWebTest::Pair("h:"
                                                     "{1=1,2=2,3=3}"));
        EQUALS(given("  {\"spaced\" : 1}  "), ClientWebTest::Pair("h:"
                                                                  "{spaced=1}"));
        // Of two members of one name the last counts.
        EQUALS(given("{\"a\":1,\"a\":2}"), ClientWebTest::Pair("h:"
                                                               "{a=2}"));
        EQUALS(given("\"\\u00e9\""), ClientWebTest::Pair("h:"
                                                         "\"\xc3\xa9\""));

        // A whole number is an integer to Lua, however the page spelled it.
        stage.Page(2, "ev", "[1, 2.0, 1e3, 0.5]");
        EQUALS(stage.Lua("seen()"), "h:{1=1,2=2,3=1000,4=0.5}");
        stage.Page(2, "ev", "{\"n\":null,\"list\":[1,null,3]}");
        EQUALS(stage.Lua("seen()"), "h:{list={1=1,3=3}}");

        // A page writes the payload. One nested beyond what a handler is built is its text, not a crash.
        const std::string deep = std::string(100000, '[') + std::string(100000, ']');
        stage.Page(2, "ev", deep);
        EQUALS(stage.Lua("seen()"), "h:\"" + deep + "\"");
        const std::string nested = std::string(128, '[') + "1" + std::string(128, ']');
        stage.Page(2, "ev", nested);
        EQUALS(LuaRuntimeTest::Contains(stage.Lua("seen()"), "h:{1={1={1="), true);
        stage.Page(2, "ev", "[" + nested + "]");
        EQUALS(stage.Lua("seen()"), "h:\"[" + nested + "]\"");

        // An event nobody listens to, and one of another view, reach nobody.
        stage.Page(2, "other", "1");
        stage.Page(1, "other", "1");
        EQUALS(stage.Lua("seen()") + stage.Js("seen()"), "");
        EQUALS(stage.Errors(), "");
    });

    IT("calls the JavaScript handlers of an event on the global, with the one value the payload parses to", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "1");
        // A strict function sees the receiver it is given.
        EQUALS(stage.Js("Web.on(1, 'ev', function (p) { 'use strict'; log.push('strict:' + (this === globalThis)); p.seen = 1; })"), "undefined");
        EQUALS(stage.Js("Web.on(1, 'ev', rec('second'))"), "undefined");
        stage.Page(1, "ev", "{\"k\":1}");
        EQUALS(stage.Js("seen()"), "strict:true|second:{k=1,seen=1}");

        // Each Lua handler gets a value of its own.
        EQUALS(stage.Lua("Web.createView('https://x.test/')"), "2");
        EQUALS(stage.Lua("Web.on(2, 'ev', function(p) p.seen = 1 log[#log + 1] = 'first' end)"), "nil");
        EQUALS(stage.Lua("Web.on(2, 'ev', rec('second'))"), "nil");
        stage.Page(2, "ev", "{\"k\":1}");
        EQUALS(stage.Lua("seen()"), "first|second:{k=1}");
    });

    IT("registers and removes the handlers of a page event in both languages", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "1");
        EQUALS(stage.Lua("Web.createView('https://x.test/')"), "2");

        for (const char *arguments : {"", "2, 'ev'", "2, 'ev', 'fn'", "'2', 'ev', print", "2, 5, print", "2, 'ev\\xff', print"}) {
            EQUALS(stage.Lua(std::string("Web.on(") + arguments + ")"), "<error> eval:1: Web.on requires 3 arguments: viewId, eventName, handler");
        }
        for (const char *arguments : {"", "1, 'ev'", "1, 'ev', 'fn'", "'1', 'ev', () => {}", "1, 5, () => {}"}) {
            EQUALS(stage.Js(std::string("Web.on(") + arguments + ")"), "<error> Error: Web.on requires 3 arguments: viewId, eventName, handler");
        }
        EQUALS(stage.Lua("Web.on(2, '', print)"), "<error> eval:1: Web.on: eventName must not be empty");
        EQUALS(stage.Js("Web.on(1, '', () => {})"), "<error> Error: Web.on: eventName must not be empty");
        EQUALS(stage.Lua("Web.on(999, 'ev', print)"), "<error> eval:1: Web.on: unknown web view 999");
        EQUALS(stage.Js("Web.on(999, 'ev', () => {})"), "<error> Error: Web.on: unknown web view 999");
        EQUALS(stage.Js("Web.on(1, 'ev', () => {})", ""), "<error> Error: Web.on: must be called from within a resource");
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 0u);
        EQUALS(stage.Listeners(1) + stage.Listeners(2), "");

        const char *lua = "a, b = rec('a'), rec('b') Web.on(2, 'ev', a) Web.on(2, 'ev', a) Web.on(2, 'ev', b) Web.on(2, 'other', b)";
        EQUALS(stage.Lua(std::string("(function() ") + lua + " end)()"), "nil");
        EQUALS(stage.Js("(globalThis.a = rec('a'), globalThis.b = rec('b'), Web.on(1, 'ev', a), Web.on(1, 'ev', a), Web.on(1, 'ev', b), Web.on(1, 'other', b))"), "undefined");
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 8u);
        // One listener a name, whatever the number of handlers.
        for (const int id : {1, 2}) {
            EQUALS(stage.Listeners(id), "ev|other|");
            EQUALS(std::count(stage.View(id)->calls.begin(), stage.View(id)->calls.end(), "AddEventListener"), 2);
        }

        const auto both = [&stage](const std::string &lua, const std::string &js) {
            return stage.Lua(lua) + " / " + stage.Js(js);
        };
        const auto fired = [&stage](const std::string &name) {
            stage.Page(1, name, "1");
            stage.Page(2, name, "1");
            return stage.Lua("seen()") + " / " + stage.Js("seen()");
        };
        EQUALS(fired("ev"), ClientWebTest::Pair("a:1|a:1|b:1"));

        EQUALS(both("Web.off(2, 'none')", "Web.off(1, 'none')"), ClientWebTest::Pair("false"));
        EQUALS(both("Web.off(999, 'ev')", "Web.off(999, 'ev')"), ClientWebTest::Pair("false"));
        EQUALS(both("Web.off(2, 'ev', function() end)", "Web.off(1, 'ev', () => {})"), ClientWebTest::Pair("false"));
        // A handler registered twice goes twice.
        EQUALS(both("Web.off(2, 'ev', a)", "Web.off(1, 'ev', a)"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.off(2, 'ev', a)", "Web.off(1, 'ev', a)"), ClientWebTest::Pair("false"));
        EQUALS(fired("ev"), ClientWebTest::Pair("b:1"));
        EQUALS(stage.Listeners(1) + stage.Listeners(2), "ev|other|ev|other|");
        // The listener of a name goes with its last handler.
        EQUALS(both("Web.off(2, 'ev', b)", "Web.off(1, 'ev', b)"), ClientWebTest::Pair("true"));
        EQUALS(stage.Listeners(1) + stage.Listeners(2), "other|other|");
        EQUALS(fired("ev"), ClientWebTest::Pair(""));
        EQUALS(fired("other"), ClientWebTest::Pair("b:1"));
        // Without a handler, and with what is not a function, every handler of the name goes.
        EQUALS(both("(function() Web.on(2, 'other', a) Web.on(2, 'third', a) end)()", "(Web.on(1, 'other', a), Web.on(1, 'third', a))"), "nil / undefined");
        EQUALS(both("Web.off(2, 'other')", "Web.off(1, 'other')"), ClientWebTest::Pair("true"));
        EQUALS(both("Web.off(2, 'other')", "Web.off(1, 'other')"), ClientWebTest::Pair("false"));
        EQUALS(both("Web.off(2, 'third', 'not a function')", "Web.off(1, 'third', 'not a function')"), ClientWebTest::Pair("true"));
        EQUALS(stage.Listeners(1) + stage.Listeners(2), "");
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 0u);

        for (const char *arguments : {"", "2", "2, 5", "'2', 'ev'"}) {
            EQUALS(stage.Lua(std::string("Web.off(") + arguments + ")"), "<error> eval:1: Web.off requires at least 2 arguments: viewId, eventName");
            EQUALS(stage.Js(std::string("Web.off(") + arguments + ")"), "<error> Error: Web.off requires at least 2 arguments: viewId, eventName");
        }
    });

    IT("lets a handler remove itself, add another and destroy its view while the event is dispatched", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "1");
        EQUALS(stage.Lua("Web.createView('https://x.test/')"), "2");

        EQUALS(stage.Js("(globalThis.once = function () { log.push('once'); Web.off(1, 're', once); Web.on(1, 're', rec('added')); }, Web.on(1, 're', once), Web.on(1, 're', rec('second')))"), "undefined");
        EQUALS(stage.Lua("(function() local once once = function() log[#log + 1] = 'once' Web.off(2, 're', once) Web.on(2, 're', rec('added')) end Web.on(2, 're', once) Web.on(2, 're', rec('second')) end)()"), "nil");
        // The handlers of an event are those it had when it arrived.
        stage.Page(1, "re", "1");
        stage.Page(2, "re", "1");
        EQUALS(stage.Js("seen()"), "once|second:1");
        EQUALS(stage.Lua("seen()"), "once|second:1");
        stage.Page(1, "re", "2");
        stage.Page(2, "re", "2");
        EQUALS(stage.Js("seen()"), "second:2|added:2");
        EQUALS(stage.Lua("seen()"), "second:2|added:2");

        EQUALS(stage.Js("(Web.on(1, 'kill', function () { log.push('destroyed:' + Web.destroyView(1)); }), Web.on(1, 'kill', rec('after')))"), "undefined");
        EQUALS(stage.Lua("(function() Web.on(2, 'kill', function() log[#log + 1] = 'destroyed:' .. tostring(Web.destroyView(2)) end) Web.on(2, 'kill', rec('after')) end)()"), "nil");
        stage.Page(1, "kill", "1");
        stage.Page(2, "kill", "1");
        EQUALS(stage.Js("seen()"), "destroyed:true|after:1");
        EQUALS(stage.Lua("seen()"), "destroyed:true|after:1");
        EQUALS(stage.gui->ViewCount(), 0u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 0u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount(), 0u);
        EQUALS(stage.Errors(), "");
    });

    IT("reports a handler of a page event that fails to the manager of its resource, and calls the others", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        const char *keepGoing = R"(,"errorBehavior":"continue")";
        EQUALS(stage.Start("js-fails", "client.js", "globalThis.failing = Web.createView('https://x.test/'); Web.on(failing, 'ev', (p) => log.push('js-before:' + p)); Web.on(failing, 'ev', () => { throw new Error('js boom'); }); Web.on(failing, 'ev', (p) => log.push('js-after:' + p));", keepGoing), "");
        EQUALS(stage.Start("lua-fails", "client.lua", std::string(ClientWebTest::kLua) + "view = Web.createView('https://x.test/') Web.on(view, 'ev\\nil', rec('lua-before')) Web.on(view, 'ev\\nil', function() error('lua boom') end) Web.on(view, 'ev\\nil', rec('lua-after'))", keepGoing), "");
        EQUALS(stage.Start("later", "client.lua", std::string(ClientWebTest::kLua) + "view = Web.createView('https://x.test/') Web.on(view, 'ev', function() sleep(1) error('late boom') end)", keepGoing), "");
        EQUALS(stage.Start("stops", "client.lua", "view = Web.createView('https://x.test/') Web.on(view, 'ev', function() error('fatal boom') end)"), "");

        stage.Page(1, "ev", "1");
        stage.Page(2, "ev\nil", "1");
        stage.Page(3, "ev", "1");
        // The handlers of a resource after the one that failed wait for the next update; those of the others run.
        stage.Page(1, "ev", "2");
        stage.Page(2, "ev\nil", "2");
        EQUALS(stage.Js("seen()"), "js-before:1");
        EQUALS(stage.Lua("seen()", "lua-fails"), "lua-before:1");
        // Nothing is acted on inside the dispatch: the manager hears of it at its next upkeep.
        EQUALS(stage.Errors(), "");
        stage.Frame();
        EQUALS(LuaRuntimeTest::Contains(stage.Errors(), "js-fails: Web view 1 event 'ev' handler error: Error: js boom"), true);
        // The name of an event is a script's: it takes one line of the log.
        EQUALS(LuaRuntimeTest::Contains(stage.Errors(), "lua-fails: Web view 2 event 'ev il' handler error: "), true);
        EQUALS(LuaRuntimeTest::Contains(stage.Errors(), "lua boom"), true);
        EQUALS(LuaRuntimeTest::Contains(stage.Errors(), "late boom"), false);
        stage.client.Wait(20);
        stage.Frame();
        EQUALS(LuaRuntimeTest::Contains(stage.Errors(), "later: Web view 3 event 'ev' handler error: "), true);
        EQUALS(LuaRuntimeTest::Contains(stage.Errors(), "late boom"), true);
        // A resource that goes on, in the state of one that failed, keeps its views and its handlers.
        for (const char *name : {"js-fails", "lua-fails", "later"}) {
            EQUALS(stage.client.manager->GetResourceState(name) == Framework::Scripting::ResourceState::Error, true);
        }
        EQUALS(stage.gui->ViewCount(), 4u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 8u);
        // The next update starts over.
        stage.Page(1, "ev", "3");
        stage.Page(2, "ev\nil", "3");
        EQUALS(stage.Js("seen()"), "js-before:3");
        EQUALS(stage.Lua("seen()", "lua-fails"), "lua-before:3");
        stage.Frame();

        // By default a failing handler stops its resource, and its views go with it.
        stage.Page(4, "ev", "1");
        EQUALS(stage.client.manager->IsResourceRunning("stops"), true);
        stage.Frame();
        EQUALS(LuaRuntimeTest::Contains(stage.Errors(), "stops: Web view 4 event 'ev' handler error: "), true);
        EQUALS(stage.client.manager->IsResourceRunning("stops"), false);
        EQUALS(stage.View(4) == nullptr, true);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 7u);
    });

    IT("charges a resource whose handlers all hang one deadline an update, whatever its page sends", {
        Framework::Scripting::LuaEngineOptions options = Framework::Scripting::LuaEngineOptions::Client();
        options.entryDeadline    = std::chrono::milliseconds(150);
        ClientWebTest::Stage stage(/*cleanupOnStop*/ true, options);
        EQUALS(stage.ready, true);
        const char *keepGoing  = R"(,"errorBehavior":"continue")";
        const std::string spin = "local function spin() local stop = os.clock() + 20 while os.clock() < stop do end end ";
        EQUALS(stage.Start("hangs", "client.lua", spin + "view = Web.createView('https://x.test/') for i = 1, 20 do Web.on(view, 'ev', spin) end Web.on(view, 'other', spin)", keepGoing), "");
        EQUALS(stage.Start("fine", "client.lua", "ran = 0 view = Web.createView('https://x.test/') Web.on(view, 'ev', function() ran = ran + 1 end)"), "");
        EQUALS(stage.Start("js-fails", "client.js", "globalThis.threw = 0; globalThis.failing = Web.createView('https://x.test/'); for (let i = 0; i < 3; ++i) Web.on(failing, 'ev', () => { ++threw; throw Error('boom'); });", keepGoing), "");

        // The page decides how many events arrive between two updates.
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i) {
            stage.Page(1, "ev", "1");
            stage.Page(1, "other", "1");
            stage.Page(2, "ev", "1");
            stage.Page(3, "ev", "1");
        }
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        EQUALS(took >= 150, true);
        EQUALS(took < 1500, true);
        EQUALS(stage.Lua("ran", "fine"), "5");
        EQUALS(stage.Js("threw", "js-fails"), "1");

        // One report for each resource, and the next update starts over.
        stage.Frame();
        EQUALS(stage.client.errors.size(), 2u);
        stage.Page(3, "ev", "1");
        stage.Page(3, "ev", "1");
        stage.Page(2, "ev", "1");
        EQUALS(stage.Js("threw", "js-fails"), "2");
        EQUALS(stage.Lua("ran", "fine"), "6");
        stage.Frame();
        EQUALS(stage.client.errors.size(), 3u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 25u);
    });

    // ==================== Reentrancy ====================

    IT("calls no later handler of a resource that a handler stopped, in either language", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Start("js-quits", "client.js", "globalThis.quitLog = []; globalThis.quitView = Web.createView('https://x.test/'); Web.on(quitView, 'ev', () => { quitLog.push('quit'); stopResource('js-quits'); quitLog.push('on'); }); Web.on(quitView, 'ev', () => quitLog.push('after'));"), "");
        EQUALS(stage.Start("lua-quits", "client.lua", "view = Web.createView('https://x.test/') Web.on(view, 'ev', function() stopResource('lua-quits') error('still running') end) Web.on(view, 'ev', function() error('after') end)"), "");
        // A handler of another resource on the view of the one that stops: a JavaScript function is its maker's.
        EQUALS(stage.Js("(Web.createView('https://x.test/'), 1)"), "1");
        EQUALS(stage.Start("js-kills", "client.js", "globalThis.victimLog = []; globalThis.victimView = Web.createView('https://x.test/'); Web.on(victimView, 'ev', () => { stopResource('js'); }); Web.on(victimView, 'ev', rec('of-js')); Web.on(victimView, 'ev', () => victimLog.push('mine'));"), "");
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount(), 4u);

        stage.Page(1, "ev", "1");
        // The script that stopped its resource ran on to its end; the handler after it was not called.
        EQUALS(stage.Js("quitLog.join()", ""), "quit,on");
        EQUALS(stage.View(1) == nullptr, true);
        stage.Page(2, "ev", "1");
        EQUALS(stage.View(2) == nullptr, true);
        stage.Page(4, "ev", "1");
        EQUALS(stage.Js("victimLog.join() + '/' + log.length", ""), "mine/0");
        EQUALS(stage.View(3) == nullptr, true);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount(), 1u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 2u);
        // A resource that is stopping has nothing reported.
        stage.Frame();
        EQUALS(stage.Errors(), "");
        EQUALS((stage.client.manager->IsResourceRunning("js-quits") || stage.client.manager->IsResourceRunning("lua-quits") || stage.client.manager->IsResourceRunning("js")), false);
    });

    IT("stops dispatching when a handler shuts Web down, and reports what fails afterwards to nobody", {
        for (const bool lua : {false, true}) {
            ClientWebTest::Stage stage;
            EQUALS(stage.ready, true);
            if (lua) {
                EQUALS(stage.Lua("(function() view = Web.createView('https://x.test/') Web.on(view, 'ev', function() log[#log + 1] = 'first' shutdownWeb() error('after the shutdown') end) Web.on(view, 'ev', rec('second')) end)()"), "nil");
            }
            else {
                EQUALS(stage.Js("(globalThis.view = Web.createView('https://x.test/'), Web.on(view, 'ev', () => { log.push('first'); shutdownWeb(); throw new Error('after the shutdown'); }), Web.on(view, 'ev', rec('second')))"), "undefined");
            }
            stage.Page(1, "ev", "1");
            EQUALS((lua ? stage.Lua("seen()") : stage.Js("seen()")), "first");
            EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount() + Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 0u);
            // The manager the handler was registered under is no longer Web's to report to.
            stage.Frame();
            stage.Frame();
            EQUALS(stage.Errors(), "");
            EQUALS(stage.client.manager->IsResourceRunning(lua ? "lua" : "js"), true);
            // The listener the view still has finds nobody.
            stage.Page(1, "ev", "2");
            EQUALS((lua ? stage.Lua("seen()") : stage.Js("seen()")), "");
        }
    });

    IT("survives a reset of the scripting from inside a handler, and serves both languages afterwards", {
        for (const bool lua : {false, true}) {
            ClientWebTest::Stage stage;
            EQUALS(stage.ready, true);
            EQUALS(stage.Js("(globalThis.view = Web.createView('https://x.test/'), 1)"), "1");
            EQUALS(stage.Lua("(function() view = Web.createView('https://y.test/') end)()"), "nil");
            EQUALS(stage.Js("(globalThis.reached = [], Web.on(view, 'ev', () => { reached.push('js'); resetScripting(); reached.push('on'); }), Web.on(view, 'ev', () => reached.push('after')))"), "undefined");
            EQUALS(stage.Lua("(function() Web.on(view, 'ev', function() resetScripting() error('after the reset') end) Web.on(view, 'ev', function() error('second') end) end)()"), "nil");
            stage.Page(lua ? 2 : 1, "ev", "1");
            if (!lua) {
                EQUALS(stage.Js("reached.join()", ""), "js,on");
            }
            EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount() + Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 0u);
            EQUALS((stage.client.manager->IsResourceRunning("js") || stage.client.manager->IsResourceRunning("lua")), false);

            // The rest of ClientScriptingModule::Reset, then Init.
            stage.client.lua.Shutdown();
            stage.client.v8.ResetContext();
            EQUALS(stage.Register(), true);
            EQUALS(stage.client.Start("js"), "");
            EQUALS(stage.client.Start("lua"), "");
            EQUALS(stage.Js("(globalThis.view = Web.createView('https://x.test/'), Web.on(view, 'ev', rec('js')), view)"), "3");
            EQUALS(stage.Lua("(function() view = Web.createView('https://y.test/') Web.on(view, 'ev', rec('lua')) return view end)()"), "4");
            stage.Page(3, "ev", "1");
            stage.Page(4, "ev", "1");
            EQUALS(stage.Js("seen()"), "js:1");
            EQUALS(stage.Lua("seen()"), "lua:1");
            stage.Frame();
            EQUALS(stage.Errors(), "");
        }
    });

    // ==================== Cleanup ====================

    IT("destroys the views of a resource that stops and drops its handlers, in both languages", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Start("js-other", "client.js", "globalThis.otherHandler = function (p) { log.push('other:' + show(p)); }; globalThis.otherView = Web.createView('https://o.test/');"), "");
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "2");
        EQUALS(stage.Js("Web.createView('https://x.test/2')"), "3");
        EQUALS(stage.Lua("Web.createView('https://y.test/')"), "4");
        EQUALS(stage.Lua("Web.createView('https://y.test/2')"), "5");
        EQUALS(stage.Js("(Web.on(2, 'ev', rec('js')), Web.on(3, 'ev', rec('js')))"), "undefined");
        EQUALS(stage.Lua("(function() Web.on(4, 'ev', rec('lua')) Web.on(5, 'ev', rec('lua')) end)()"), "nil");
        // A JavaScript function is its maker's, whoever registers it: here on a view of the one that registers.
        EQUALS(stage.Js("(Web.on(2, 'shared', otherHandler), Web.on(2, 'mixed', otherHandler), Web.on(2, 'mixed', rec('js')))"), "undefined");
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount(), 5u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 7u);
        EQUALS(stage.Listeners(2), "ev|mixed|shared|");
        // Events of the browser that nobody drained yet.
        stage.Raise(4, Framework::GUI::ViewEvent::Tooltip);
        stage.Raise(1, Framework::GUI::ViewEvent::Tooltip);

        EQUALS(static_cast<bool>(stage.client.manager->StopResource("lua")), true);
        EQUALS((stage.View(4) == nullptr && stage.View(5) == nullptr), true);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount(), 3u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 5u);

        // The handlers of the resource that stops go from the views of the others, and a listener with its last.
        EQUALS(static_cast<bool>(stage.client.manager->StopResource("js-other")), true);
        EQUALS(stage.View(1) == nullptr, true);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount(), 2u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 3u);
        EQUALS(stage.Listeners(2), "ev|mixed|");
        stage.Page(2, "mixed", "1");
        EQUALS(stage.Js("seen()"), "js:1");

        EQUALS(static_cast<bool>(stage.client.manager->StopResource("js")), true);
        EQUALS(stage.gui->ViewCount(), 0u);
        EQUALS(stage.gui->destroyed.size(), 5u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount(), 0u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 0u);
        // What was queued for views that are gone reaches nobody.
        stage.Frame();
        EQUALS(stage.Errors(), "");

        // A resource that starts again starts from nothing.
        EQUALS(stage.client.Start("lua"), "");
        EQUALS(stage.Lua("Web.showView(4)"), "false");
        EQUALS(stage.Lua("Web.createView('https://y.test/')"), "6");
    });

    IT("never calls a Lua handler whose state is gone", {
        // Nobody tells Web that the resource stopped.
        ClientWebTest::Stage stage(false);
        EQUALS(stage.ready, true);
        EQUALS(stage.Lua("Web.createView('https://y.test/')"), "1");
        EQUALS(stage.Lua("Web.on(1, 'ev', rec('lua'))"), "nil");
        EQUALS(static_cast<bool>(stage.client.manager->StopResource("lua")), true);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 1u);
        stage.Page(1, "ev", "1");
        stage.Frame();
        EQUALS(stage.Errors(), "");
        Framework::Integrations::Client::Scripting::Builtins::Web::CleanupResource("lua");
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 0u);
        EQUALS(stage.gui->ViewCount(), 0u);
    });

    IT("forgets every view and handler on a shutdown, and serves both languages again after a reset", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "1");
        EQUALS(stage.Lua("Web.createView('https://y.test/')"), "2");
        EQUALS(stage.Js("Web.on(1, 'ev', rec('js'))"), "undefined");
        EQUALS(stage.Lua("Web.on(2, 'ev', rec('lua'))"), "nil");
        stage.Raise(2, Framework::GUI::ViewEvent::Tooltip);

        Framework::Integrations::Client::Scripting::Builtins::Web::Shutdown();
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::ViewCount(), 0u);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::HandlerCount(), 0u);
        // The views are the manager's of the views to collect; scripts no longer reach them.
        stage.Page(1, "ev", "1");
        stage.Page(2, "ev", "1");
        stage.Frame();
        EQUALS(stage.Js("seen()") + stage.Lua("seen()"), "");
        EQUALS(stage.Lua("Web.showView(2)"), "false");
        EQUALS(stage.Js("Web.showView(1)"), "false");
        EQUALS(stage.Lua("Web.emit(2, 'ev')"), "false");
        EQUALS(stage.Lua("Web.createView('https://y.test/')"), "<error> eval:1: Web.createView: resource manager not available");
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "<error> Error: Web.createView: resource manager not available");
        EQUALS(stage.Lua("Web.on(2, 'ev', print)"), "<error> eval:1: Web.on: resource manager not available");
        EQUALS(stage.Js("Web.on(1, 'ev', () => {})"), "<error> Error: Web.on: resource manager not available");
        EQUALS(stage.Lua("show(Web.getScreenSize())"), "{height=1080,width=1920}");

        stage.Reset();
        EQUALS(stage.Register(), true);
        EQUALS(stage.client.Start("js"), "");
        EQUALS(stage.client.Start("lua"), "");
        EQUALS(stage.Js("Web.createView('https://x.test/')"), "3");
        EQUALS(stage.Lua("Web.createView('https://y.test/')"), "4");
        EQUALS(stage.Js("Web.on(3, 'ev', rec('js'))"), "undefined");
        EQUALS(stage.Lua("Web.on(4, 'ev', rec('lua'))"), "nil");
        stage.Page(3, "ev", "1");
        stage.Page(4, "ev", "1");
        EQUALS(stage.Js("seen()"), "js:1");
        EQUALS(stage.Lua("seen()"), "lua:1");
        EQUALS(stage.Errors(), "");
    });

    // ==================== Browser events ====================

    IT("gives the events of a browser to the resource that owns the view, with the same fields in both languages", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        const char *names[] = {"browserCreated", "browserLoadingStart", "browserDocumentReady", "browserLoadingFailed", "browserNavigate", "browserPopup", "browserCursorChange", "browserTooltip", "browserInputFocusChange", "browserResourceBlocked",
            "browserConsoleMessage", "browserOriginChange"};
        for (const char *name : names) {
            EQUALS(stage.Js(std::string("typeof Events.on('") + name + "', (e) => { log.push('" + name + ":' + show(e)); })"), "function");
            EQUALS(stage.Lua(std::string("type(Events.on('") + name + "', function(e) log[#log + 1] = '" + name + ":' .. show(e) end))"), "function");
        }
        EQUALS(stage.Js("Web.createView('https://x.test/index.html')"), "1");
        EQUALS(stage.Lua("Web.createView('https://x.test/index.html')"), "2");

        // Nothing reaches a script before the scripting ticks: the browser reports from inside its own calls.
        EQUALS(stage.Js("seen()") + stage.Lua("seen()"), "");
        stage.Frame();
        // The creation is told again for who could not listen yet, then the lock of the origin.
        EQUALS(stage.Js("seen()"), "browserCreated:{url=\"\",viewId=1}|browserOriginChange:{origin=\"https://x.test\",url=\"https://x.test/index.html\",viewId=1}");
        EQUALS(stage.Lua("seen()"), "browserCreated:{url=\"\",viewId=2}|browserOriginChange:{origin=\"https://x.test\",url=\"https://x.test/index.html\",viewId=2}");

        const char *expected[] = {"browserCreated:{url=\"https://x.test/p\",viewId=#}",
            "browserLoadingStart:{isMainFrame=true,url=\"https://x.test/p\",viewId=#}",
            "browserDocumentReady:{url=\"https://x.test/p\",viewId=#}",
            "browserLoadingFailed:{description=\"ERR_X\",errorCode=-3,isMainFrame=true,url=\"https://x.test/p\",viewId=#}",
            "browserNavigate:{blocked=true,isMainFrame=true,url=\"https://x.test/p\",viewId=#}",
            "browserPopup:{openerUrl=\"https://x.test/o\",url=\"https://x.test/p\",viewId=#}",
            "browserCursorChange:{cursor=\"pointer\",cursorType=2,viewId=#}",
            "browserTooltip:{text=\"tip\",viewId=#}",
            "browserInputFocusChange:{focused=true,viewId=#}",
            "browserResourceBlocked:{domain=\"x.test\",reason=\"host-filter\",url=\"https://x.test/p\",viewId=#}",
            "browserConsoleMessage:{line=12,message=\"msg\",severity=\"error\",source=\"src.js\",viewId=#}",
            "browserOriginChange:{origin=\"https://x.test\",url=\"https://x.test/p\",viewId=#}"};
        const auto with = [](std::string text, char id) {
            text[text.find('#')] = id;
            return text;
        };
        EQUALS(stage.Lua("type(Events.on('browserConsoleMessage', function(e) kinds = math.type(e.line) .. ',' .. math.type(e.viewId) end))"), "function");
        for (int kind = 0; kind < 12; ++kind) {
            stage.Raise(1, ClientWebTest::Filled(static_cast<Framework::GUI::ViewEvent>(kind)));
            stage.Raise(2, ClientWebTest::Filled(static_cast<Framework::GUI::ViewEvent>(kind)));
            stage.Frame();
            EQUALS(stage.Js("seen()"), with(expected[kind], '1'));
            EQUALS(stage.Lua("seen()"), with(expected[kind], '2'));
        }
        // Whole numbers are integers to Lua.
        EQUALS(stage.Lua("kinds"), "integer,integer");

        // The names of a cursor and of a severity, and what has none.
        const auto cursor = [&stage](int type) {
            Framework::GUI::ViewEventData data;
            data.event      = Framework::GUI::ViewEvent::CursorChange;
            data.cursorType = type;
            stage.Raise(2, data);
            stage.Frame();
            return stage.Lua("seen()");
        };
        EQUALS(cursor(CT_POINTER), "browserCursorChange:{cursor=\"default\",cursorType=0,viewId=2}");
        EQUALS(cursor(CT_IBEAM), "browserCursorChange:{cursor=\"text\",cursorType=3,viewId=2}");
        EQUALS(cursor(CT_GRABBING), "browserCursorChange:{cursor=\"grabbing\",cursorType=42,viewId=2}");
        EQUALS(cursor(CT_MIDDLEPANNING), "browserCursorChange:{cursor=\"custom\",cursorType=20,viewId=2}");
        EQUALS(cursor(500), "browserCursorChange:{cursor=\"custom\",cursorType=500,viewId=2}");
        const auto severity = [&stage](int level) {
            Framework::GUI::ViewEventData data;
            data.event    = Framework::GUI::ViewEvent::ConsoleMessage;
            data.severity = level;
            stage.Raise(2, data);
            stage.Frame();
            return stage.Lua("seen()");
        };
        EQUALS(severity(LOGSEVERITY_DEBUG), "browserConsoleMessage:{line=0,message=\"\",severity=\"debug\",source=\"\",viewId=2}");
        EQUALS(severity(LOGSEVERITY_INFO), "browserConsoleMessage:{line=0,message=\"\",severity=\"info\",source=\"\",viewId=2}");
        EQUALS(severity(LOGSEVERITY_WARNING), "browserConsoleMessage:{line=0,message=\"\",severity=\"warning\",source=\"\",viewId=2}");
        EQUALS(severity(LOGSEVERITY_FATAL), "browserConsoleMessage:{line=0,message=\"\",severity=\"fatal\",source=\"\",viewId=2}");
        EQUALS(severity(99), "browserConsoleMessage:{line=0,message=\"\",severity=\"info\",source=\"\",viewId=2}");
        EQUALS(stage.Js("seen()"), "");
        EQUALS(stage.Errors(), "");
    });

    IT("keeps the events of a browser apart from what a page names, bounded, and from views that are gone", {
        ClientWebTest::Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Lua("type(Events.on('browserTooltip', function(e) log[#log + 1] = 'browser:' .. show(e) end))"), "function");
        EQUALS(stage.Js("typeof Events.on('browserTooltip', (e) => { log.push('browser:' + show(e)); })"), "function");
        EQUALS(stage.Lua("Web.createView('https://y.test/')"), "1");
        EQUALS(stage.Lua("Web.on(1, 'browserTooltip', rec('page'))"), "nil");
        stage.Frame();

        // A page that names its event as the browser's reaches the handlers of Web.on and no other.
        stage.Page(1, "browserTooltip", "{\"text\":\"forged\",\"viewId\":1}");
        stage.Frame();
        EQUALS(stage.Lua("seen()"), "page:{text=\"forged\",viewId=1}");
        // And what the browser reports reaches the listeners of the owner alone, not those of Web.on.
        stage.Raise(1, ClientWebTest::Filled(Framework::GUI::ViewEvent::Tooltip));
        stage.Frame();
        EQUALS(stage.Lua("seen()"), "browser:{text=\"tip\",viewId=1}");
        EQUALS(stage.Js("seen()"), "");

        // The queue keeps the last 1024 of what a tick did not drain.
        Framework::GUI::ViewEventData numbered;
        numbered.event = Framework::GUI::ViewEvent::Tooltip;
        for (int i = 0; i < 1030; ++i) {
            numbered.tooltip = std::to_string(i);
            stage.Raise(1, numbered);
        }
        stage.Frame();
        EQUALS(stage.Lua("#log"), "1024");
        EQUALS(stage.Lua("log[1] .. log[#log]"), "browser:{text=\"6\",viewId=1}browser:{text=\"1029\",viewId=1}");
        EQUALS(stage.Lua("seen() ~= ''"), "true");

        // What a view reported before it was destroyed is dropped with it, there and then, and what another
        // view reported stays.
        EQUALS(stage.Lua("Web.createView('https://y.test/2')"), "2");
        stage.Frame();
        EQUALS(stage.Lua("seen()"), "");
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::PendingEventCount(), 0u);
        stage.Raise(1, ClientWebTest::Filled(Framework::GUI::ViewEvent::Tooltip));
        stage.Raise(2, ClientWebTest::Filled(Framework::GUI::ViewEvent::Tooltip));
        stage.Raise(1, ClientWebTest::Filled(Framework::GUI::ViewEvent::Tooltip));
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::PendingEventCount(), 3u);
        EQUALS(stage.Lua("Web.destroyView(1)"), "true");
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::PendingEventCount(), 1u);
        stage.Frame();
        EQUALS(stage.Lua("seen()"), "browser:{text=\"tip\",viewId=2}");
        stage.Raise(2, ClientWebTest::Filled(Framework::GUI::ViewEvent::Tooltip));
        EQUALS(static_cast<bool>(stage.client.manager->StopResource("lua")), true);
        EQUALS(Framework::Integrations::Client::Scripting::Builtins::Web::PendingEventCount(), 0u);
        stage.Frame();
        EQUALS(stage.Errors(), "");
    });
})
