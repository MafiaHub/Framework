/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/bind/call.h"
#include "scripting/bind/callable.h"
#include "scripting/bind/namespace.h"
#include "scripting/bind/v8_callable.h"
#include "scripting/bind/value.h"
#include "scripting/scripting_catalog.h"
#include "scripting/v8_engine.h"
#include "scripting/value_transfer.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// script_callable_ut.h has the callables of the server, where every JavaScript runtime is Node's. This is the
// client's engine: one bare isolate, with no queue that drains as a call leaves it. Its tick runs the microtasks.
namespace ClientCallableTest {
    namespace Bind = Framework::Scripting::Bind;
    using Bind::Call;
    using Bind::CallOutcome;
    using Bind::Deferred;
    using Bind::ReturnMode;
    using Bind::ScriptCallable;
    using Framework::Scripting::TransferredValue;
    using Kind = TransferredValue::Kind;

    struct Kept final {
        std::vector<std::shared_ptr<ScriptCallable>> callables;
        std::vector<Deferred> deferreds;
    };

    inline Kept &Held() {
        static Kept kept;
        return kept;
    }

    // What an Invoke told its done.
    struct Told final {
        int calls  = 0;
        bool threw = false;
        std::string error;
        TransferredValue value;
    };

    inline std::shared_ptr<Told> Invoke(const std::shared_ptr<ScriptCallable> &callable, const std::vector<TransferredValue> &arguments = {}) {
        auto told = std::make_shared<Told>();
        callable->Invoke(arguments, ReturnMode::Value, [told](CallOutcome &&outcome) {
            ++told->calls;
            told->threw = outcome.threw;
            told->error = std::move(outcome.error);
            told->value = std::move(outcome.value);
        });
        return told;
    }

    inline TransferredValue Number(double number) {
        TransferredValue value;
        value.kind   = Kind::Number;
        value.number = number;
        return value;
    }

    inline TransferredValue Text(std::string text) {
        TransferredValue value;
        value.kind = Kind::String;
        value.text = std::move(text);
        return value;
    }

    inline const Bind::Namespace &BareDefinition() {
        using Bind::Doc;
        using Bind::TypeOf;
        namespace Type               = Bind::Type;
        static const auto definition = Bind::Namespace("Bare", "What a builtin of the client does with callables and deferred results.")
                                           .Function(
                                               "keep",
                                               [](Call &call) {
                                                   Held().callables.push_back(call.Arg(0).ToCallable());
                                               },
                                               Doc("Keeps a function.").Param("handler", Type::Any(), "Function to keep."))
                                           .Function(
                                               "later",
                                               [](Call &call) {
                                                   Held().deferreds.push_back(call.Defer());
                                               },
                                               Doc("Resolves when the test says.").ReturnType(Type::Any()));
        return definition;
    }
} // namespace ClientCallableTest

MODULE(client_callable, {
    using namespace ClientCallableTest;

    IT("invokes callables and settles deferred results in an isolate with no Node around it", {
        Held() = {};
        Framework::Scripting::V8Engine engine;
        EQUALS(engine.Init(), Framework::Scripting::ScriptingError::SCRIPTING_NONE);
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);
            // A catalog of its own, so that Bare stays out of the one the framework exports.
            Framework::Scripting::SetScriptingCatalog(isolate, "client_callable");
            BareDefinition().MaterialiseV8(isolate, context, context->Global(), v8pp::metadata::catalog("client_callable"), Bind::Side::Client);
        }
        const auto tick = [&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            engine.Tick();
        };

        EQUALS(engine.Execute("globalThis.out = [];"
                              "Bare.keep((a, b) => a + b);"
                              "Bare.keep(async (value) => { await null; return value * 2; });"
                              "Bare.keep((value) => new Promise((resolve) => setTimeout(() => resolve(value + '!'), 1)));"
                              "Bare.keep(async () => { await null; throw new Error('refused'); });"),
            true);
        const std::vector<std::shared_ptr<ScriptCallable>> callables = Held().callables;
        EQUALS(callables.size(), 4u);
        EQUALS(callables.at(0)->GetLanguage() == Framework::Scripting::Language::JavaScript, true);
        EQUALS(callables.at(0)->IsAlive(), true);

        const auto sum = Invoke(callables.at(0), {Number(2), Number(3)});
        EQUALS(sum->calls, 1);
        EQUALS(sum->threw, false);
        EQUALS(sum->value.number, 5.0);

        // No queue drains as the call leaves this isolate: what a promise settles with is told when the isolate
        // next runs its microtasks, also when it needed nothing else. That is the tick of the engine, or the next
        // call into the isolate.
        const auto doubled = Invoke(callables.at(1), {Number(21)});
        EQUALS(doubled->calls, 0);
        engine.Tick();
        EQUALS(doubled->calls, 1);
        EQUALS(doubled->threw, false);
        EQUALS(doubled->value.number, 42.0);
        const auto refused = Invoke(callables.at(3));
        EQUALS(refused->calls, 0);
        EQUALS(Invoke(callables.at(0), {Number(1), Number(1)})->calls, 1);
        EQUALS(refused->calls, 1);
        EQUALS(refused->threw, true);
        EQUALS(refused->error.substr(0, refused->error.find('\n')), "Error: refused");

        // One that waits on a timer takes the tick that fires the timer.
        const auto late = Invoke(callables.at(2), {Text("late")});
        engine.Tick();
        EQUALS(late->calls, 0);
        for (int ticks = 0; ticks < 100 && late->calls == 0; ++ticks) {
            tick();
        }
        EQUALS(late->calls, 1);
        EQUALS(late->threw, false);
        EQUALS(late->value.text, "late!");
        tick();
        EQUALS(late->calls, 1);

        // A deferred result is a promise there too, and what waits on it runs at the next tick at the latest.
        EQUALS(engine.Execute("globalThis.first = Bare.later(); first.then((value) => out.push('resolved ' + value)); Bare.later().catch((error) => out.push('rejected ' + error.message));"
                              "if (!(first instanceof Promise)) throw Error('not a promise');"),
            true);
        EQUALS(Held().deferreds.size(), 2u);
        Held().deferreds.at(0).Resolve(Text("now"));
        Held().deferreds.at(1).Reject("Bare.later: no");
        tick();
        EQUALS(engine.Execute("if (out.join() !== 'resolved now,rejected Bare.later: no') throw Error('got ' + out.join());"), true);

        // The engine's one isolate has no listener to speak for it: its shutdown does.
        Framework::Scripting::ClearScriptingCatalog(engine.GetIsolate());
        engine.Shutdown();
        EQUALS(callables.at(0)->IsAlive(), false);
        const auto gone = Invoke(callables.at(0));
        EQUALS(gone->calls, 1);
        EQUALS(gone->error, "runtime stopped");
        Held() = {};
    });
});
