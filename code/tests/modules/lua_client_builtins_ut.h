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
#include "input/input.h"
#include "integrations/client/scripting/builtins/keybinds.h"
#include "integrations/client/scripting/builtins/voice.h"
#include "scripting/engine_scope.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/scripting_catalog.h"
#include "voice/client/voice_client.h"

#include <v8pp/convert.hpp>

#include <lua.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

// The builtins only a client has, Key and Voice, from JavaScript and from Lua: one isolate and one Lua state
// per resource, registered the way ClientScriptingModule registers them. keybinds_ut.h drives Key through a
// window's messages, on Windows; here the input is a fake, so the same dispatch runs on every host.
namespace ClientBuiltinsTest {
    using namespace Framework::Scripting;
    using Framework::Integrations::Client::Scripting::Builtins::Keybinds;
    using Framework::Utils::Vfs;
    using LuaClientTest::Contains;
    using LuaClientTest::Mount;

    // Keys that are held and wheel notches that arrived, as a test says.
    class FakeInput final: public Framework::Input::IInput {
      public:
        bool available = true;
        std::set<int> held;
        std::vector<int> notches;

        // +1 is a notch up, -1 a notch down.
        void Turn(std::initializer_list<int> directions) {
            for (const int direction : directions) {
                notches.push_back(direction > 0 ? FW_KEY_WHEELUP : FW_KEY_WHEELDOWN);
            }
        }

        void Update() override {}
        void SetMousePosition(int, int) override {}
        void GetMousePosition(int &x, int &y) const override {
            x = y = 0;
        }
        void SetMouseVisible(bool) override {}
        bool IsMouseVisible() const override {
            return false;
        }
        void SetMouseLocked(bool) override {}
        bool IsMouseLocked() const override {
            return false;
        }
        void SetInputLocked(bool) override {}
        bool IsInputLocked() const override {
            return false;
        }
        bool IsAvailable() const override {
            return available;
        }
        bool IsKeyDown(int key) const override {
            return held.contains(key);
        }
        bool IsKeyUp(int key) const override {
            return !held.contains(key);
        }
        bool IsKeyPressed(int) const override {
            return false;
        }
        bool IsKeyReleased(int) const override {
            return false;
        }
        bool IsMouseButtonDown(int) const override {
            return false;
        }
        bool IsMouseButtonUp(int) const override {
            return true;
        }
        bool IsMouseButtonPressed(int) const override {
            return false;
        }
        bool IsMouseButtonReleased(int) const override {
            return false;
        }
        uint32_t GetLastWheelNotch() const override {
            return static_cast<uint32_t>(notches.size());
        }
        int GetWheelNotch(uint32_t number) const override {
            return number >= 1 && number <= notches.size() ? notches[number - 1] : 0;
        }
    };

    inline bool gateOpen = true;

    // The scripting of a client with Key and Voice in its isolate and in every Lua state, and a fake input.
    struct Desk final {
        FakeInput input;
        LuaClientTest::Client client;
        std::vector<std::string> mounted;

        // Without `cleanupOnStop` nobody tells Key that a resource stopped, as the module does.
        explicit Desk(bool cleanupOnStop = true) {
            namespace ClientBuiltins = Framework::Integrations::Client::Scripting::Builtins;
            gateOpen                 = true;
            Framework::CoreModules::SetInput(&input);
            Keybinds::SetActiveCallback([] {
                return gateOpen;
            });
            client.lua.AddRuntimeSetup([](LuaRuntime &runtime) {
                ClientBuiltins::Keybinds::RegisterLua(runtime);
                ClientBuiltins::Voice::RegisterLua(runtime);
                lua_pushcfunction(runtime.State(), [](lua_State *) {
                    gateOpen = false;
                    return 0;
                });
                lua_setglobal(runtime.State(), "closeGate");
            });
            ready = client.Init();
            if (!ready) {
                return;
            }
            {
                EngineScope scope(&client.v8);
                const auto context = scope.Context();
                ClientBuiltins::Keybinds::Register(scope.Isolate(), context, context->Global(), client.manager.get());
                ClientBuiltins::Voice::Register(scope.Isolate(), context, context->Global(), client.manager.get());
                const auto closeGate = v8::Function::New(context, [](const v8::FunctionCallbackInfo<v8::Value> &) {
                    gateOpen = false;
                }).ToLocalChecked();
                context->Global()->Set(context, v8pp::to_v8(scope.Isolate(), "closeGate"), closeGate).Check();
            }
            if (cleanupOnStop) {
                client.manager->AddOnResourceStopped([manager = client.manager.get()](const std::string &resourceName) {
                    Keybinds::CleanupResource(manager, resourceName);
                });
            }
        }

        // In the order of ClientScriptingModule::Shutdown.
        ~Desk() {
            if (client.manager) {
                client.manager->StopAll();
                client.manager.reset();
            }
            client.lua.Shutdown();
            Keybinds::Shutdown();
            client.v8.Shutdown();
            Keybinds::SetActiveCallback(nullptr);
            Framework::CoreModules::SetInput(nullptr);
            for (const std::string &name : mounted) {
                Vfs::Get().Unmount(name + ".zip");
            }
        }

        // Mounts and starts a resource of one client script. Empty when it started.
        std::string Start(const std::string &name, const std::string &file, const std::string &source, const std::string &manifest = "") {
            if (const std::string error = Mount(name, "\"clientScripts\":[\"" + file + "\"]" + manifest, {{file, source}}); !error.empty()) {
                return error;
            }
            mounted.push_back(name);
            return client.Start(name);
        }

