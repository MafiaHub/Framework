/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_runtime_ut.h"
#include "node_test_helpers.h"

#include "scripting/bind/call.h"
#include "scripting/bind/callable.h"
#include "scripting/bind/namespace.h"
#include "scripting/bind/v8_callable.h"
#include "scripting/bind/value.h"
#include "scripting/function_references.h"
#include "scripting/lua/lua_engine.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/lua/lua_scheduler.h"
#include "scripting/lua/lua_transfer.h"
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/scripting_catalog.h"
#include "scripting/value_transfer.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ScriptCallableTest {
    namespace Bind = Framework::Scripting::Bind;
    using Bind::Call;
    using Bind::CallOutcome;
    using Bind::Deferred;
    using Bind::ReturnMode;
    using Bind::ScriptCallable;
    using Framework::Scripting::Language;
    using Framework::Scripting::LuaRuntime;
    using Framework::Scripting::TransferredValue;
    using LuaRuntimeTest::Contains;
    using LuaRuntimeTest::FirstLine;
    using Kind = TransferredValue::Kind;

    class Stage;

    // What an Invoke told its done.
    struct Told final {
        int calls  = 0;
        bool threw = false;
        std::string error;
        TransferredValue value;
    };

    // What the functions of Stage keep of a script's call, for the test to use once the call has returned.
    struct Kept final {
        std::vector<std::shared_ptr<ScriptCallable>> callables;
        std::vector<Deferred> deferreds;
        // What the calls Stage.run made were told.
        std::vector<std::shared_ptr<Told>> told;
        // What Stage.give was given, and Stage.take hands out.
        TransferredValue value;
    };

    inline Kept &Held() {
        static Kept kept;
        return kept;
    }

    // The stage the functions of Stage act on.
    inline Stage *current = nullptr;

    void DestroyRuntime(const std::string &resource);

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

    inline TransferredValue List(std::vector<TransferredValue> items) {
        TransferredValue value;
        value.kind  = Kind::Array;
        value.items = std::move(items);
        return value;
    }

    // A copied value as JSON spells it, with what JSON has no word for by name.
    inline std::string Spell(const TransferredValue &value) {
        switch (value.kind) {
        case Kind::Undefined: return "undefined";
        case Kind::Null: return "null";
        case Kind::Boolean: return value.boolean ? "true" : "false";
        case Kind::Number: return std::trunc(value.number) == value.number ? std::to_string(static_cast<long long>(value.number)) : std::to_string(value.number);
        case Kind::String: return "\"" + value.text + "\"";
        case Kind::Function: return value.callable != nullptr ? "<function>" : "<function without a callable>";
        case Kind::Array: {
            std::string text = "[";
            for (const TransferredValue &item : value.items) {
                text += (text.size() > 1 ? "," : "") + Spell(item);
            }
            return text + "]";
        }
        case Kind::Object: {
            std::string text = "{";
            for (const auto &[name, field] : value.properties) {
                text += (text.size() > 1 ? "," : "") + ("\"" + name + "\":") + Spell(field);
            }
            return text + "}";
        }
        default: return "<kind " + std::to_string(static_cast<int>(value.kind)) + ">";
        }
    }

    inline std::shared_ptr<Told> Invoke(const std::shared_ptr<ScriptCallable> &callable, const std::vector<TransferredValue> &arguments = {}, ReturnMode mode = ReturnMode::Value) {
        auto told = std::make_shared<Told>();
        callable->Invoke(arguments, mode, [told](CallOutcome &&outcome) {
            ++told->calls;
            told->threw = outcome.threw;
            told->error = std::move(outcome.error);
            told->value = std::move(outcome.value);
        });
        return told;
    }

    // Stage, the same functions in both languages: what a builtin does with a function it is handed, with a result
    // it settles later, and with a value that crosses between runtimes.
    inline const Bind::Namespace &StageDefinition() {
        using Bind::Doc;
        using Bind::TypeOf;
        namespace Type               = Bind::Type;
        static const auto definition = Bind::Namespace("Stage", "What a builtin does with callables and deferred results.")
                                           .Function(
                                               "keep",
                                               [](Call &call) {
                                                   std::shared_ptr<ScriptCallable> callable = call.Arg(0).ToCallable();
                                                   call.Return(callable != nullptr);
                                                   if (callable != nullptr) {
                                                       Held().callables.push_back(std::move(callable));
                                                   }
                                               },
                                               Doc("Keeps a function.").Param("handler", Type::Any(), "Function to keep.").ReturnType(TypeOf<bool>()))
                                           .Function(
                                               "same",
                                               [](Call &call) {
                                                   const std::shared_ptr<ScriptCallable> first  = call.Arg(0).ToCallable();
                                                   const std::shared_ptr<ScriptCallable> second = call.Arg(1).ToCallable();
                                                   call.Return(first != nullptr && second != nullptr && first->Equals(*second) && second->Equals(*first));
                                               },
                                               Doc("Tells whether two functions are one.").Param("first", Type::Any(), "A function.").Param("second", Type::Any(), "Another.").ReturnType(TypeOf<bool>()))
                                           .Function(
                                               "owner",
                                               [](Call &call) {
                                                   const std::shared_ptr<ScriptCallable> callable = call.Arg(0).ToCallable();
                                                   call.Return(callable != nullptr ? callable->ResourceName() : std::string("<none>"));
                                               },
                                               Doc("Names the resource of a function.").Param("handler", Type::Any(), "A function.").ReturnType(TypeOf<std::string>()))
                                           .Function(
                                               "now",
                                               [](Call &call) {
                                                   Deferred deferred = call.Defer();
                                                   deferred.Resolve(call.Arg(0).ToTransfer().GetValue());
                                                   // What a deferred call returns is not what the script gets.
                                                   call.Return("dropped");
                                                   Held().deferreds.push_back(deferred);
                                               },
                                               Doc("Resolves before it returns.").Param("value", Type::Any(), "Value to resolve with.").ReturnType(Type::Any()))
                                           .Function(
                                               "later",
                                               [](Call &call) {
                                                   Held().deferreds.push_back(call.Defer());
                                               },
                                               Doc("Resolves when the test says.").ReturnType(Type::Any()))
                                           .Function(
                                               "refuse",
                                               [](Call &call) {
                                                   call.Defer().Reject("Stage.refuse: " + call.Arg(0).AsString());
                                               },
                                               Doc("Rejects before it returns.").Param("reason", TypeOf<std::string>(), "Why.").ReturnType(Type::Any()))
                                           .Function(
                                               "laterAndThrow",
                                               [](Call &call) {
                                                   Held().deferreds.push_back(call.Defer());
                                                   call.ThrowError("Stage.laterAndThrow: thrown all the same");
                                               },
                                               Doc("Defers, then throws.").ReturnType(Type::Any()))
                                           .Function(
                                               "give",
                                               [](Call &call) {
                                                   auto copied = call.Arg(0).ToTransfer();
                                                   if (copied) {
                                                       Held().value = std::move(copied).GetValue();
                                                   }
                                                   call.Return(static_cast<bool>(copied));
                                               },
                                               Doc("Copies a value out of the runtime.").Param("value", Type::Any(), "Value to copy.").ReturnType(TypeOf<bool>()))
                                           .Function(
                                               "take",
                                               [](Call &call) {
                                                   const auto built = call.FromTransfer(Held().value);
                                                   if (!built) {
                                                       call.ThrowError(built.GetError());
                                                       return;
                                                   }
                                                   call.Return(built.GetValue());
                                               },
                                               Doc("Builds the value Stage.give copied.").ReturnType(Type::Any()))
                                           .Function(
                                               "box",
                                               [](Call &call) {
                                                   Bind::Value box = call.NewObject();
                                                   if (const auto built = call.FromTransfer(Held().value)) {
                                                       box.Set("item", built.GetValue());
                                                   }
                                                   call.Return(box);
                                               },
                                               Doc("Builds the value Stage.give copied as the item of a new object.").ReturnType(Type::Any()))
                                           .Function(
                                               "run",
                                               [](Call &call) {
                                                   // As a builtin that dispatches to a handler does, from inside a script's call.
                                                   const std::shared_ptr<ScriptCallable> callable = call.Arg(0).ToCallable();
                                                   const auto told                                = std::make_shared<Told>();
                                                   callable->Invoke({call.Arg(1).ToTransfer().GetValue()}, ReturnMode::Value, [told](CallOutcome &&outcome) {
                                                       ++told->calls;
                                                       told->threw = outcome.threw;
                                                       told->error = std::move(outcome.error);
                                                       told->value = std::move(outcome.value);
                                                   });
                                                   Held().told.push_back(told);
                                                   if (told->calls == 0) {
                                                       call.Return("not yet");
                                                   }
                                                   else if (told->threw) {
                                                       call.ThrowError(told->error);
                                                   }
                                                   else if (const auto built = call.FromTransfer(told->value)) {
                                                       call.Return(built.GetValue());
                                                   }
                                                   else {
                                                       call.ThrowError(built.GetError());
                                                   }
                                               },
                                               Doc("Invokes a function with a value.").Param("handler", Type::Any(), "Function to invoke.").Param("value", Type::Any(), "Its argument.").ReturnType(Type::Any()))
                                           .Function(
                                               "destroy",
                                               [](Call &call) {
                                                   DestroyRuntime(call.Arg(0).AsString());
                                               },
                                               Doc("Destroys the runtime of a resource.").Param("resource", TypeOf<std::string>(), "The resource."));
        return definition;
    }

    // A Node engine and a Lua engine, each telling the callables of the runtimes it destroys, with Stage in every
    // runtime they create. JavaScript runs on the real clock, Lua on one the test moves.
    class Stage final {
      public:
        explicit Stage(const Framework::Scripting::LuaEngineOptions &luaOptions = {}): _lua(luaOptions) {
            using namespace Framework::Scripting;
            static int count = 0;
            _catalog         = "script_callable/" + std::to_string(++count);
            Held()           = {};
            current          = this;

            (void)_node.Init();
            Bind::WatchRuntimes(_node);
            _node.SetRuntimeSetupCallback([this](Engine *engine) {
                v8::Isolate *isolate           = engine->GetIsolate();
                v8::Local<v8::Context> context = engine->GetContext();
                // A catalog of its own, so that Stage stays out of the one the framework exports.
                SetScriptingCatalog(isolate, _catalog);
                StageDefinition().MaterialiseV8(isolate, context, context->Global(), v8pp::metadata::catalog(_catalog), Bind::Side::Server);
            });

            Bind::WatchRuntimes(_lua);
            _lua.SetRuntimeSetupCallback([](LuaRuntime &runtime) {
                StageDefinition().RegisterLuaGlobal(runtime.State(), Bind::Side::Server);
            });
            (void)_lua.Init();
        }

        ~Stage() {
            Held()  = {};
            current = nullptr;
            _lua.Shutdown();
            _node.Shutdown();
        }

        Framework::Scripting::NodeEngine &Node() {
            return _node;
        }

        Framework::Scripting::LuaEngine &LuaEngine() {
            return _lua;
        }

        bool CreateJs(const std::string &resource) {
            return _node.CreateResourceRuntime(resource);
        }

        bool CreateLua(const std::string &resource) {
            const bool created = _lua.CreateResourceRuntime(resource);
            // The scheduler of a new state counts from the real clock until it is told the time.
            At(_elapsed);
            return created;
        }

        v8::Isolate *Isolate(const std::string &resource) {
            return _node.GetResourceRuntime(resource)->GetIsolate();
        }

        LuaRuntime &Runtime(const std::string &resource) {
            return *_lua.GetRuntime(resource);
        }

        // What `source` gives in the JavaScript runtime of `resource`, as String() spells it, or "<threw> " and the
        // exception.
        std::string Js(const std::string &resource, const std::string &source) {
            return NodeTest::Eval(*_node.GetResourceRuntime(resource), source);
        }

        // Runs `source` in the Lua runtime of `resource` as a script file runs: in a coroutine, which can wait.
        // Empty, or the error.
        std::string Lua(const std::string &resource, const std::string &source) {
            return LuaRuntimeTest::Run(Runtime(resource), source);
        }

        // What `expression` gives there on the main thread, where nothing can wait, as tostring() spells it.
        std::string LuaValue(const std::string &resource, const std::string &expression) {
            return LuaRuntimeTest::Eval(Runtime(resource), expression);
        }

        // Has the collector of a JavaScript runtime take everything nothing holds.
        void CollectJs(const std::string &resource) {
            v8::Isolate *isolate = Isolate(resource);
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            isolate->LowMemoryNotification();
        }

        size_t Suspended(const std::string &resource) {
            return Runtime(resource).Scheduler().SuspendedCoroutines();
        }

        // Ticks the Lua engine at `ms` after the start.
        void At(int ms) {
            _elapsed = ms;
            _lua.Tick(_start + std::chrono::milliseconds(ms));
        }

        // Ticks the Node engine until `condition` holds.
        template <typename Condition>
        bool TickUntil(Condition condition) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline) {
                _node.Tick();
                if (condition()) {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        }

      private:
        std::string _catalog;
        std::chrono::steady_clock::time_point _start = std::chrono::steady_clock::time_point() + std::chrono::hours(1);
        int _elapsed                                 = 0;
        Framework::Scripting::NodeEngine _node;
        Framework::Scripting::LuaEngine _lua;
    };

    // As a resource that stops does, of either language, also from inside its own script.
    inline void DestroyRuntime(const std::string &resource) {
        current->Node().DestroyResourceRuntime(resource);
        current->LuaEngine().DestroyResourceRuntime(resource);
    }
} // namespace ScriptCallableTest

