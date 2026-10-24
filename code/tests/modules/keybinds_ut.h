/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <input/window_input.h>
#include <integrations/client/scripting/builtins/keybinds.h>
#include <scripting/engine_scope.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/v8_engine.h>
#include <utils/package/package.h>
#include <utils/vfs.h>

#include <v8pp/convert.hpp>

#include <initializer_list>

namespace KeybindTests {
    using Framework::Integrations::Client::Scripting::Builtins::Keybinds;

    class FocusedInput final: public Framework::Input::WindowInput {
      protected:
        bool HasFocus() const override {
            return true;
        }
        bool ReadKeyDown(int) const override {
            return false;
        }
    };

    inline bool gateOpen = true;

    // +1 is a notch up, -1 a notch down.
    inline void Turn(FocusedInput &input, std::initializer_list<int> notches) {
        for (int direction : notches) {
            input.ProcessEvent(nullptr, WM_MOUSEWHEEL, static_cast<WPARAM>(static_cast<uint16_t>(direction * WHEEL_DELTA)) << 16, 0);
        }
    }

    inline const char *kScript = "globalThis.log = [];"
                                 "const rec = (key, state) => log.push(key + ':' + state);"
                                 "globalThis.reset = () => { Key.unbind('wheelup'); Key.unbind('wheeldown'); log.length = 0; };"
                                 "globalThis.expect = want => { if (log.join() !== want) throw Error(log.join() + ' != ' + want); };"
                                 "globalThis.bindBoth = () => { Key.bind('wheelup', 'both', rec); Key.bind('wheeldown', 'both', rec); };"
                                 "globalThis.bindSelfUnbind = () => { const h = (k, s) => { rec(k, s); Key.unbind('wheeldown', h); }; Key.bind('wheeldown', 'both', h); };"
                                 "globalThis.bindRebind = () => { const h = (k, s) => { rec(k, s); Key.unbind('wheeldown', h); Key.bind('wheeldown', h); }; Key.bind('wheeldown', h); };"
                                 "globalThis.bindGate = () => Key.bind('wheelup', (k, s) => { rec(k, s); closeGate(); });";
} // namespace KeybindTests

MODULE(keybinds, {
    IT("dispatches wheel notches in order, per edge and behind the gate", {
        using namespace KeybindTests;
        auto &vfs = Framework::Utils::Vfs::Get();
        EQUALS(vfs.Init(nullptr), true);
        Framework::Utils::Package::Writer writer;
        writer.Add("package.json", "{\"name\":\"keys\",\"version\":\"1.0.0\",\"mafiahub\":{\"clientScripts\":[\"client.js\"]}}");
        writer.Add("client.js", kScript);
        std::string blob, zip, error;
        EQUALS(writer.Build(nullptr, blob) && Framework::Utils::Package::Open(blob, nullptr, zip, error), true);
        EQUALS(vfs.MountMemory(std::move(zip), "keys.zip", Framework::Utils::Vfs::ResourcePath("keys"), error), true);

        FocusedInput input;
        Framework::CoreModules::SetInput(&input);
        Keybinds::SetActiveCallback([] {
            return gateOpen;
        });
        Framework::Scripting::V8Engine engine;
        engine.SetModuleRootPath(Framework::Utils::Vfs::kResourceMountRoot);
        EQUALS(engine.Init(), Framework::Scripting::ScriptingError::SCRIPTING_NONE);
        {
            Framework::Scripting::ResourceManagerConfig config;
            config.resourcesPath = Framework::Utils::Vfs::kResourceMountRoot;
            config.isClient      = true;
            Framework::Scripting::ResourceManager manager(&engine, config);
            {
                Framework::Scripting::EngineScope scope(&engine);
                const auto context = scope.Context();
                Keybinds::Register(scope.Isolate(), context, context->Global(), &manager);
                const auto closeGate = v8::Function::New(context, [](const v8::FunctionCallbackInfo<v8::Value> &) {
                    gateOpen = false;
                }).ToLocalChecked();
                context->Global()->Set(context, v8pp::to_v8(scope.Isolate(), "closeGate"), closeGate).Check();
            }
            EQUALS(manager.DiscoverResources(), 1u);
            EQUALS(static_cast<bool>(manager.StartAll()), true);

            EQUALS(engine.Execute("bindBoth();"), true);
            Turn(input, {1, -1, 1});
            Keybinds::Update();
            EQUALS(engine.Execute("expect('wheelup:down,wheelup:up,wheeldown:down,wheeldown:up,wheelup:down,wheelup:up'); reset();"), true);

            // Unbinding on down drops both that notch's up and the later notches.
            EQUALS(engine.Execute("bindSelfUnbind();"), true);
            Turn(input, {-1, -1, -1});
            Keybinds::Update();
            EQUALS(engine.Execute("expect('wheeldown:down'); reset();"), true);

            EQUALS(engine.Execute("bindRebind();"), true);
            Turn(input, {-1, -1, -1});
            Keybinds::Update();
            EQUALS(engine.Execute("expect('wheeldown:down,wheeldown:down,wheeldown:down'); reset();"), true);

            // A handler that closes the gate holds back the tick's other notches, also once it reopens.
            EQUALS(engine.Execute("bindGate();"), true);
            Turn(input, {1, 1, 1});
            Keybinds::Update();
            gateOpen = true;
            Keybinds::Update();
            EQUALS(engine.Execute("expect('wheelup:down');"), true);

            // No update saw the window unfocused, yet the notch before the focus loss stays dropped.
            Turn(input, {1});
            input.ProcessEvent(nullptr, WM_KILLFOCUS, 0, 0);
            input.ProcessEvent(nullptr, WM_SETFOCUS, 0, 0);
            Keybinds::Update();
            EQUALS(engine.Execute("expect('wheelup:down'); reset();"), true);

            Keybinds::Shutdown();
            manager.StopAll();
        }
        Keybinds::SetActiveCallback(nullptr);
        Framework::CoreModules::SetInput(nullptr);
        engine.Shutdown();
        vfs.Unmount("keys.zip");
    });
});
