/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "key_labels_ut.h"

#include <integrations/client/scripting/builtins/keybinds.h>
#include <scripting/node_engine.h>

MODULE(keybind_labels, {
    Framework::Scripting::NodeEngine engine({});
    const bool ready = engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE;
    IT("starts an isolate for the public Key API", { EQUALS(ready, true); });
    if (ready) {
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);
            Framework::Integrations::Client::Scripting::Builtins::Keybinds::Register(isolate, context, context->Global(), nullptr);
        }
        Framework::Integrations::Client::Scripting::Builtins::Keybinds::SetActiveCallback([] {
            return false;
        });

        IT("returns labels without a resource or input source and while input is gated", {
            EQUALS(engine.Execute(R"JS(
                if (Key.getLabel("ReTuRn") !== "Enter") throw Error("alias");
                if (Key.getLabel("mouse3") !== "Mouse 3") throw Error("mouse");
                if (Key.isDown("y") !== false) throw Error("gate");
            )JS"),
                true);
        });
        IT("rejects missing, non-string and unknown names", {
            EQUALS(engine.Execute(R"JS(
                for (const args of [[], [null], [42], [{}], [""], ["unknown"]]) {
                    let threw = false;
                    try { Key.getLabel(...args); } catch (e) { threw = true; }
                    if (!threw) throw Error("accepted invalid key");
                }
            )JS"),
                true);
        });
        IT("refreshes the public label when the keyboard layout changes", {
            KeyLabelTests::Layout english(L"00000409");
            KeyLabelTests::Layout german(L"00000407");
            ActivateKeyboardLayout(english.value, 0);
            EQUALS(engine.Execute("if (Key.getLabel('y') !== 'Y') throw Error('QWERTY');"), true);
            ActivateKeyboardLayout(german.value, 0);
            EQUALS(engine.Execute("if (Key.getLabel('y') !== 'Z') throw Error('QWERTZ');"), true);
        });

        Framework::Integrations::Client::Scripting::Builtins::Keybinds::Shutdown();
        Framework::Integrations::Client::Scripting::Builtins::Keybinds::SetActiveCallback(nullptr);
        engine.Shutdown();
    }
});