MODULE(script_callable, {
    using namespace ScriptCallableTest;

    // --- A JavaScript function ---

    IT("calls a JavaScript function with copies of the arguments and tells done what it returned", {
        Stage stage;
        EQUALS(stage.CreateJs("alpha"), true);
        EQUALS(stage.Js("alpha", "Stage.keep((a, b, list) => ({ sum: a + b, list: list.concat('!') }))"), "true");
        EQUALS(stage.Js("alpha", "Stage.keep('not a function')"), "false");
        EQUALS(Held().callables.size(), 1u);

        const std::shared_ptr<ScriptCallable> callable = Held().callables.at(0);
        EQUALS(callable->GetLanguage() == Language::JavaScript, true);
        EQUALS(callable->IsAlive(), true);

        const auto told = Invoke(callable, {Number(2), Number(3), List({Text("a")})});
        EQUALS(told->calls, 1);
        EQUALS(told->threw, false);
        EQUALS(Spell(told->value), R"({"sum":5,"list":["a","!"]})");

        // Missing arguments are undefined, and a function that returns nothing gives undefined.
        EQUALS(stage.Js("alpha", "Stage.keep((a) => { globalThis.got = String(a); })"), "true");
        const auto nothing = Invoke(Held().callables.at(1));
        EQUALS(nothing->calls, 1);
        EQUALS(Spell(nothing->value), "undefined");
        EQUALS(stage.Js("alpha", "got"), "undefined");
    });

    IT("calls a JavaScript function with no this", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.Js("alpha", "Stage.keep(function () { 'use strict'; return this === undefined; })");
        EQUALS(Spell(Invoke(Held().callables.at(0))->value), "true");
    });

    IT("exposes the isolate and the function of a JavaScript callable", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.Js("alpha", "globalThis.kept = () => 1; Stage.keep(kept)");
        const std::shared_ptr<ScriptCallable> callable = Held().callables.at(0);
        const auto *javascript                         = static_cast<const Bind::V8Callable *>(callable.get());
        EQUALS(javascript->Isolate() == stage.Isolate("alpha"), true);

        v8::Isolate *isolate = javascript->Isolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = stage.Node().GetResourceRuntime("alpha")->GetContext();
        v8::Context::Scope contextScope(context);
        v8::Local<v8::Value> kept = context->Global()->Get(context, v8::String::NewFromUtf8Literal(isolate, "kept")).ToLocalChecked();
        EQUALS(javascript->Function()->StrictEquals(kept), true);
    });

    IT("tells done later what the promise of a JavaScript function settles with", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.Js("alpha", "Stage.keep(async (value) => { await new Promise((resolve) => setTimeout(resolve, 2)); return value * 2; })");
        const auto told = Invoke(Held().callables.at(0), {Number(21)});
        EQUALS(told->calls, 0);
        EQUALS(stage.TickUntil([&] {
            return told->calls > 0;
        }),
            true);
        EQUALS(told->calls, 1);
        EQUALS(told->threw, false);
        EQUALS(Spell(told->value), "42");
        stage.Node().Tick();
        EQUALS(told->calls, 1);

        // A promise that settles as the call leaves the runtime has told done by the time Invoke returns.
        stage.Js("alpha", "Stage.keep(async () => 'at once')");
        const auto atOnce = Invoke(Held().callables.at(1));
        EQUALS(atOnce->calls, 1);
        EQUALS(Spell(atOnce->value), "\"at once\"");
    });

    IT("reports what a JavaScript function throws and what its promise is rejected with", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.Js("alpha", "Stage.keep(function failing() { throw new Error('boom'); })");
        const auto thrown = Invoke(Held().callables.at(0));
        EQUALS(thrown->calls, 1);
        EQUALS(thrown->threw, true);
        EQUALS(FirstLine(thrown->error), "Error: boom");
        EQUALS(Contains(thrown->error, "at failing"), true);
        EQUALS(Spell(thrown->value), "undefined");

        stage.Js("alpha", "Stage.keep(async function refusing() { await new Promise((resolve) => setTimeout(resolve, 2)); throw new TypeError('locked'); })");
        const auto rejected = Invoke(Held().callables.at(1));
        EQUALS(rejected->calls, 0);
        EQUALS(stage.TickUntil([&] {
            return rejected->calls > 0;
        }),
            true);
        EQUALS(rejected->threw, true);
        EQUALS(FirstLine(rejected->error), "TypeError: locked");
        EQUALS(Contains(rejected->error, "at refusing"), true);

        // What is thrown need not be an error.
        stage.Js("alpha", "Stage.keep(() => Promise.reject('just text'))");
        const auto text = Invoke(Held().callables.at(2));
        EQUALS(text->calls, 1);
        EQUALS(text->threw, true);
        EQUALS(text->error, "just text");
    });

    // --- A Lua function ---

    IT("calls a Lua function with copies of the arguments and tells done its first result", {
        Stage stage;
        EQUALS(stage.CreateLua("gamma"), true);
        EQUALS(stage.Lua("gamma", "kept = Stage.keep(function(a, b, list) list[#list + 1] = '!' return { sum = a + b, list = list }, 'second' end)"), "");
        EQUALS(stage.LuaValue("gamma", "kept"), "true");
        EQUALS(stage.LuaValue("gamma", "Stage.keep('not a function')"), "false");
        EQUALS(Held().callables.size(), 1u);

        const std::shared_ptr<ScriptCallable> callable = Held().callables.at(0);
        EQUALS(callable->GetLanguage() == Language::Lua, true);
        EQUALS(callable->ResourceName(), "gamma");
        EQUALS(callable->IsAlive(), true);

        const auto told = Invoke(callable, {Number(2), Number(3), List({Text("a")})});
        EQUALS(told->calls, 1);
        EQUALS(told->threw, false);
        EQUALS(Spell(told->value), R"({"list":["a","!"],"sum":5})");

        stage.Lua("gamma", "Stage.keep(function(a) got = tostring(a) end)");
        const auto nothing = Invoke(Held().callables.at(1));
        EQUALS(nothing->calls, 1);
        EQUALS(Spell(nothing->value), "undefined");
        EQUALS(stage.LuaValue("gamma", "got"), "nil");
        EQUALS(lua_gettop(stage.Runtime("gamma").State()), 0);
    });

    IT("tells done when a Lua function that waited has ended", {
        Stage stage;
        stage.CreateLua("gamma");
        stage.Lua("gamma", "Stage.keep(function(value) sleep(10) sleep(10) return value * 2 end)");
        const auto told = Invoke(Held().callables.at(0), {Number(21)});
        EQUALS(told->calls, 0);
        EQUALS(stage.Suspended("gamma"), 1u);
        stage.At(10);
        EQUALS(told->calls, 0);
        stage.At(20);
        EQUALS(told->calls, 1);
        EQUALS(told->threw, false);
        EQUALS(Spell(told->value), "42");
        EQUALS(stage.Suspended("gamma"), 0u);
        stage.At(30);
        EQUALS(told->calls, 1);
    });

    IT("reports what a Lua function raises, with the traceback", {
        Stage stage;
        stage.CreateLua("gamma");
        stage.Lua("gamma", "function failing() error('boom') end Stage.keep(failing)");
        const auto raised = Invoke(Held().callables.at(0));
        EQUALS(raised->calls, 1);
        EQUALS(raised->threw, true);
        EQUALS(FirstLine(raised->error), "test:1: boom");
        EQUALS(Contains(raised->error, "\nstack traceback:\n"), true);
        EQUALS(Contains(raised->error, "in function 'failing'"), true);
        EQUALS(Spell(raised->value), "undefined");

        stage.Lua("gamma", "Stage.keep(function() sleep(10) error('boom later') end)");
        const auto later = Invoke(Held().callables.at(1));
        EQUALS(later->calls, 0);
        stage.At(10);
        EQUALS(later->calls, 1);
        EQUALS(later->threw, true);
        EQUALS(FirstLine(later->error), "test:1: boom later");
        // The error went to done and to nobody else.
        EQUALS(stage.LuaEngine().DrainPendingErrors().size(), 0u);
    });

    // --- What comes back, and what goes in ---

    IT("copies back a boolean and nothing else when only the outcome is asked for", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Js("alpha", "Stage.keep(() => false); Stage.keep(() => Symbol('cannot cross')); Stage.keep(async () => true); Stage.keep(async () => ({ big: 1 }))");
        EQUALS(Spell(Invoke(Held().callables.at(0), {}, ReturnMode::Outcome)->value), "false");
        const auto symbol = Invoke(Held().callables.at(1), {}, ReturnMode::Outcome);
        EQUALS(symbol->threw, false);
        EQUALS(Spell(symbol->value), "undefined");
        EQUALS(Spell(Invoke(Held().callables.at(2), {}, ReturnMode::Outcome)->value), "true");
        EQUALS(Spell(Invoke(Held().callables.at(3), {}, ReturnMode::Outcome)->value), "undefined");

        Held().callables.clear();
        stage.Lua("gamma", "Stage.keep(function() return false end) Stage.keep(function() return coroutine.create(print) end) Stage.keep(function() sleep(5) return true end)");
        EQUALS(Spell(Invoke(Held().callables.at(0), {}, ReturnMode::Outcome)->value), "false");
        const auto coroutine = Invoke(Held().callables.at(1), {}, ReturnMode::Outcome);
        EQUALS(coroutine->threw, false);
        EQUALS(Spell(coroutine->value), "undefined");
        const auto slept = Invoke(Held().callables.at(2), {}, ReturnMode::Outcome);
        stage.At(5);
        EQUALS(slept->calls, 1);
        EQUALS(Spell(slept->value), "true");
    });

    IT("fails the call whose result cannot cross", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Js("alpha", "Stage.keep(() => ({ id: Symbol('cannot cross') })); Stage.keep(async () => new WeakMap())");
        const auto symbol = Invoke(Held().callables.at(0));
        EQUALS(symbol->calls, 1);
        EQUALS(symbol->threw, true);
        EQUALS(symbol->error, "Cannot copy value.id: a symbol cannot be copied");
        const auto weak = Invoke(Held().callables.at(1));
        EQUALS(weak->calls, 1);
        EQUALS(weak->threw, true);
        EQUALS(weak->error, "Cannot copy value: this kind of object cannot be copied");

        Held().callables.clear();
        stage.Lua("gamma", "Stage.keep(function() return { job = coroutine.create(print) } end)");
        const auto coroutine = Invoke(Held().callables.at(0));
        EQUALS(coroutine->calls, 1);
        EQUALS(coroutine->threw, true);
        EQUALS(coroutine->error, "Cannot copy value.job: a coroutine cannot be copied");
        EQUALS(lua_gettop(stage.Runtime("gamma").State()), 0);
    });

    IT("fails the call whose argument cannot be built in the runtime of the function", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Js("alpha", "globalThis.ran = 0; Stage.keep(() => { ++ran; })");
        stage.Lua("gamma", "ran = 0 Stage.keep(function() ran = ran + 1 end)");

        TransferredValue unknown;
        unknown.kind          = Kind::HostObject;
        unknown.text          = "NoSuchType";
        const auto javascript = Invoke(Held().callables.at(0), {Number(1), unknown});
        EQUALS(javascript->calls, 1);
        EQUALS(javascript->threw, true);
        EQUALS(FirstLine(javascript->error), "Argument 1: Error: Unknown host type NoSuchType");
        EQUALS(stage.Js("alpha", "ran"), "0");

        const auto lua = Invoke(Held().callables.at(1), {Number(1), unknown});
        EQUALS(lua->calls, 1);
        EQUALS(lua->threw, true);
        EQUALS(lua->error, "Argument 2: Cannot copy value: host type 'NoSuchType' is not available in Lua");
        EQUALS(stage.LuaValue("gamma", "ran"), "0");
        EQUALS(lua_gettop(stage.Runtime("gamma").State()), 0);
    });

    // --- One callable for a function ---

    IT("gives one callable for a function, however often it is taken", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Js("alpha", "globalThis.handler = () => 1; Stage.keep(handler); Stage.keep(handler); Stage.keep(() => 1)");
        stage.Lua("gamma", "function handler() return 1 end Stage.keep(handler) Stage.keep(handler) Stage.keep(function() return 1 end)");
        for (const size_t first : {size_t {0}, size_t {3}}) {
            const auto &callables = Held().callables;
            EQUALS(callables.at(first).get() == callables.at(first + 1).get(), true);
            EQUALS(callables.at(first)->Equals(*callables.at(first + 1)), true);
            EQUALS(callables.at(first)->Equals(*callables.at(first + 2)), false);
            EQUALS(callables.at(first + 2)->Equals(*callables.at(first)), false);
        }
        EQUALS(Held().callables.at(0)->Equals(*Held().callables.at(3)), false);
        EQUALS(Held().callables.at(3)->Equals(*Held().callables.at(0)), false);

        EQUALS(stage.Js("alpha", "[Stage.same(handler, handler), Stage.same(handler, () => 1), Stage.same(handler, 1)].join()"), "true,false,false");
        EQUALS(stage.LuaValue("gamma", "tostring(Stage.same(handler, handler)) .. tostring(Stage.same(handler, print)) .. tostring(Stage.same(handler, 1))"), "truefalsefalse");

        // Once nobody holds it the function gets a new one, which works like the first.
        Held().callables.clear();
        stage.Js("alpha", "Stage.keep(handler)");
        stage.Lua("gamma", "Stage.keep(handler)");
        EQUALS(Spell(Invoke(Held().callables.at(0))->value), "1");
        EQUALS(Spell(Invoke(Held().callables.at(1))->value), "1");
        EQUALS(stage.Js("alpha", "Stage.same(handler, handler)"), "true");
        EQUALS(stage.LuaValue("gamma", "Stage.same(handler, handler)"), "true");
    });

    IT("names the resource a function belongs to", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        // JavaScript asks the resource manager, when the function is taken.
        EQUALS(stage.Js("alpha", "Stage.owner(() => 1)"), "");
        {
            Framework::Scripting::ResourceManager manager(&stage.Node());
            Bind::SetCallResourceManager(stage.Isolate("alpha"), &manager);
            EQUALS(stage.Js("alpha", "Stage.owner(() => 1)"), "alpha");
            stage.Js("alpha", "Stage.keep(() => 1)");
            Bind::SetCallResourceManager(stage.Isolate("alpha"), nullptr);
        }
        EQUALS(Held().callables.at(0)->ResourceName(), "alpha");
        EQUALS(stage.LuaValue("gamma", "Stage.owner(print)"), "gamma");
        EQUALS(stage.LuaValue("gamma", "Stage.owner(1)"), "<none>");
    });

    // --- A runtime that goes ---

    IT("marks the callables of a destroyed runtime dead and answers for them at once", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateJs("beta");
        stage.CreateLua("gamma");
        stage.CreateLua("delta");
        stage.Js("alpha", "Stage.keep(() => 1)");
        stage.Js("beta", "Stage.keep(() => 2)");
        stage.Lua("gamma", "Stage.keep(function() return 3 end)");
        stage.Lua("delta", "Stage.keep(function() return 4 end)");
        const std::vector<std::shared_ptr<ScriptCallable>> callables = Held().callables;

        // A listener added after WatchRuntimes finds them dead already, with the runtime still there. The engines
        // keep the listeners, which hear of the other runtimes as the stage goes.
        const auto heard = std::make_shared<std::string>();
        stage.Node().AddRuntimeDisposingListener([heard, callables](v8::Isolate *isolate) {
            *heard += std::string(" javascript:") + (isolate->IsInUse() ? "open:" : "closed:") + (callables.at(0)->IsAlive() ? "alive" : "dead");
        });
        stage.LuaEngine().AddRuntimeDisposingListener([heard, callables](LuaRuntime &runtime) {
            *heard += std::string(" lua:") + (lua_gettop(runtime.State()) == 0 ? "open:" : "busy:") + (callables.at(2)->IsAlive() ? "alive" : "dead");
        });

        stage.Node().DestroyResourceRuntime("alpha");
        stage.LuaEngine().DestroyResourceRuntime("gamma");
        EQUALS(*heard, " javascript:open:dead lua:open:dead");
        for (const size_t gone : {size_t {0}, size_t {2}}) {
            EQUALS(callables.at(gone)->IsAlive(), false);
            const auto told = Invoke(callables.at(gone), {Number(1)});
            EQUALS(told->calls, 1);
            EQUALS(told->threw, true);
            EQUALS(told->error, "runtime stopped");
            EQUALS(Spell(told->value), "undefined");
            // It is still the callable it was, and what it was of.
            EQUALS(callables.at(gone)->Equals(*callables.at(gone)), true);
            EQUALS(callables.at(gone)->Equals(*callables.at(gone + 1)), false);
        }
        EQUALS(callables.at(0)->ResourceName(), "");
        EQUALS(callables.at(2)->ResourceName(), "gamma");
        EQUALS(static_cast<const Bind::V8Callable *>(callables.at(0).get())->Isolate() == nullptr, true);

        // The runtimes that stay are untouched.
        EQUALS(callables.at(1)->IsAlive(), true);
        EQUALS(callables.at(3)->IsAlive(), true);
        EQUALS(Spell(Invoke(callables.at(1))->value), "2");
        EQUALS(Spell(Invoke(callables.at(3))->value), "4");
    });

    IT("completes what a destroyed runtime still owed", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Js("alpha", "Stage.keep(() => new Promise(() => {}))");
        stage.Lua("gamma", "Stage.keep(function() sleep(1000) return 'never' end)");
        const auto javascript = Invoke(Held().callables.at(0));
        const auto lua        = Invoke(Held().callables.at(1));
        const auto again      = Invoke(Held().callables.at(1));
        EQUALS(javascript->calls + lua->calls + again->calls, 0);

        stage.Node().DestroyResourceRuntime("alpha");
        EQUALS(javascript->calls, 1);
        EQUALS(javascript->threw, true);
        EQUALS(javascript->error, "runtime stopped");

        stage.LuaEngine().DestroyResourceRuntime("gamma");
        EQUALS(lua->calls, 1);
        EQUALS(lua->threw, true);
        EQUALS(lua->error, "runtime stopped");
        EQUALS(again->calls, 1);
        EQUALS(again->error, "runtime stopped");

        stage.At(2000);
        stage.Node().Tick();
        EQUALS(javascript->calls + lua->calls + again->calls, 3);
    });

    IT("takes a Lua runtime that was destroyed from its own script for gone at once", {
        Stage stage;
        stage.CreateLua("gamma");
        stage.Lua("gamma", "Stage.keep(function() ran = true return 1 end) Stage.keep(function() sleep(10) end)");
        const std::vector<std::shared_ptr<ScriptCallable>> callables = Held().callables;
        const auto waiting                                           = Invoke(callables.at(1));
        EQUALS(waiting->calls, 0);

        // The state stays open until the script that destroyed it has returned and the engine ticks.
        std::shared_ptr<Told> inside;
        Held().callables.clear();
        stage.Lua("gamma", "Stage.destroy('gamma') Stage.keep(function() return 2 end)");
        EQUALS(stage.LuaEngine().GetRuntime("gamma") == nullptr, true);
        EQUALS(callables.at(0)->IsAlive(), false);
        EQUALS(Held().callables.at(0)->IsAlive(), false);
        inside = Invoke(callables.at(0));
        EQUALS(inside->calls, 1);
        EQUALS(inside->error, "runtime stopped");
        EQUALS(waiting->calls, 0);

        stage.At(20);
        EQUALS(waiting->calls, 1);
        EQUALS(waiting->error, "runtime stopped");
        EQUALS(callables.at(0)->IsAlive(), false);
    });

    IT("takes a JavaScript runtime that was stopped from its own script for gone at once", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        const auto disposed = std::make_shared<int>(0);
        stage.Node().AddRuntimeDisposingListener([disposed](v8::Isolate *) {
            ++*disposed;
        });
        stage.Js("alpha", "Stage.give(() => 'still here'); Stage.keep(() => 1); Stage.keep(() => new Promise(() => {})); Stage.later()");
        EQUALS(stage.Lua("gamma", "remote = Stage.take() before = remote()"), "");
        EQUALS(stage.LuaValue("gamma", "before"), "still here");
        const std::vector<std::shared_ptr<ScriptCallable>> callables = Held().callables;
        const auto waiting                                           = Invoke(callables.at(1));
        EQUALS(waiting->calls, 0);

        // The resource stops itself. Its runtime outlives the script that stopped it, until the engine ticks, but
        // to everyone who kept a function of it, it is gone now.
        Held().callables.clear();
        EQUALS(stage.Js("alpha", "Stage.destroy('alpha'); Stage.keep(() => 2); Stage.later(); typeof Stage.take()"), "function");
        EQUALS(stage.Node().GetResourceRuntime("alpha") == nullptr, true);
        EQUALS(*disposed, 0);
        EQUALS(callables.at(0)->IsAlive(), false);
        EQUALS(static_cast<const Bind::V8Callable *>(callables.at(0).get())->Isolate() == nullptr, true);
        EQUALS(waiting->calls, 1);
        EQUALS(waiting->error, "runtime stopped");
        EQUALS(Invoke(callables.at(0))->error, "runtime stopped");
        EQUALS(stage.LuaValue("gamma", "remote()"), "<error> eval:1: runtime stopped");

        // What it kept of itself after it stopped is dead from the start, and what it still owes is not settled.
        EQUALS(Held().callables.at(0)->IsAlive(), false);
        EQUALS(Invoke(Held().callables.at(0))->error, "runtime stopped");
        EQUALS(Held().deferreds.size(), 2u);
        for (Deferred &deferred : Held().deferreds) {
            deferred.Resolve(Text("nobody is there"));
        }

        EQUALS(*disposed, 0);
        stage.Node().Tick();
        EQUALS(*disposed, 1);
        EQUALS(callables.at(0)->IsAlive(), false);
        EQUALS(Invoke(callables.at(0))->error, "runtime stopped");
        EQUALS(waiting->calls, 1);
    });

    IT("answers runtime stopped for a JavaScript function that ends its runtime", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.Js("alpha", "Stage.keep(() => { process.exit(3); }); Stage.keep(() => 1); Stage.keep(() => new Promise(() => {}))");
        const std::vector<std::shared_ptr<ScriptCallable>> callables = Held().callables;
        const auto waiting                                           = Invoke(callables.at(2));
        const auto exited                                            = Invoke(callables.at(0));
        EQUALS(exited->calls, 1);
        EQUALS(exited->threw, true);
        EQUALS(exited->error, "runtime stopped");
        // Nothing runs in it any more, and what it owed is told, though the engine only stops the resource at its
        // next tick.
        EQUALS(stage.Node().GetResourceRuntime("alpha") != nullptr, true);
        EQUALS(callables.at(1)->IsAlive(), false);
        EQUALS(Invoke(callables.at(1))->error, "runtime stopped");
        EQUALS(waiting->calls, 1);
        EQUALS(waiting->error, "runtime stopped");

        stage.Node().Tick();
        EQUALS(stage.Node().GetResourceRuntime("alpha") == nullptr, true);
        EQUALS(callables.at(1)->IsAlive(), false);
        EQUALS(waiting->calls, 1);
    });

    IT("tells done of a Lua function the deadline stopped", {
        Framework::Scripting::LuaEngineOptions options;
        options.entryDeadline = std::chrono::milliseconds(50);
        Stage stage(options);
        stage.CreateLua("gamma");
        stage.Lua("gamma", "Stage.keep(function() while true do end end) Stage.keep(function() return 'fine' end)");
        const auto stopped = Invoke(Held().callables.at(0));
        EQUALS(stopped->calls, 1);
        EQUALS(stopped->threw, true);
        EQUALS(Contains(FirstLine(stopped->error), "script ran too long"), true);
        EQUALS(Spell(stopped->value), "undefined");
        // The runtime goes on.
        EQUALS(Held().callables.at(0)->IsAlive(), true);
        EQUALS(Spell(Invoke(Held().callables.at(1))->value), "\"fine\"");
        EQUALS(lua_gettop(stage.Runtime("gamma").State()), 0);
    });

    IT("leaves what outlives its engine harmless", {
        std::vector<Deferred> deferreds;
        std::vector<std::shared_ptr<ScriptCallable>> callables;
        TransferredValue value;
        {
            Stage stage;
            stage.CreateJs("alpha");
            stage.CreateLua("gamma");
            stage.Js("alpha", "Stage.later(); Stage.later(); Stage.keep(() => 1); Stage.give({ on: () => 2 })");
            stage.Lua("gamma", "spawn(function() Stage.later() end) Stage.keep(function() return 1 end) Stage.later()");
            deferreds = Held().deferreds;
            callables = Held().callables;
            value     = Held().value;
        }
        EQUALS(deferreds.size(), 4u);
        deferreds.at(0).Resolve(Text("late"));
        deferreds.at(2).Reject("late");
        EQUALS(deferreds.at(0).Settled(), true);
        EQUALS(deferreds.at(1).Settled(), false);
        EQUALS(Invoke(callables.at(0))->error, "runtime stopped");
        EQUALS(Invoke(callables.at(1))->error, "runtime stopped");
        EQUALS(Invoke(value.properties.at(0).second.callable)->error, "runtime stopped");
        deferreds.clear();
        callables.clear();
        value = {};
    });

    IT("lets done destroy the runtime of the function and invoke another", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Js("alpha", "Stage.keep(() => 'javascript'); Stage.keep(() => 'more')");
        stage.Lua("gamma", "Stage.keep(function() return 'lua' end) Stage.keep(function() return 'more' end)");
        const std::vector<std::shared_ptr<ScriptCallable>> callables = Held().callables;

        std::string order;
        callables.at(2)->Invoke({}, ReturnMode::Value, [&](CallOutcome &&outcome) {
            order += Spell(outcome.value);
            stage.LuaEngine().DestroyResourceRuntime("gamma");
            // Gone for everyone, though its state outlives this call.
            callables.at(3)->Invoke({}, ReturnMode::Value, [&](CallOutcome &&nested) {
                order += " " + nested.error;
            });
            callables.at(0)->Invoke({}, ReturnMode::Value, [&](CallOutcome &&nested) {
                order += " " + Spell(nested.value);
                stage.Node().DestroyResourceRuntime("alpha");
            });
        });
        EQUALS(order, "\"lua\" runtime stopped \"javascript\"");
        EQUALS(callables.at(0)->IsAlive(), false);
        EQUALS(callables.at(2)->IsAlive(), false);
        stage.At(1);
        stage.Node().Tick();
        EQUALS(Invoke(callables.at(1))->error, "runtime stopped");
    });

    IT("forgets the callables of an isolate no engine speaks for when its builtins are unregistered", {
        Stage stage;
        std::string error;
        std::unique_ptr<Framework::Scripting::NodeRuntime> bare = stage.Node().CreateRuntime(error);
        std::shared_ptr<ScriptCallable> callable;
        {
            v8::Isolate *isolate = bare->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = bare->GetContext();
            v8::Context::Scope contextScope(context);
            v8::Local<v8::Value> function = v8::Script::Compile(context, v8::String::NewFromUtf8Literal(isolate, "(() => new Promise(() => {}))")).ToLocalChecked()->Run(context).ToLocalChecked();
            callable                      = Bind::V8ToCallable(isolate, context, function.As<v8::Function>());
        }
        const auto pending = Invoke(callable);
        EQUALS(pending->calls, 0);
        Framework::Scripting::Builtins::UnregisterAll(bare->GetIsolate());
        EQUALS(callable->IsAlive(), false);
        EQUALS(pending->calls, 1);
        EQUALS(pending->error, "runtime stopped");
        bare.reset();
        EQUALS(Invoke(callable)->error, "runtime stopped");
    });

    IT("marks the callables of a Lua runtime no engine owns dead when its state closes", {
        std::shared_ptr<ScriptCallable> callable;
        std::shared_ptr<Told> pending;
        {
            LuaRuntime runtime(LuaRuntimeTest::Options());
            StageDefinition().RegisterLuaGlobal(runtime.State(), Bind::Side::Server);
            Held() = {};
            EQUALS(LuaRuntimeTest::Run(runtime, "Stage.keep(function() sleep(10) end)"), "");
            callable = Held().callables.at(0);
            Held()   = {};
            pending  = Invoke(callable);
            EQUALS(pending->calls, 0);
            EQUALS(callable->IsAlive(), true);
        }
        EQUALS(callable->IsAlive(), false);
        EQUALS(pending->calls, 1);
        EQUALS(pending->error, "runtime stopped");
        EQUALS(Invoke(callable)->error, "runtime stopped");
    });

    IT("invokes a function from inside a call of its own runtime", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        EQUALS(stage.Js("alpha", "Stage.run((value) => value + 1, 1)"), "2");
        EQUALS(stage.Js("alpha", "try { Stage.run(() => { throw new Error('inside'); }) } catch (error) { error.message.split('\\n')[0] }"), "Error: inside");
        EQUALS(stage.Js("alpha", "Stage.run(() => new Promise((resolve) => setTimeout(() => resolve('late'), 2)))"), "not yet");
        EQUALS(stage.TickUntil([&] {
            return Held().told.at(2)->calls > 0;
        }),
            true);
        EQUALS(Spell(Held().told.at(2)->value), "\"late\"");

        // On the main thread of Lua and in a coroutine, where the function may wait and the caller does not.
        Held().told.clear();
        EQUALS(stage.LuaValue("gamma", "Stage.run(function(value) return value + 1 end, 1)"), "2");
        EQUALS(stage.Lua("gamma", "result = Stage.run(function(value) return value + 1 end, 2) waited = Stage.run(function() sleep(5) return 'late' end)"), "");
        EQUALS(stage.LuaValue("gamma", "result .. ' ' .. waited"), "3 not yet");
        EQUALS(stage.LuaValue("gamma", "select(2, pcall(Stage.run, function() error('inside') end)):match('^[^\\n]*')"), "eval:1: inside");
        EQUALS(Held().told.at(2)->calls, 0);
        stage.At(5);
        EQUALS(Spell(Held().told.at(2)->value), "\"late\"");
        EQUALS(lua_gettop(stage.Runtime("gamma").State()), 0);

        // Nobody has to wait on a call.
        stage.Js("alpha", "globalThis.ran = 0; Stage.keep(() => { ++ran; })");
        stage.Lua("gamma", "ran = 0 Stage.keep(function() ran = ran + 1 end)");
        Held().callables.at(0)->Invoke({}, ReturnMode::Value, {});
        Held().callables.at(1)->Invoke({}, ReturnMode::Outcome, {});
        EQUALS(stage.Js("alpha", "ran"), "1");
        EQUALS(stage.LuaValue("gamma", "ran"), "1");
    });

    IT("keeps a Lua function for as long as its callable is held, and no longer", {
        Stage stage;
        stage.CreateLua("gamma");
        stage.CreateLua("delta");
        LuaRuntime &gamma = stage.Runtime("gamma");
        lua_gc(gamma.State(), LUA_GCCOLLECT);
        const size_t before = gamma.MemoryInUse();

        // Each closes over a string of its own that nothing else holds.
        EQUALS(stage.Lua("gamma", "for i = 1, 100 do local big = string.rep(tostring(i), 20000) Stage.keep(function() return #big end) end"), "");
        lua_gc(gamma.State(), LUA_GCCOLLECT);
        EQUALS(gamma.MemoryInUse() > before + 2000000, true);
        EQUALS(Spell(Invoke(Held().callables.at(99))->value), "60000");

        Held().callables.clear();
        lua_gc(gamma.State(), LUA_GCCOLLECT);
        EQUALS(gamma.MemoryInUse() < before + 100000, true);

        // A function another runtime stands in for is held by the stand-in, until that is collected.
        stage.Lua("gamma", "Stage.give(function() return 1 end)");
        const std::weak_ptr<ScriptCallable> given = Held().value.callable;
        stage.Lua("delta", "standIn = Stage.take()");
        Held().value = {};
        lua_gc(stage.Runtime("delta").State(), LUA_GCCOLLECT);
        EQUALS(given.expired(), false);
        stage.Lua("delta", "standIn = nil");
        lua_gc(stage.Runtime("delta").State(), LUA_GCCOLLECT);
        EQUALS(given.expired(), true);
        EQUALS(lua_gettop(gamma.State()), 0);
    });

    // --- Deferred results ---

    IT("returns a promise the deferred settles, in JavaScript", {
        Stage stage;
        stage.CreateJs("alpha");
        // Resolved before the function returned, it is a promise all the same.
        EQUALS(stage.Js("alpha", "globalThis.out = []; globalThis.first = Stage.now({ n: 5 }); first instanceof Promise"), "true");
        stage.Js("alpha", "first.then((value) => out.push('now ' + value.n))");
        EQUALS(stage.TickUntil([&] {
            return stage.Js("alpha", "out.join()") == "now 5";
        }),
            true);
        EQUALS(Held().deferreds.at(0).Settled(), true);

        EQUALS(stage.Js("alpha", "(async () => { out.push('later ' + await Stage.later()); })(); out.length"), "1");
        Deferred later = Held().deferreds.at(1);
        EQUALS(later.Settled(), false);
        later.Resolve(Text("done"));
        EQUALS(later.Settled(), true);
        // The continuation ran as the runtime was left.
        EQUALS(stage.Js("alpha", "out.join()"), "now 5,later done");

        stage.Js("alpha", "Stage.later().catch((error) => out.push(error instanceof Error, error.message))");
        Held().deferreds.at(2).Reject("Stage.later: refused");
        EQUALS(stage.Js("alpha", "out.slice(2).join()"), "true,Stage.later: refused");

        stage.Js("alpha", "Stage.refuse('at once').catch((error) => out.push(error.message))");
        EQUALS(stage.TickUntil([&] {
            return stage.Js("alpha", "out[4]") == "Stage.refuse: at once";
        }),
            true);
        // Rejected before the script had the promise, it was not taken for a rejection nobody handles.
        EQUALS(stage.Node().DrainPendingErrors().size(), 0u);
    });

    IT("returns to Lua the value of a deferred that was resolved before the function returned", {
        Stage stage;
        stage.CreateLua("gamma");
        EQUALS(stage.Lua("gamma", "local value = Stage.now({ n = 5 }) result = value.n count = select('#', Stage.now(nil))"), "");
        EQUALS(stage.LuaValue("gamma", "result"), "5");
        EQUALS(stage.LuaValue("gamma", "count"), "1");
        EQUALS(stage.Suspended("gamma"), 0u);

        EQUALS(stage.Lua("gamma", "ok, message = pcall(Stage.refuse, 'at once')"), "");
        EQUALS(stage.LuaValue("gamma", "ok"), "false");
        EQUALS(stage.LuaValue("gamma", "message"), "Stage.refuse: at once");
        EQUALS(FirstLine(stage.Lua("gamma", "Stage.refuse('in the open')")), "test:1: Stage.refuse: in the open");
        EQUALS(lua_gettop(stage.Runtime("gamma").State()), 0);
    });

    IT("suspends the Lua coroutine that called until the deferred is settled", {
        Stage stage;
        stage.CreateLua("gamma");
        EQUALS(stage.Lua("gamma", "steps = 'before' local value = Stage.later() steps = steps .. ' ' .. value.word .. ' ' .. select('#', Stage.later())"), "");
        EQUALS(stage.LuaValue("gamma", "steps"), "before");
        EQUALS(stage.Suspended("gamma"), 1u);

        TransferredValue word;
        word.kind = Kind::Object;
        word.properties.emplace_back("word", Text("after"));
        Held().deferreds.at(0).Resolve(word);
        EQUALS(stage.LuaValue("gamma", "steps"), "before");
        EQUALS(stage.Suspended("gamma"), 1u);
        // Resolved with nothing, the call still returns one value.
        Held().deferreds.at(1).Resolve();
        EQUALS(stage.LuaValue("gamma", "steps"), "before after 1");
        EQUALS(stage.Suspended("gamma"), 0u);

        EQUALS(stage.Lua("gamma", "ok, message = pcall(Stage.later) after = 'went on'"), "");
        EQUALS(stage.LuaValue("gamma", "after"), "nil");
        Held().deferreds.at(2).Reject("Stage.later: refused");
        EQUALS(stage.LuaValue("gamma", "ok"), "false");
        EQUALS(stage.LuaValue("gamma", "message"), "Stage.later: refused");
        EQUALS(stage.LuaValue("gamma", "after"), "went on");

        // A value Lua cannot have is the error of the call.
        EQUALS(stage.Lua("gamma", "ok, message = pcall(Stage.later)"), "");
        TransferredValue regexp;
        regexp.kind = Kind::RegExp;
        Held().deferreds.at(3).Resolve(regexp);
        EQUALS(stage.LuaValue("gamma", "ok"), "false");
        EQUALS(stage.LuaValue("gamma", "message"), "Cannot copy value: a RegExp cannot be copied to Lua");
        EQUALS(stage.Suspended("gamma"), 0u);
        EQUALS(lua_gettop(stage.Runtime("gamma").State()), 0);
    });

    IT("raises in Lua where the caller cannot wait, unless the deferred is settled by then", {
        Stage stage;
        stage.CreateLua("gamma");
        // On the main thread, and in a coroutine the script made itself.
        EQUALS(stage.LuaValue("gamma", "Stage.now(5)"), "5");
        EQUALS(stage.LuaValue("gamma", "coroutine.wrap(function() return Stage.now(6) end)()"), "6");
        EQUALS(stage.LuaValue("gamma", "select(2, pcall(Stage.refuse, 'no'))"), "Stage.refuse: no");

        EQUALS(stage.LuaValue("gamma", "Stage.later()"), "<error> eval:1: Stage.later: cannot wait here");
        EQUALS(stage.LuaValue("gamma", "coroutine.wrap(function() return Stage.later() end)()"), "<error> eval:1: eval:1: Stage.later: cannot wait here: called inside a coroutine the script created");
        // Settling them afterwards finds nobody.
        for (Deferred &deferred : Held().deferreds) {
            deferred.Resolve(Number(1));
        }
        EQUALS(stage.Suspended("gamma"), 0u);
        EQUALS(lua_gettop(stage.Runtime("gamma").State()), 0);
    });

    IT("settles a deferred once, and not at all once its runtime is gone", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Js("alpha", "globalThis.out = []; Stage.later().then((value) => out.push('resolved ' + value), (error) => out.push('rejected ' + error.message))");
        stage.Lua("gamma", "out = '' out = out .. tostring(Stage.later())");
        Deferred javascript = Held().deferreds.at(0);
        Deferred lua        = Held().deferreds.at(1);
        // A copy is the same deferred.
        Deferred copy = lua;
        javascript.Resolve(Number(1));
        javascript.Reject("too late");
        javascript.Resolve(Number(2));
        copy.Resolve(Number(1));
        EQUALS(lua.Settled(), true);
        lua.Reject("too late");
        lua.Resolve(Number(2));
        EQUALS(stage.Js("alpha", "out.join()"), "resolved 1");
        EQUALS(stage.LuaValue("gamma", "out"), "1");

        // One that was never given a call is settled, and does nothing.
        Deferred none;
        EQUALS(none.Settled(), true);
        none.Resolve(Number(1));
        none.Reject("nobody");

        stage.Js("alpha", "Stage.later(); Stage.later()");
        stage.Lua("gamma", "spawn(function() Stage.later() end) Stage.later()");
        const std::vector<Deferred> orphans(Held().deferreds.begin() + 2, Held().deferreds.end());
        EQUALS(orphans.size(), 4u);
        stage.Node().DestroyResourceRuntime("alpha");
        stage.LuaEngine().DestroyResourceRuntime("gamma");
        for (size_t i = 0; i < orphans.size(); ++i) {
            Deferred orphan = orphans[i];
            EQUALS(orphan.Settled(), false);
            if (i % 2 == 0) {
                orphan.Resolve(Text("nobody is there"));
            }
            else {
                orphan.Reject("nobody is there");
            }
            EQUALS(orphan.Settled(), true);
        }
    });

    IT("throws what the function threw, whatever it deferred", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        EQUALS(stage.Js("alpha", "Stage.laterAndThrow()"), "<threw> Error: Stage.laterAndThrow: thrown all the same");
        EQUALS(stage.LuaValue("gamma", "Stage.laterAndThrow()"), "<error> eval:1: Stage.laterAndThrow: thrown all the same");
        EQUALS(FirstLine(stage.Lua("gamma", "Stage.laterAndThrow()")), "test:1: Stage.laterAndThrow: thrown all the same");
        // Nobody is left to hear of them: a rejection finds no promise to leave unhandled.
        for (Deferred &deferred : Held().deferreds) {
            deferred.Reject("nobody hears this");
        }
        stage.Node().Tick();
        EQUALS(stage.Node().DrainPendingErrors().size(), 0u);
        EQUALS(stage.Suspended("gamma"), 0u);
    });

    // --- Functions between runtimes ---

    IT("copies a function out of a runtime as its callable", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        EQUALS(stage.Js("alpha", "globalThis.handler = () => 'javascript'; Stage.keep(handler); Stage.give({ on: handler })"), "true");
        EQUALS(Held().value.properties.at(0).second.kind == Kind::Function, true);
        EQUALS(Held().value.properties.at(0).second.callable == Held().callables.at(0), true);
        EQUALS(Held().value.properties.at(0).second.reference, 0u);

        EQUALS(stage.Lua("gamma", "function handler() return 'lua' end Stage.keep(handler) given = Stage.give({ on = handler })"), "");
        EQUALS(stage.LuaValue("gamma", "given"), "true");
        EQUALS(Held().value.properties.at(0).second.kind == Kind::Function, true);
        EQUALS(Held().value.properties.at(0).second.callable == Held().callables.at(1), true);

        // The transfer of Lua does so on its own.
        lua_State *L = stage.Runtime("gamma").State();
        lua_getglobal(L, "handler");
        const auto copied = Framework::Scripting::LuaTransfer::Copy(L, -1);
        lua_pop(L, 1);
        EQUALS(static_cast<bool>(copied), true);
        EQUALS(copied.GetValue().kind == Kind::Function, true);
        EQUALS(copied.GetValue().callable == Held().callables.at(1), true);

        // A function without a callable cannot be built anywhere.
        TransferredValue bare;
        bare.kind = Kind::Function;
        std::string error;
        EQUALS(Framework::Scripting::LuaTransfer::Rebuild(L, bare, error), false);
        EQUALS(error, "Cannot copy value: a function cannot be copied");
        Held().value = bare;
        EQUALS(stage.Js("alpha", "Stage.take()"), "<threw> Error: A function cannot be rebuilt here");
        EQUALS(lua_gettop(L), 0);
    });

    IT("calls a Lua function from JavaScript", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Lua("gamma", "calls = 0 Stage.give(function(a, b) calls = calls + 1 return { joined = a .. b.word, n = select('#', a, b) } end)");
        EQUALS(stage.Js("alpha", "globalThis.joiner = Stage.take(); typeof joiner"), "function");
        // It finished before it returned, so its result is the value and not a promise for it.
        EQUALS(stage.Js("alpha", "JSON.stringify(joiner('x', { word: 'y' }))"), R"({"joined":"xy","n":2})");
        EQUALS(stage.Js("alpha", "joiner('again', { word: '' }).joined"), "again");
        EQUALS(stage.LuaValue("gamma", "calls"), "2");
        EQUALS(lua_gettop(stage.Runtime("gamma").State()), 0);
    });

    IT("hands JavaScript a promise for a Lua function that waits", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Lua("gamma", "Stage.give(function(value) sleep(10) if value < 0 then error('negative') end return value * 2 end)");
        EQUALS(stage.Js("alpha", "globalThis.out = []; globalThis.double = Stage.take(); const promise = double(21); promise.then((value) => out.push(value)); promise instanceof Promise"), "true");
        stage.Js("alpha", "double(-1).catch((error) => out.push(error.message.split('\\n')[0]))");
        EQUALS(stage.Js("alpha", "out.length"), "0");
        stage.At(10);
        EQUALS(stage.Js("alpha", "out.join()"), "42,test:1: negative");
    });

    IT("calls a JavaScript function from Lua", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Js("alpha", "globalThis.calls = 0; Stage.give((a, b) => { ++calls; return { joined: a + b.word, n: [a, b].length }; })");
        EQUALS(stage.Lua("gamma", "joiner = Stage.take() kind = type(joiner) local got = joiner('x', { word = 'y' }) result = got.joined .. got.n"), "");
        EQUALS(stage.LuaValue("gamma", "kind"), "function");
        EQUALS(stage.LuaValue("gamma", "result"), "xy2");
        // It finished before it returned, so nothing had to wait: the main thread can call it too.
        EQUALS(stage.LuaValue("gamma", "joiner('again', { word = '' }).joined"), "again");
        EQUALS(stage.Suspended("gamma"), 0u);
        EQUALS(stage.Js("alpha", "calls"), "2");
    });

    IT("suspends Lua until the promise of a JavaScript function settles", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Js("alpha", "Stage.give(async (value) => { await new Promise((resolve) => setTimeout(resolve, 2)); if (value < 0) { throw new Error('negative'); } return value * 2; })");
        EQUALS(stage.Lua("gamma", "double = Stage.take() result = double(21)"), "");
        EQUALS(stage.Lua("gamma", "ok, message = pcall(double, -1)"), "");
        EQUALS(stage.LuaValue("gamma", "result"), "nil");
        EQUALS(stage.Suspended("gamma"), 2u);
        EQUALS(stage.TickUntil([&] {
            return stage.Suspended("gamma") == 0;
        }),
            true);
        EQUALS(stage.LuaValue("gamma", "result"), "42");
        EQUALS(stage.LuaValue("gamma", "ok"), "false");
        EQUALS(stage.LuaValue("gamma", "message:match('^[^\\n]*')"), "Error: negative");

        // Where Lua cannot wait the call raises, and the function runs all the same.
        EQUALS(stage.LuaValue("gamma", "double(1)"), "<error> eval:1: cannot wait here");
    });

    IT("raises in the caller what the function of another runtime raised", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Lua("gamma", "Stage.give(function() error('lua says no') end)");
        EQUALS(stage.Js("alpha", "try { Stage.take()(); 'returned' } catch (error) { (error instanceof Error) + ' ' + error.message.split('\\n')[0] }"), "true test:1: lua says no");

        stage.Js("alpha", "Stage.give(() => { throw new RangeError('javascript says no'); })");
        EQUALS(stage.LuaValue("gamma", "select(2, pcall(Stage.take())):match('^[^\\n]*')"), "RangeError: javascript says no");

        // An argument that cannot cross is the caller's mistake, and the function does not run.
        stage.Js("alpha", "globalThis.ran = 0; Stage.give(() => { ++ran; })");
        EQUALS(stage.LuaValue("gamma", "Stage.take()(1, coroutine.create(print))"), "<error> eval:1: Argument 2: Cannot copy value: a coroutine cannot be copied");
        stage.Lua("gamma", "ran = 0 Stage.give(function() ran = ran + 1 end)");
        EQUALS(stage.Js("alpha", "Stage.take()(1, Symbol('no'))"), "<threw> Error: Argument 1: Cannot copy value: a symbol cannot be copied");
        EQUALS(stage.Js("alpha", "ran"), "0");
        EQUALS(stage.LuaValue("gamma", "ran"), "0");
    });

    IT("gives a function back to its owner as itself", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        // Out of JavaScript, through Lua, and home.
        stage.Js("alpha", "globalThis.original = (value) => value + 1; Stage.give(original)");
        EQUALS(stage.Js("alpha", "Stage.take() === original"), "true");
        EQUALS(stage.Lua("gamma", "standIn = Stage.take() Stage.give({ nested = { standIn } })"), "");
        EQUALS(stage.Js("alpha", "Stage.take().nested[0] === original"), "true");

        // Out of Lua, through JavaScript, and home.
        stage.Lua("gamma", "function original(value) return value + 1 end Stage.give(original)");
        EQUALS(stage.LuaValue("gamma", "rawequal(Stage.take(), original)"), "true");
        EQUALS(stage.Js("alpha", "globalThis.standIn = Stage.take(); Stage.give({ nested: [standIn] })"), "true");
        EQUALS(stage.LuaValue("gamma", "rawequal(Stage.take().nested[1], original)"), "true");

        // A stand-in is the function it stands for, to whoever keeps it.
        stage.Js("alpha", "Stage.keep(standIn)");
        stage.Lua("gamma", "Stage.keep(original)");
        EQUALS(Held().callables.at(0) == Held().callables.at(1), true);
        EQUALS(stage.LuaValue("gamma", "Stage.same(original, original)"), "true");
    });

    IT("passes callbacks along with the arguments, both ways", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.Lua("gamma", "Stage.give(function(callback, value) return callback(value) + 1 end)");
        EQUALS(stage.Js("alpha", "Stage.take()((value) => value * 2, 20)"), "41");

        stage.Js("alpha", "Stage.give((callback, value) => callback(value) + 1)");
        EQUALS(stage.LuaValue("gamma", "Stage.take()(function(value) return value * 2 end, 20)"), "41");
    });

    IT("takes a call on the table a function arrived in for a method call, in Lua", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.CreateLua("delta");
        stage.Js("alpha", "Stage.give({ count: (...args) => args.length, kind: (first) => typeof first, nested: { count: (...args) => args.length } })");
        EQUALS(stage.Lua("gamma", "api = Stage.take() bare = api.count other = { count = api.count }"), "");

        // With a dot or a colon the function gets the same arguments.
        EQUALS(stage.LuaValue("gamma", "api.count(1, 2)"), "2");
        EQUALS(stage.LuaValue("gamma", "api:count(1, 2)"), "2");
        EQUALS(stage.LuaValue("gamma", "api:kind()"), "undefined");
        EQUALS(stage.LuaValue("gamma", "api:kind('text')"), "string");
        EQUALS(stage.LuaValue("gamma", "api.nested:count(1)"), "1");
        // The table is told by what it is, not by how the call was written.
        EQUALS(stage.LuaValue("gamma", "api.count(api, 1)"), "1");

        // Anything else in front is an argument: off the table, in a table of the script, with another table.
        EQUALS(stage.LuaValue("gamma", "bare(1, 2)"), "2");
        EQUALS(stage.LuaValue("gamma", "bare({}, 1)"), "2");
        EQUALS(stage.LuaValue("gamma", "other:count(1)"), "2");
        EQUALS(stage.LuaValue("gamma", "api.nested.count(api, 1)"), "2");
        EQUALS(stage.LuaValue("gamma", "api.count(api.nested, 1)"), "2");

        // The function is the same one in every table it arrives in, and a method of each.
        EQUALS(stage.Lua("gamma", "again = Stage.take()"), "");
        EQUALS(stage.LuaValue("gamma", "rawequal(again.count, api.count)"), "true");
        EQUALS(stage.LuaValue("gamma", "again:count(1) + api:count(1)"), "2");

        // A function that arrives on its own is a method of nothing.
        stage.Js("alpha", "Stage.give((...args) => args.length)");
        EQUALS(stage.LuaValue("gamma", "Stage.take()({}, 1)"), "2");

        // The same for a function of another Lua state. In its own state it is itself, and called as written.
        stage.Lua("delta", "Stage.give({ count = function(...) return select('#', ...) end })");
        EQUALS(stage.LuaValue("gamma", "(function() local theirs = Stage.take() return theirs:count(1, 2) + theirs.count(1, 2) end)()"), "4");
        EQUALS(stage.LuaValue("delta", "(function() local own = Stage.take() return own:count(1) end)()"), "2");

        // A table a builtin fills itself is one its functions arrived in too.
        stage.Js("alpha", "Stage.give((...args) => args.length)");
        EQUALS(stage.LuaValue("gamma", "(function() local box = Stage.box() return box:item(1) + box.item(1) end)()"), "2");

        // Remembering the table keeps neither it nor the function.
        EQUALS(stage.Lua("gamma", "probe = setmetatable({}, { __mode = 'k' }) probe[Stage.take()] = true probe[api] = true probe[again] = true"), "");
        EQUALS(stage.Lua("gamma", "api = nil again = nil"), "");
        lua_gc(stage.Runtime("gamma").State(), LUA_GCCOLLECT);
        lua_gc(stage.Runtime("gamma").State(), LUA_GCCOLLECT);
        EQUALS(stage.LuaValue("gamma", "next(probe)"), "nil");
        EQUALS(lua_gettop(stage.Runtime("gamma").State()), 0);
    });

    IT("calls between two Lua states and between two isolates", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateJs("beta");
        stage.CreateLua("gamma");
        stage.CreateLua("delta");

        stage.Lua("gamma", "home = 'gamma' Stage.give(function(value) return home .. ' got ' .. value end)");
        EQUALS(stage.LuaValue("delta", "Stage.take()('from delta')"), "gamma got from delta");
        stage.Lua("gamma", "Stage.give(function(value) sleep(10) return value + 1 end)");
        EQUALS(stage.Lua("delta", "result = Stage.take()(1)"), "");
        EQUALS(stage.LuaValue("delta", "result"), "nil");
        stage.At(10);
        EQUALS(stage.LuaValue("delta", "result"), "2");

        stage.Js("alpha", "globalThis.home = 'alpha'; Stage.give((value) => home + ' got ' + value)");
        EQUALS(stage.Js("beta", "globalThis.remote = Stage.take(); remote('from beta')"), "alpha got from beta");
        EQUALS(stage.Js("beta", "remote === Stage.take()"), "true");
    });

    IT("gives a runtime one function for a function of another, for as long as it keeps it", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.CreateLua("delta");
        v8::Isolate *alpha = stage.Isolate("alpha");
        stage.Lua("gamma", "Stage.give(function(value) return value + 1 end)");
        const std::weak_ptr<ScriptCallable> given = Held().value.callable;

        // The same function each time it arrives, in both languages.
        EQUALS(stage.Js("alpha", "globalThis.first = Stage.take(); first === Stage.take() && Stage.take() === first"), "true");
        EQUALS(stage.LuaValue("delta", "(function() local first = Stage.take() return rawequal(first, Stage.take()) end)()"), "true");
        EQUALS(Bind::V8StandInCount(alpha), 1u);

        // Arriving on every frame, with nobody keeping it, it leaves nothing behind: what stood for it goes when it
        // is collected, though the function itself is still held where it came from.
        stage.Js("alpha", "globalThis.first = undefined");
        std::string results;
        for (int frame = 0; frame < 200; ++frame) {
            results = stage.Js("alpha", "Stage.take()(" + std::to_string(frame) + ")");
        }
        EQUALS(results, "200");
        EQUALS(Bind::V8StandInCount(alpha) <= 1, true);
        stage.CollectJs("alpha");
        EQUALS(Bind::V8StandInCount(alpha), 0u);
        EQUALS(given.expired(), false);
        EQUALS(stage.Js("alpha", "globalThis.again = Stage.take(); again(41)"), "42");
        EQUALS(Bind::V8StandInCount(alpha), 1u);
        stage.CollectJs("alpha");
        EQUALS(Bind::V8StandInCount(alpha), 1u);

        // Each function has its own, and each goes with it.
        stage.Js("alpha", "globalThis.again = undefined");
        for (int other = 0; other < 50; ++other) {
            stage.Lua("gamma", "Stage.give(function() return " + std::to_string(other) + " end)");
            results = stage.Js("alpha", "Stage.take()()");
        }
        EQUALS(results, "49");
        stage.CollectJs("alpha");
        EQUALS(Bind::V8StandInCount(alpha), 0u);
        // The last of them is the one copy still held. The first went with what stood for it, here and in Lua,
        // where the next arrival is a new function.
        EQUALS(given.expired(), false);
        lua_gc(stage.Runtime("delta").State(), LUA_GCCOLLECT);
        EQUALS(given.expired(), true);
        EQUALS(stage.LuaValue("delta", "(function() local first = Stage.take() return rawequal(first, Stage.take()) and first() end)()"), "49");
        EQUALS(lua_gettop(stage.Runtime("gamma").State()), 0);
    });

    IT("answers for a function whose runtime is gone", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateLua("gamma");
        stage.CreateLua("delta");
        stage.Lua("gamma", "Stage.give(function() return 1 end)");
        stage.Js("alpha", "globalThis.remote = Stage.take()");
        stage.Lua("delta", "remote = Stage.take()");
        stage.LuaEngine().DestroyResourceRuntime("gamma");
        EQUALS(stage.Js("alpha", "remote()"), "<threw> Error: runtime stopped");
        EQUALS(stage.LuaValue("delta", "remote()"), "<error> eval:1: runtime stopped");
        // Built once its runtime is gone, it still is a function, and says the same.
        EQUALS(stage.Js("alpha", "Stage.take()()"), "<threw> Error: runtime stopped");
    });

    IT("keeps to the function references of JavaScript where a value has both", {
        Stage stage;
        stage.CreateJs("alpha");
        stage.CreateJs("beta");
        Framework::Scripting::FunctionReferences *references = stage.Node().GetFunctionReferences();
        stage.Js("alpha", "Stage.keep(() => 'the callable')");

        TransferredValue copied;
        {
            v8::Isolate *isolate = stage.Isolate("alpha");
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = stage.Node().GetResourceRuntime("alpha")->GetContext();
            v8::Context::Scope contextScope(context);
            v8::Local<v8::Value> value = v8::Script::Compile(context, v8::String::NewFromUtf8Literal(isolate, "({ word: 'the reference', read() { return this.word; } })")).ToLocalChecked()->Run(context).ToLocalChecked();
            copied                     = Framework::Scripting::ValueTransfer::Copy(isolate, context, value, references->For(references->FindRuntime(isolate))).GetValue();
        }
        TransferredValue &read = copied.properties.at(1).second;
        EQUALS(read.kind == Kind::Function, true);
        EQUALS(read.reference != 0, true);
        EQUALS(read.callable == nullptr, true);

        const auto rebuildInBeta = [&](bool withReferences) {
            v8::Isolate *isolate = stage.Isolate("beta");
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = stage.Node().GetResourceRuntime("beta")->GetContext();
            v8::Context::Scope contextScope(context);
            v8::TryCatch tryCatch(isolate);
            v8::Local<v8::Value> rebuilt;
            const Framework::Scripting::TransferFunctions functions = withReferences ? references->For(references->FindRuntime(isolate)) : Framework::Scripting::TransferFunctions {};
            if (!Framework::Scripting::ValueTransfer::Rebuild(isolate, context, copied, functions).ToLocal(&rebuilt)) {
                v8::String::Utf8Value text(isolate, tryCatch.Exception());
                return std::string("<threw> ") + *text;
            }
            context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "received"), rebuilt).Check();
            return std::string();
        };

        // As before this value had a callable to offer: with the references it is their stand-in, which runs the
        // function on its object, and without them it cannot be built.
        EQUALS(rebuildInBeta(true), "");
        EQUALS(stage.Js("beta", "received.read()"), "the reference");
        EQUALS(rebuildInBeta(false), "<threw> Error: A function cannot be rebuilt here");

        read.callable = Held().callables.at(0);
        EQUALS(rebuildInBeta(true), "");
        EQUALS(stage.Js("beta", "received.read()"), "the reference");
        EQUALS(rebuildInBeta(false), "");
        EQUALS(stage.Js("beta", "received.read()"), "the callable");

        // With a callable alone the references of the caller have nothing to import.
        read.reference = 0;
        read.retainer.reset();
        EQUALS(rebuildInBeta(true), "");
        EQUALS(stage.Js("beta", "received.read()"), "the callable");
    });
});