        // A frame: the keys are polled, then the scripting is updated.
        void Frame() {
            Keybinds::Update();
            client.Update();
        }

        // Runs JavaScript as a script of `resource`. False when it threw.
        bool Run(const std::string &code, const std::string &resource) {
            return client.v8.Execute(code, "/resources/" + resource + "/client.js");
        }

        // The value of a JavaScript expression evaluated as a script of `resource`, as String() spells it, or
        // "<error> " with the name and the message of what it threw.
        std::string Js(const std::string &expression, const std::string &resource) {
            if (!Run("globalThis.__result = (() => { try { return String(" + expression + "); } catch (e) { return '<error> ' + e.name + ': ' + e.message; } })();", resource)) {
                return "<did not run>";
            }
            EngineScope scope(&client.v8);
            const auto context = scope.Context();
            return v8pp::from_v8<std::string>(scope.Isolate(), context->Global()->Get(context, v8pp::to_v8(scope.Isolate(), "__result")).ToLocalChecked());
        }

        std::string Lua(const std::string &resource, const std::string &expression) const {
            return client.Eval(resource, expression);
        }

        bool ready = false;
    };

    // What keybinds_ut.h has a script do, for each language. `seen()` is what the handlers recorded.
    inline const char *kJavaScript = "globalThis.log = [];"
                                     "const rec = (key, state) => log.push(key + ':' + state);"
                                     "globalThis.seen = () => log.join();"
                                     "globalThis.reset = () => { Key.unbind('wheelup'); Key.unbind('wheeldown'); log.length = 0; };"
                                     "globalThis.bindBoth = () => { Key.bind('wheelup', 'both', rec); Key.bind('wheeldown', 'both', rec); };"
                                     "globalThis.bindSelfUnbind = () => { const h = (k, s) => { rec(k, s); Key.unbind('wheeldown', h); }; Key.bind('wheeldown', 'both', h); };"
                                     "globalThis.bindRebind = () => { const h = (k, s) => { rec(k, s); Key.unbind('wheeldown', h); Key.bind('wheeldown', h); }; Key.bind('wheeldown', h); };"
                                     "globalThis.bindGate = () => Key.bind('wheelup', (k, s) => { rec(k, s); closeGate(); });";

    inline const char *kLua = R"(
        log = {}
        local function rec(key, state) log[#log + 1] = key .. ':' .. state end
        function seen() return table.concat(log, ',') end
        function reset() Key.unbind('wheelup') Key.unbind('wheeldown') log = {} end
        function bindBoth() Key.bind('wheelup', 'both', rec) Key.bind('wheeldown', 'both', rec) end
        function bindSelfUnbind() local h h = function(k, s) rec(k, s) Key.unbind('wheeldown', h) end Key.bind('wheeldown', 'both', h) end
        function bindRebind() local h h = function(k, s) rec(k, s) Key.unbind('wheeldown', h) Key.bind('wheeldown', h) end Key.bind('wheeldown', h) end
        function bindGate() Key.bind('wheelup', function(k, s) rec(k, s) closeGate() end) end
    )";

    // The wheel scenario of keybinds_ut.h, with `call` running one of the functions above and giving what
    // `seen()` answers after it. Empty when every step went as it does there, the step that did not otherwise.
    inline std::string WheelScenario(Desk &desk, const std::function<std::string(const std::string &)> &call) {
        const auto expect = [&](const char *step, const std::string &want) {
            const std::string got = call("seen");
            return got == want ? std::string() : std::string(step) + ": " + got;
        };

        call("bindBoth");
        desk.input.Turn({1, -1, 1});
        desk.Frame();
        if (const std::string failed = expect("order", "wheelup:down,wheelup:up,wheeldown:down,wheeldown:up,wheelup:down,wheelup:up"); !failed.empty()) {
            return failed;
        }
        call("reset");

        // Unbinding on down drops both that notch's up and the later notches.
        call("bindSelfUnbind");
        desk.input.Turn({-1, -1, -1});
        desk.Frame();
        if (const std::string failed = expect("self unbind", "wheeldown:down"); !failed.empty()) {
            return failed;
        }
        call("reset");

        call("bindRebind");
        desk.input.Turn({-1, -1, -1});
        desk.Frame();
        if (const std::string failed = expect("rebind", "wheeldown:down,wheeldown:down,wheeldown:down"); !failed.empty()) {
            return failed;
        }
        call("reset");

        // A handler that closes the gate holds back the tick's other notches, also once it reopens.
        call("bindGate");
        desk.input.Turn({1, 1, 1});
        desk.Frame();
        gateOpen = true;
        desk.Frame();
        if (const std::string failed = expect("gate", "wheelup:down"); !failed.empty()) {
            return failed;
        }

        // A notch that arrived while the source was away is not replayed when it is back.
        desk.input.Turn({1});
        desk.input.available = false;
        desk.Frame();
        desk.input.available = true;
        desk.Frame();
        if (const std::string failed = expect("resync", "wheelup:down"); !failed.empty()) {
            return failed;
        }
        call("reset");
        return Keybinds::HandlerCount() == 0 ? std::string() : "handlers left: " + std::to_string(Keybinds::HandlerCount());
    }

    // A voice client that was never attached to a peer: its settings are kept, and no device is opened. On the
    // heap: it holds the playout rings of every speaker, and the tests of a module share one stack frame.
    struct VoiceBooth final {
        std::unique_ptr<Framework::Voice::VoiceClient> owned = std::make_unique<Framework::Voice::VoiceClient>();
        Framework::Voice::VoiceClient &voice                 = *owned;

        VoiceBooth() {
            Framework::CoreModules::SetVoiceClient(&voice);
        }

        ~VoiceBooth() {
            Framework::CoreModules::SetVoiceClient(nullptr);
        }
    };

    inline const char *const kVoiceFunctions[] = {"setEnabled", "isEnabled", "setVolume", "getVolume", "setHearingRange", "getHearingRange", "getRange", "setTier", "getTier", "setPushToTalkKey", "getPushToTalkKey", "setPushToTalkReleaseDelay", "getPushToTalkReleaseDelay",
        "isTalking", "hasMicrophone", "setTransmitMode", "getTransmitMode", "setActivationThreshold", "getActivationThreshold", "getInputLevel", "setNoiseSuppression", "isNoiseSuppressionEnabled", "getInputDevices", "setInputDevice", "getInputDevice"};
} // namespace ClientBuiltinsTest

MODULE(lua_client_builtins, {
    using namespace Framework::Scripting;
    using ClientBuiltinsTest::Contains;
    using ClientBuiltinsTest::Desk;
    using ClientBuiltinsTest::gateOpen;
    using ClientBuiltinsTest::VoiceBooth;
    using Framework::Integrations::Client::Scripting::Builtins::Keybinds;

    // ==================== Key: surface ====================

    IT("gives both languages the same Key and Voice, read-only in Lua", {
        Desk desk;
        EQUALS(desk.ready, true);
        EQUALS(desk.Start("js", "client.js", "0;"), "");
        EQUALS(desk.Start("lua", "client.lua", ""), "");

        for (const char *name : {"getLabel", "bind", "unbind", "isDown"}) {
            EQUALS(desk.Js(std::string("typeof Key.") + name, "js"), "function");
            EQUALS(desk.Lua("lua", std::string("type(Key.") + name + ")"), "function");
        }
        for (const char *name : ClientBuiltinsTest::kVoiceFunctions) {
            EQUALS(desk.Js(std::string("typeof Voice.") + name, "js"), "function");
            EQUALS(desk.Lua("lua", std::string("type(Voice.") + name + ")"), "function");
        }
        EQUALS(desk.Js("Object.keys(Key).length + ' ' + Object.keys(Voice).length", "js"), "4 25");
        EQUALS(desk.Lua("lua", "(function() Key.bind = print end)()"), "<error> eval:1: attempt to set 'bind' on Key");
        EQUALS(desk.Lua("lua", "(function() Voice.getTier = print end)()"), "<error> eval:1: attempt to set 'getTier' on Voice");
    });

    IT("labels a key and refuses what is not one, in both languages", {
        Desk desk;
        EQUALS(desk.Start("js", "client.js", "0;"), "");
        EQUALS(desk.Start("lua", "client.lua", ""), "");

        const std::string label = desk.Js("Key.getLabel('F5')", "js");
        EQUALS(desk.Lua("lua", "Key.getLabel('F5')"), label);
        EQUALS(desk.Lua("lua", "Key.getLabel('f5')"), label);
        EQUALS(label, "F5");

        EQUALS(desk.Js("Key.getLabel()", "js"), "<error> Error: Key.getLabel requires a key name");
        EQUALS(desk.Js("Key.getLabel(5)", "js"), "<error> Error: Key.getLabel requires a key name");
        EQUALS(desk.Js("Key.getLabel('Nope')", "js"), "<error> Error: Key.getLabel: unknown key name 'Nope'");
        EQUALS(desk.Lua("lua", "Key.getLabel()"), "<error> eval:1: Key.getLabel requires a key name");
        EQUALS(desk.Lua("lua", "Key.getLabel(5)"), "<error> eval:1: Key.getLabel requires a key name");
        EQUALS(desk.Lua("lua", "Key.getLabel('Nope')"), "<error> eval:1: Key.getLabel: unknown key name 'Nope'");
    });

    IT("throws the same errors for a wrong bind, unbind or isDown in both languages", {
        Desk desk;
        EQUALS(desk.Start("js", "client.js", "0;"), "");
        EQUALS(desk.Start("lua", "client.lua", ""), "");

        const struct {
            const char *js;
            const char *lua;
            const char *message;
        } cases[] = {
            {"Key.bind('f5')", "Key.bind('f5')", "Key.bind requires (key, state, handler) or (key, handler)"},
            {"Key.bind(5, () => {})", "Key.bind(5, function() end)", "Key.bind requires (key, state, handler) or (key, handler)"},
            {"Key.bind('f5', 5)", "Key.bind('f5', 5)", "Key.bind: expected a handler function"},
            {"Key.bind('f5', 'down')", "Key.bind('f5', 'down')", "Key.bind: expected a handler function"},
            {"Key.bind('f5', 'down', 5)", "Key.bind('f5', 'down', 5)", "Key.bind: expected a handler function"},
            {"Key.bind('f5', 'sideways', () => {})", "Key.bind('f5', 'sideways', function() end)", "Key.bind: state must be 'down', 'up' or 'both'"},
            {"Key.bind('Nope', () => {})", "Key.bind('Nope', function() end)", "Key.bind: unknown key name 'nope'"},
            {"Key.unbind()", "Key.unbind()", "Key.unbind requires (key, state?, handler?)"},
            {"Key.unbind(5)", "Key.unbind(5)", "Key.unbind requires (key, state?, handler?)"},
            {"Key.unbind('f5', 'sideways')", "Key.unbind('f5', 'sideways')", "Key.unbind: state must be 'down', 'up' or 'both'"},
            {"Key.isDown()", "Key.isDown()", "Key.isDown requires a key name"},
            {"Key.isDown({})", "Key.isDown({})", "Key.isDown requires a key name"},
            {"Key.isDown('Nope')", "Key.isDown('Nope')", "Key.isDown: unknown key name 'nope'"},
        };
        for (const auto &item : cases) {
            EQUALS(desk.Js(item.js, "js"), std::string("<error> Error: ") + item.message);
            EQUALS(desk.Lua("lua", item.lua), std::string("<error> eval:1: ") + item.message);
        }
        EQUALS(Keybinds::HandlerCount(), 0u);

        // A key nobody bound, and a key bound by nobody the caller is.
        EQUALS(desk.Js("Key.unbind('f5')", "js"), "false");
        EQUALS(desk.Lua("lua", "Key.unbind('f5')"), "false");

        // Outside a resource there is nobody to own a binding.
        EQUALS(desk.client.v8.Execute("globalThis.__outside = (() => { try { return Key.bind('f5', () => {}); } catch (e) { return e.message; } })();"), true);
        EQUALS(desk.Js("__outside", "js"), "Key.bind: must be called from within a resource");
        EQUALS(Keybinds::HandlerCount(), 0u);
    });

    // ==================== Key: dispatch ====================

    IT("dispatches wheel notches to JavaScript in order, per edge and behind the gate", {
        Desk desk;
        EQUALS(desk.Start("wheels", "client.js", ClientBuiltinsTest::kJavaScript), "");
        EQUALS(ClientBuiltinsTest::WheelScenario(desk,
                   [&](const std::string &function) {
                       return desk.Js(function + "()", "wheels");
                   }),
            "");
    });

    IT("dispatches wheel notches to Lua in order, per edge and behind the gate", {
        Desk desk;
        EQUALS(desk.Start("wheels", "client.lua", ClientBuiltinsTest::kLua), "");
        EQUALS(ClientBuiltinsTest::WheelScenario(desk,
                   [&](const std::string &function) {
                       return desk.Lua("wheels", function + "()");
                   }),
            "");
    });

    IT("fires down, up and both handlers on the edges of a key, in both languages", {
        Desk desk;
        EQUALS(desk.Start("js", "client.js",
                   "globalThis.jsLog = []; Key.bind('F5', (k, s) => jsLog.push('default:' + k + ':' + s)); Key.bind('f5', 'up', (k, s) => jsLog.push('up:' + s)); "
                   "globalThis.jsBound = Key.bind('f5', 'both', function (k, s) { jsLog.push('both:' + s + ':' + arguments.length + ':' + (this === undefined || this === globalThis)); });"),
            "");
        EQUALS(desk.Start("lua", "client.lua", R"(
            log = {}
            Key.bind('f5', function(k, s) log[#log + 1] = 'default:' .. k .. ':' .. s end)
            Key.bind('F5', 'up', function(k, s) log[#log + 1] = 'up:' .. s end)
            bound = Key.bind('f5', 'both', function(...) log[#log + 1] = 'both:' .. select(2, ...) .. ':' .. select('#', ...) end)
        )"),
            "");
        EQUALS(desk.Js("jsBound", "js"), "true");
        EQUALS(desk.Lua("lua", "bound"), "true");
        EQUALS(Keybinds::HandlerCount(), 6u);

        desk.Frame();
        EQUALS(desk.Js("jsLog.join()", "js"), "");

        desk.input.held.insert(FW_KEY_F5);
        desk.Frame();
        // Held: one press is one edge.
        desk.Frame();
        EQUALS(desk.Js("jsLog.join()", "js"), "default:f5:down,both:down:2:true");
        EQUALS(desk.Lua("lua", "table.concat(log, ',')"), "default:f5:down,both:down:2");
        EQUALS(desk.Js("Key.isDown('F5') + ' ' + Key.isDown('f6')", "js"), "true false");
        EQUALS(desk.Lua("lua", "tostring(Key.isDown('F5')) .. ' ' .. tostring(Key.isDown('f6'))"), "true false");

        desk.input.held.erase(FW_KEY_F5);
        desk.Frame();
        EQUALS(desk.Js("jsLog.slice(2).join()", "js"), "up:up,both:up:2:true");
        EQUALS(desk.Lua("lua", "table.concat(log, ',', 3)"), "up:up,both:up:2");
    });

    IT("keeps quiet behind the gate, for a key held when it was bound and for a chord with Alt", {
        Desk desk;
        EQUALS(desk.Start("lua", "client.lua", "log = {} function watch(key) return Key.bind(key, 'both', function(k, s) log[#log + 1] = k .. ':' .. s end) end function seen() return table.concat(log, ',') end"), "");
        EQUALS(desk.Start("js", "client.js", "0;"), "");

        // Bound while it is held: the press was not for this handler, the release is.
        desk.input.held.insert(FW_KEY_F5);
        EQUALS(desk.Lua("lua", "watch('f5')"), "true");
        desk.Frame();
        EQUALS(desk.Lua("lua", "seen()"), "");
        desk.input.held.erase(FW_KEY_F5);
        desk.Frame();
        EQUALS(desk.Lua("lua", "seen()"), "f5:up");

        // Behind the gate an edge is seen and not told, and the key reads as up.
        gateOpen = false;
        desk.input.held.insert(FW_KEY_F5);
        desk.Frame();
        EQUALS(desk.Lua("lua", "tostring(Key.isDown('f5'))"), "false");
        EQUALS(desk.Js("Key.isDown('f5')", "js"), "false");
        gateOpen = true;
        desk.Frame();
        EQUALS(desk.Lua("lua", "seen()"), "f5:up");
        EQUALS(desk.Lua("lua", "tostring(Key.isDown('f5'))"), "true");
        desk.input.held.erase(FW_KEY_F5);
        desk.Frame();
        EQUALS(desk.Lua("lua", "seen()"), "f5:up,f5:up");

        // A press while Alt is held is the system's: neither it nor its release is told.
        desk.input.held.insert(FW_KEY_MENU);
        desk.Frame();
        desk.input.held.insert(FW_KEY_F5);
        desk.Frame();
        desk.input.held.erase(FW_KEY_F5);
        desk.Frame();
        desk.input.held.erase(FW_KEY_MENU);
        desk.Frame();
        EQUALS(desk.Lua("lua", "seen()"), "f5:up,f5:up");

        // With no source of input nothing is down.
        Framework::CoreModules::SetInput(nullptr);
        desk.Frame();
        EQUALS(desk.Lua("lua", "tostring(Key.isDown('f5'))"), "false");
        Framework::CoreModules::SetInput(&desk.input);
    });

    // ==================== Key: ownership ====================

    IT("unbinds only the calling resource's handlers, across the two languages", {
        Desk desk;
        EQUALS(desk.Start("alpha", "client.js", "globalThis.ran = []; Key.bind('f5', () => ran.push('a'));"), "");
        EQUALS(desk.Start("bravo", "client.lua", "ran = 0 Key.bind('f5', function() ran = ran + 1 end)"), "");
        EQUALS(desk.Start("charlie", "client.js", "Key.bind('f5', () => ran.push('c'));"), "");
        EQUALS(Keybinds::HandlerCount(), 3u);

        EQUALS(desk.Lua("bravo", "tostring(Key.unbind('F5')) .. tostring(Key.unbind('f5'))"), "truefalse");
        EQUALS(desk.Js("Key.unbind('f5') + ' ' + Key.unbind('f5')", "charlie"), "true false");
        EQUALS(Keybinds::HandlerCount(), 1u);

        desk.input.held.insert(FW_KEY_F5);
        desk.Frame();
        EQUALS(desk.Js("ran.join()", "alpha"), "a");
        EQUALS(desk.Lua("bravo", "ran"), "0");
    });

    IT("unbinds by state and by handler, in both languages", {
        Desk desk;
        EQUALS(desk.Start("js", "client.js", "globalThis.one = () => {}; globalThis.two = () => {}; globalThis.fill = () => { Key.bind('f5', one); Key.bind('f5', 'up', one); Key.bind('f5', 'up', two); Key.bind('f6', two); };"), "");
        EQUALS(desk.Start("lua", "client.lua", "function one() end function two() end function fill() Key.bind('f5', one) Key.bind('f5', 'up', one) Key.bind('f5', 'up', two) Key.bind('f6', two) end"), "");

        const auto count = [] {
            return Keybinds::HandlerCount();
        };
        const auto js = [&](const std::string &expression) {
            return desk.Js(expression, "js");
        };
        const auto lua = [&](const std::string &expression) {
            return desk.Lua("lua", "tostring(" + expression + ")");
        };
        for (const auto &run : {std::function<std::string(const std::string &)>(js), std::function<std::string(const std::string &)>(lua)}) {
            run("fill()");
            EQUALS(count(), 4u);
            // The state alone, then the handler alone, in either order after the key.
            EQUALS(run("Key.unbind('f5', 'up')"), "true");
            EQUALS(count(), 2u);
            EQUALS(run("Key.unbind('f5', two)"), "false");
            EQUALS(run("Key.unbind('f5', one)"), "true");
            EQUALS(count(), 1u);
            run("fill()");
            EQUALS(run("Key.unbind('f5', one, 'up')"), "true");
            EQUALS(count(), 4u);
            EQUALS(run("Key.unbind('f5', 'down', two)"), "false");
            EQUALS(run("Key.unbind('f5', 'both')"), "false");
            EQUALS(run("Key.unbind('f5')"), "true");
            EQUALS(run("Key.unbind('f6')"), "true");
            EQUALS(count(), 0u);
        }
    });

    IT("drops the handlers of a resource that stops, whichever language bound them", {
        Desk desk;
        EQUALS(desk.Start("alpha", "client.js", "globalThis.aRan = 0; globalThis.aHandler = () => ++aRan; Key.bind('f5', aHandler); Key.bind('wheelup', aHandler);"), "");
        EQUALS(desk.Start("bravo", "client.lua", "Key.bind('f5', function() end) Key.bind('f6', 'both', function() end)"), "");
        EQUALS(desk.Start("charlie", "client.js", "globalThis.cRan = 0; Key.bind('f5', () => ++cRan);"), "");
        // In the one isolate a handler belongs to the resource whose script made it, whoever binds it.
        EQUALS(desk.Js("Key.bind('f6', aHandler)", "charlie"), "true");
        EQUALS(Keybinds::HandlerCount(), 6u);

        EQUALS(static_cast<bool>(desk.client.manager->StopResource("bravo")), true);
        EQUALS(Keybinds::HandlerCount(), 4u);
        EQUALS(static_cast<bool>(desk.client.manager->StopResource("alpha")), true);
        EQUALS(Keybinds::HandlerCount(), 1u);

        desk.input.held.insert(FW_KEY_F5);
        desk.input.held.insert(FW_KEY_F6);
        desk.input.Turn({1});
        desk.Frame();
        EQUALS(desk.Js("aRan + ' ' + cRan", "charlie"), "0 1");

        // The same name in another manager is another resource.
        Keybinds::CleanupResource(nullptr, "nobody");
        ResourceManager other(&desk.client.v8, {});
        Keybinds::CleanupResource(&other, "charlie");
        EQUALS(Keybinds::HandlerCount(), 1u);
        Keybinds::CleanupResource("charlie");
        EQUALS(Keybinds::HandlerCount(), 0u);
    });

    IT("never calls a Lua handler whose state is gone, and lets go of it", {
        // Nobody tells Key of the stop: the handler is found dead when its key is next used.
        Desk desk(/*cleanupOnStop*/ false);
        EQUALS(desk.Start("gone", "client.lua", "Key.bind('f5', function() error('called after the stop') end)"), "");
        EQUALS(desk.Start("stays", "client.lua", "ran = 0 Key.bind('f5', function() ran = ran + 1 end)"), "");
        EQUALS(static_cast<bool>(desk.client.manager->StopResource("gone")), true);
        EQUALS(Keybinds::HandlerCount(), 2u);

        desk.input.held.insert(FW_KEY_F5);
        desk.Frame();
        desk.Frame();
        EQUALS(Keybinds::HandlerCount(), 1u);
        EQUALS(desk.Lua("stays", "ran"), "1");
        EQUALS(desk.client.errors.size(), 0u);
    });

    IT("forgets every binding on a shutdown and keeps the gate of the host", {
        Desk desk;
        EQUALS(desk.Start("js", "client.js", "globalThis.ran = 0; globalThis.bindOne = () => Key.bind('f5', () => ++ran); bindOne();"), "");
        EQUALS(desk.Start("lua", "client.lua", "ran = 0 function again() return Key.bind('f5', function() ran = ran + 1 end) end again()"), "");
        EQUALS(Keybinds::HandlerCount(), 2u);

        Keybinds::Shutdown();
        EQUALS(Keybinds::HandlerCount(), 0u);
        desk.input.held.insert(FW_KEY_F5);
        desk.Frame();
        EQUALS(desk.Js("ran", "js"), "0");
        EQUALS(desk.Lua("lua", "ran"), "0");
        desk.input.held.erase(FW_KEY_F5);
        desk.Frame();

        // What binds afterwards is behind the gate the host installed before.
        EQUALS(desk.Js("bindOne()", "js"), "true");
        EQUALS(desk.Lua("lua", "again()"), "true");
        gateOpen = false;
        desk.input.held.insert(FW_KEY_F5);
        desk.Frame();
        EQUALS(desk.Js("ran", "js"), "0");
        EQUALS(desk.Lua("lua", "ran"), "0");
        gateOpen = true;
        desk.input.held.erase(FW_KEY_F5);
        desk.Frame();
        desk.input.held.insert(FW_KEY_F5);
        desk.Frame();
        EQUALS(desk.Js("ran", "js"), "1");
        EQUALS(desk.Lua("lua", "ran"), "1");
    });

    // ==================== Key: failures ====================

    IT("reports a handler that fails as an error of its resource, and goes on with the others", {
        Desk desk;
        EQUALS(desk.Start("js-fails", "client.js", "Key.bind('f5', () => { throw Error('js boom'); });", R"(,"errorBehavior":"continue")"), "");
        EQUALS(desk.Start("lua-fails", "client.lua", "Key.bind('f5', function() error('lua boom') end)", R"(,"errorBehavior":"continue")"), "");
        EQUALS(desk.Start("later", "client.js", "Key.bind('f5', 'up', async () => { await null; throw Error('late boom'); });", R"(,"errorBehavior":"continue")"), "");
        EQUALS(desk.Start("fine", "client.lua", "ran = 0 Key.bind('f5', 'both', function() ran = ran + 1 end)"), "");

        desk.input.held.insert(FW_KEY_F5);
        desk.Frame();
        // Reported at the next upkeep of the manager, outside any script.
        desk.Frame();
        EQUALS(desk.Lua("fine", "ran"), "1");
        EQUALS(desk.client.errors.size(), 2u);
        const auto reported = [&](const std::string &resource, const std::string &text) {
            return std::any_of(desk.client.errors.begin(), desk.client.errors.end(), [&](const std::string &error) {
                return error.starts_with(resource + ": Key 'f5' (") && Contains(error, text);
            });
        };
        EQUALS(reported("js-fails", "down) handler error: Error: js boom"), true);
        EQUALS(reported("lua-fails", "down) handler error: lua-fails/client.lua:1: lua boom"), true);

        // A handler that fails after it returned is reported when what it waited on fails.
        desk.input.held.erase(FW_KEY_F5);
        desk.Frame();
        desk.Frame();
        desk.Frame();
        EQUALS(reported("later", "up) handler error: Error: late boom"), true);
        EQUALS(desk.Lua("fine", "ran"), "2");

        // Left running, as the manifests ask: the bindings are still there.
        EQUALS(Keybinds::HandlerCount(), 4u);
    });

    IT("stops a resource whose handler fails when its manifest says so", {
        Desk desk;
        EQUALS(desk.Start("fragile", "client.lua", "Key.bind('f5', 'both', function() error('lua boom') end)", R"(,"errorBehavior":"stop")"), "");
        desk.input.held.insert(FW_KEY_F5);
        desk.Frame();
        desk.Frame();
        EQUALS(desk.client.errors.size(), 1u);
        EQUALS(desk.client.HasRuntime("fragile"), false);

        desk.input.held.erase(FW_KEY_F5);
        desk.Frame();
        desk.Frame();
        EQUALS(Keybinds::HandlerCount(), 0u);
        EQUALS(desk.client.errors.size(), 1u);
    });

    // ==================== Voice ====================

    IT("answers with the defaults and takes every setting in silence while there is no voice client", {
        Desk desk;
        EQUALS(desk.Start("js", "client.js", "0;"), "");
        EQUALS(desk.Start("lua", "client.lua", ""), "");

        const struct {
            const char *call;
            const char *js;
            const char *lua;
        } getters[] = {
            {"Voice.isEnabled()", "false", "false"},
            {"Voice.getVolume()", "1", "1.0"},
            {"Voice.getHearingRange()", "0", "0.0"},
            {"Voice.getRange()", "15", "15.0"},
            {"Voice.getTier()", "1", "1"},
            {"Voice.getPushToTalkKey()", "v", "v"},
            {"Voice.getPushToTalkReleaseDelay()", "100", "100"},
            {"Voice.isTalking()", "false", "false"},
            {"Voice.hasMicrophone()", "false", "false"},
            {"Voice.getTransmitMode()", "pushToTalk", "pushToTalk"},
            {"Voice.getInputLevel()", "0", "0.0"},
            {"Voice.isNoiseSuppressionEnabled()", "false", "false"},
            {"Voice.getInputDevice()", "", ""},
        };
        for (const auto &getter : getters) {
            EQUALS(desk.Js(getter.call, "js"), getter.js);
            EQUALS(desk.Lua("lua", getter.call), getter.lua);
        }
        EQUALS(desk.Js("Math.abs(Voice.getActivationThreshold() - 0.03) < 1e-6", "js"), "true");
        EQUALS(desk.Lua("lua", "math.abs(Voice.getActivationThreshold() - 0.03) < 1e-6"), "true");
        EQUALS(desk.Js("Array.isArray(Voice.getInputDevices()) + ' ' + Voice.getInputDevices().length", "js"), "true 0");
        EQUALS(desk.Lua("lua", "type(Voice.getInputDevices()) .. ' ' .. #Voice.getInputDevices()"), "table 0");

        for (const char *setter : {"Voice.setEnabled(true)", "Voice.setVolume(2)", "Voice.setHearingRange(5)", "Voice.setTier(2)", "Voice.setPushToTalkKey('f5')", "Voice.setPushToTalkReleaseDelay(50)", "Voice.setTransmitMode('voiceActivity')", "Voice.setActivationThreshold(0.5)",
                 "Voice.setNoiseSuppression(true)", "Voice.setInputDevice('Microphone')"}) {
            EQUALS(desk.Js(setter, "js"), "undefined");
            EQUALS(desk.Lua("lua", std::string("select('#', ") + setter + ")"), "0");
        }
        EQUALS(desk.Js("Voice.isEnabled() + ' ' + Voice.getTier() + ' ' + Voice.getTransmitMode()", "js"), "false 1 pushToTalk");
    });

    IT("sets and reads the voice settings of the local player from both languages", {
        Desk desk;
        const VoiceBooth booth;
        EQUALS(desk.Start("js", "client.js", "0;"), "");
        EQUALS(desk.Start("lua", "client.lua", ""), "");
        const auto js = [&](const std::string &expression) {
            return desk.Js(expression, "js");
        };
        const auto lua = [&](const std::string &expression) {
            return desk.Lua("lua", expression);
        };

        // Each setting is written in one language and read in both.
        EQUALS(js("Voice.isEnabled()"), "true");
        lua("Voice.setEnabled(false)");
        EQUALS(js("Voice.isEnabled()"), "false");
        js("Voice.setEnabled(1)");
        EQUALS(lua("Voice.isEnabled()"), "true");
        EQUALS(booth.voice.IsEnabled(), true);

        js("Voice.setVolume(2.5)");
        EQUALS(lua("Voice.getVolume()"), "2.5");
        lua("Voice.setVolume(9)");
        EQUALS(js("Voice.getVolume()"), "4");

        lua("Voice.setHearingRange(12.5)");
        EQUALS(js("Voice.getHearingRange()"), "12.5");
        js("Voice.setHearingRange(-3)");
        EQUALS(lua("Voice.getHearingRange()"), "0.0");

        lua("Voice.setTier(2)");
        EQUALS(js("Voice.getTier()"), "2");
        js("Voice.setTier(0)");
        EQUALS(lua("Voice.getTier()"), "0");
        EQUALS(lua("math.type(Voice.getTier())"), "integer");

        js("Voice.setPushToTalkKey('F5')");
        EQUALS(lua("Voice.getPushToTalkKey()"), "f5");
        lua("Voice.setPushToTalkKey('LShift')");
        EQUALS(js("Voice.getPushToTalkKey()"), "lshift");

        lua("Voice.setPushToTalkReleaseDelay(250.9)");
        EQUALS(js("Voice.getPushToTalkReleaseDelay()"), "250");
        js("Voice.setPushToTalkReleaseDelay(1e300)");
        EQUALS(lua("Voice.getPushToTalkReleaseDelay()"), "2000");
        lua("Voice.setPushToTalkReleaseDelay(-5)");
        EQUALS(js("Voice.getPushToTalkReleaseDelay()"), "0");
        js("Voice.setPushToTalkReleaseDelay(NaN)");
        EQUALS(js("Voice.getPushToTalkReleaseDelay()"), "0");

        js("Voice.setTransmitMode('voiceActivity')");
        EQUALS(lua("Voice.getTransmitMode()"), "voiceActivity");
        lua("Voice.setTransmitMode('pushToTalk')");
        EQUALS(js("Voice.getTransmitMode()"), "pushToTalk");

        lua("Voice.setActivationThreshold(0.25)");
        EQUALS(js("Voice.getActivationThreshold()"), "0.25");
        js("Voice.setActivationThreshold(7)");
        EQUALS(lua("Voice.getActivationThreshold()"), "1.0");

        // A flag is what the language of the script takes for true.
        js("Voice.setNoiseSuppression(true)");
        EQUALS(lua("Voice.isNoiseSuppressionEnabled()"), "true");
        js("Voice.setNoiseSuppression(0)");
        EQUALS(lua("Voice.isNoiseSuppressionEnabled()"), "false");
        lua("Voice.setNoiseSuppression(0)");
        EQUALS(js("Voice.isNoiseSuppressionEnabled()"), "true");
        lua("Voice.setNoiseSuppression(nil)");
        EQUALS(js("Voice.isNoiseSuppressionEnabled()"), "false");
        js("Voice.setNoiseSuppression('')");
        js("Voice.setNoiseSuppression('yes')");
        EQUALS(lua("Voice.isNoiseSuppressionEnabled()"), "true");

        // Outside a session there is no microphone to list, to choose or to hear.
        EQUALS(js("Voice.getInputDevices().length + ':' + Voice.getInputDevice()"), "0:");
        EQUALS(lua("#Voice.getInputDevices() .. ':' .. Voice.getInputDevice()"), "0:");
        lua("Voice.setInputDevice('Microphone')");
        EQUALS(js("Voice.getInputDevice()"), "");
        EQUALS(js("Voice.isTalking() + ' ' + Voice.hasMicrophone() + ' ' + Voice.getInputLevel() + ' ' + Voice.getRange()"), "false false 0 15");
        EQUALS(lua("tostring(Voice.isTalking()) .. ' ' .. tostring(Voice.hasMicrophone()) .. ' ' .. Voice.getInputLevel() .. ' ' .. Voice.getRange()"), "false false 0.0 15.0");
    });

    IT("throws the same errors for a wrong voice setting in both languages, and changes nothing", {
        Desk desk;
        const VoiceBooth booth;
        EQUALS(desk.Start("js", "client.js", "0;"), "");
        EQUALS(desk.Start("lua", "client.lua", ""), "");

        const struct {
            const char *call;
            const char *message;
        } cases[] = {
            {"Voice.setEnabled()", "Voice.setEnabled: expected (enabled)"},
            {"Voice.setVolume()", "Voice.setVolume: expected (volume)"},
            {"Voice.setVolume('2')", "Voice.setVolume: expected (volume)"},
            {"Voice.setHearingRange()", "Voice.setHearingRange: expected (range)"},
            {"Voice.setHearingRange('5')", "Voice.setHearingRange: expected (range)"},
            {"Voice.setTier()", "Voice.setTier: expected (tier), 0 (whisper), 1 (normal) or 2 (shout)"},
            {"Voice.setTier(3)", "Voice.setTier: expected (tier), 0 (whisper), 1 (normal) or 2 (shout)"},
            {"Voice.setTier(0.5)", "Voice.setTier: expected (tier), 0 (whisper), 1 (normal) or 2 (shout)"},
            {"Voice.setTier('1')", "Voice.setTier: expected (tier), 0 (whisper), 1 (normal) or 2 (shout)"},
            {"Voice.setPushToTalkKey()", "Voice.setPushToTalkKey: expected (key)"},
            {"Voice.setPushToTalkKey(86)", "Voice.setPushToTalkKey: expected (key)"},
            {"Voice.setPushToTalkKey('Nope')", "Voice.setPushToTalkKey: unknown key name 'Nope'"},
            {"Voice.setPushToTalkKey('WheelUp')", "Voice.setPushToTalkKey: 'WheelUp' is never held, so it cannot be push-to-talk"},
            {"Voice.setPushToTalkReleaseDelay()", "Voice.setPushToTalkReleaseDelay: expected (milliseconds)"},
            {"Voice.setPushToTalkReleaseDelay('50')", "Voice.setPushToTalkReleaseDelay: expected (milliseconds)"},
            {"Voice.setTransmitMode()", "Voice.setTransmitMode: expected (mode)"},
            {"Voice.setTransmitMode(1)", "Voice.setTransmitMode: expected (mode)"},
            {"Voice.setTransmitMode('always')", "Voice.setTransmitMode: unknown mode 'always'; expected 'pushToTalk' or 'voiceActivity'"},
            {"Voice.setActivationThreshold()", "Voice.setActivationThreshold: expected (level)"},
            {"Voice.setActivationThreshold('0.5')", "Voice.setActivationThreshold: expected (level)"},
            {"Voice.setNoiseSuppression()", "Voice.setNoiseSuppression: expected (enabled)"},
            {"Voice.setInputDevice()", "Voice.setInputDevice: expected (name)"},
            {"Voice.setInputDevice(1)", "Voice.setInputDevice: expected (name)"},
        };
        for (const auto &item : cases) {
            EQUALS(desk.Js(item.call, "js"), std::string("<error> Error: ") + item.message);
            EQUALS(desk.Lua("lua", item.call), std::string("<error> eval:1: ") + item.message);
        }

        EQUALS(booth.voice.IsEnabled(), true);
        EQUALS(booth.voice.GetMasterVolume(), 1.0f);
        EQUALS(booth.voice.GetHearingRange(), 0.0f);
        EQUALS(booth.voice.GetTier() == Framework::Voice::VoiceTier::Normal, true);
        EQUALS(booth.voice.GetPushToTalkKey(), Framework::Voice::kDefaultPushToTalkKey);
        EQUALS(booth.voice.GetPushToTalkReleaseDelay(), Framework::Voice::kDefaultPushToTalkReleaseMs);
        EQUALS(booth.voice.GetTransmitMode() == Framework::Voice::TransmitMode::PushToTalk, true);
        EQUALS(booth.voice.IsNoiseSuppressionEnabled(), false);
    });
})
