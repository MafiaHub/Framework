/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "bind_class_lua_ut.h" // Token, a class that cannot be copied
#include "lua_runtime_ut.h"
#include "lua_test_helpers.h"
#include "node_test_helpers.h"

#include "core_modules.h"
#include "scripting/bind/call.h"
#include "scripting/bind/class.h"
#include "scripting/bind/namespace.h"
#include "scripting/bind/value.h"
#include "scripting/builtins/builtins.h"
#include "scripting/builtins/vector3.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/module.h"
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/scripting_catalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace BindCallTest {
    namespace Bind = Framework::Scripting::Bind;
    using Bind::Call;
    using Bind::Side;
    using Bind::Value;
    using Bind::ValueKind;
    using Framework::Scripting::LuaRuntime;
    using Framework::Scripting::LuaRuntimeOptions;
    using Framework::Scripting::ResourceManager;

    class Counter {
      public:
        explicit Counter(int value): _value(value) {}

        int get() const {
            return _value;
        }

        void set(int value) {
            _value = value;
        }

      private:
        int _value = 0;
    };

    // A counter behind a second base, so that its Counter part does not lie at its own address.
    struct Label {
        std::string text = "step";
    };

    class Stepper final
        : public Label
        , public Counter {
      public:
        explicit Stepper(int value): Counter(value) {}
    };
} // namespace BindCallTest

FW_SCRIPT_CLASS(BindCallTest::Counter, "CallCounter");
FW_SCRIPT_CLASS(BindCallTest::Stepper, "CallStepper");

namespace BindCallTest {
    // Bumped by the functions that go on after they threw: once for each run that reached its end.
    inline int reachedEnd = 0;

    // The manager the test registered, for Probe.manager() to compare with.
    inline ResourceManager *expectedManager = nullptr;

    // Counts the functions a Lua error unwound, and those that got to their end all the same.
    inline int unwound = 0;

    struct Unwinding final {
        ~Unwinding() {
            ++unwound;
        }
    };

    // What Probe.keep found left in the object it had wrapped with a move.
    inline size_t leftAfterWrap = 0;

    // Whether Probe.spell had thrown once it had spelled its first argument.
    inline bool thrownAfterDisplay = false;

    inline const char *KindName(ValueKind kind) {
        switch (kind) {
        case ValueKind::Nil: return "Nil";
        case ValueKind::Boolean: return "Boolean";
        case ValueKind::Number: return "Number";
        case ValueKind::String: return "String";
        case ValueKind::Function: return "Function";
        case ValueKind::Array: return "Array";
        case ValueKind::Object: return "Object";
        case ValueKind::Bound: return "Bound";
        case ValueKind::Other: return "Other";
        }
        return "?";
    }

    inline const Bind::Class<Counter> &CounterDefinition() {
        using Bind::Doc;
        using Bind::TypeOf;
        namespace Type               = Bind::Type;
        static const auto definition = Bind::Class<Counter>("CallCounter", "Counts, through context functions.")
                                           .Constructor<int>(Doc("Creates a counter.").Param("value", "Initial value."))
                                           .Method("get", &Counter::get, Doc("Reads the counter.").Returns("Current value."))
                                           .Method(
                                               "bump",
                                               [](Call &call) {
                                                   Counter *self = call.Self<Counter>();
                                                   if (self == nullptr) {
                                                       return;
                                                   }
                                                   self->set(self->get() + static_cast<int>(call.Arg(0).AsInteger().value_or(1)));
                                                   call.Return(call.ArgCount());
                                               },
                                               Doc("Adds to the counter.").OptionalParam("amount", TypeOf<int>(), "Amount to add, 1 when absent.").ReturnType(TypeOf<int>()).Returns("How many arguments it was given."))
                                           .Method(
                                               "chain",
                                               [](Call &call) {
                                                   if (Counter *self = call.Self<Counter>()) {
                                                       self->set(self->get() + 1);
                                                   }
                                                   call.Return(call.This());
                                               },
                                               Doc("Adds one.").ReturnType(Type::This()).Returns("This counter."))
                                           .Method(
                                               "serverBump",
                                               [](Call &call) {
                                                   if (Counter *self = call.Self<Counter>()) {
                                                       self->set(self->get() + 100);
                                                   }
                                               },
                                               Doc("Adds a hundred."), Side::Server)
                                           .Method(
                                               "late",
                                               [](Call &call) {
                                                   // What it makes first is where Lua would have put a receiver.
                                                   const Value made = call.Wrap(Counter(1000));
                                                   Counter *self    = call.Self<Counter>();
                                                   if (self != nullptr) {
                                                       self->set(self->get() + 1);
                                                   }
                                                   call.Return(std::string(self != nullptr ? "receiver" : "none") + ":" + KindName(call.This().Kind()) + ":" + KindName(made.Kind()));
                                               },
                                               Doc("Makes a counter, then looks for its receiver.").ReturnType(TypeOf<std::string>()))
                                           .Static(
                                               "receiver",
                                               [](Call &call) {
                                                   call.Return(call.Self<Counter>() != nullptr);
                                               },
                                               Doc("Tells whether a static has a receiver.").ReturnType(TypeOf<bool>()));
        return definition;
    }

    inline const Bind::Class<Stepper> &StepperDefinition() {
        using Bind::Doc;
        CounterDefinition();
        static const auto definition = Bind::Class<Stepper>("CallStepper", "A counter with a label.").Inherit<Counter>().Constructor<int>(Doc("Creates a stepper.").Param("value", "Initial value."));
        return definition;
    }

    inline std::string Flags(const Value &value) {
        std::string flags;
        flags += value.IsNil() ? 'n' : '-';
        flags += value.IsBoolean() ? 'b' : '-';
        flags += value.IsNumber() ? 'd' : '-';
        flags += value.IsString() ? 's' : '-';
        flags += value.IsFunction() ? 'f' : '-';
        flags += value.IsArray() ? 'a' : '-';
        flags += value.IsObject() ? 'o' : '-';
        flags += value.IsBound() ? 'c' : '-';
        return flags;
    }

    // What Probe.hold() functions hold: alive while any of them is.
    inline std::shared_ptr<int> &HeldSlot() {
        static std::shared_ptr<int> held;
        return held;
    }

    inline std::shared_ptr<int> Held() {
        return HeldSlot();
    }

    inline const Bind::Namespace &ProbeDefinition() {
        using Bind::Doc;
        using Bind::TypeOf;
        namespace Type = Bind::Type;
        static const Bind::Namespace definition =
            Bind::Namespace("Probe", "Exercises the dynamic interface.")
                .Function(
                    "kind",
                    [](Call &call) {
                        call.Return(KindName(call.Arg(0).Kind()));
                    },
                    Doc("Names the kind of a value.").Param("value", Type::Any(), "Value to inspect.").ReturnType(TypeOf<std::string>()).Returns("Name of its kind."))
                .Function(
                    "flags",
                    [](Call &call) {
                        call.Return(Flags(call.Arg(0)));
                    },
                    Doc("Spells the kind shorthands of a value.").Param("value", Type::Any(), "Value to inspect.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "sum",
                    [](Call &call) {
                        double total = 0.0;
                        for (size_t i = 0; i < call.ArgCount(); ++i) {
                            total += call.Arg(i).AsNumber();
                        }
                        call.Return(total);
                    },
                    Doc("Adds numbers up.").RestParam("values", Type::Custom("number[]", "number"), "Numbers to add.").ReturnType(TypeOf<double>()).Returns("Their sum."))
                .Function(
                    "argc",
                    [](Call &call) {
                        call.Return(call.Arg(call.ArgCount()).IsNil() && call.Arg(99).IsNil() ? static_cast<int64_t>(call.ArgCount()) : int64_t {-1});
                    },
                    Doc("Counts its arguments.").RestParam("values", Type::Custom("unknown[]", "any"), "Anything.").ReturnType(TypeOf<int>()))
                .Function(
                    "echo",
                    [](Call &call) {
                        call.Return(call.Arg(0));
                    },
                    Doc("Returns its argument.").Param("value", Type::Any(), "Value to return.").ReturnType(Type::Any()))
                .Function(
                    "nothing",
                    [](Call &) {},
                    Doc("Returns nothing."))
                .Function(
                    "none",
                    [](Call &call) {
                        call.Return(call.Nil());
                    },
                    Doc("Returns no value on purpose.").ReturnType(Type::Any()))
                .Function(
                    "last",
                    [](Call &call) {
                        call.Return(1);
                        call.Return("last");
                    },
                    Doc("Returns twice.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "make",
                    [](Call &call) {
                        Value list = call.NewArray();
                        list.Append(call.Integer(1));
                        list.Append(call.Integer(2));
                        list.Append(call.Integer(3));
                        Value nested = call.NewObject();
                        nested.Set("b", call.String("x"));
                        Value made = call.NewObject();
                        made.Set("a", call.Integer(1));
                        made.Set("list", list);
                        made.Set("nested", nested);
                        call.Return(made);
                    },
                    Doc("Builds an object.").ReturnType(Type::Custom("{ a: number; list: number[]; nested: { b: string } }", "table")))
                .Function(
                    "scalars",
                    [](Call &call) {
                        Value list = call.NewArray();
                        list.Append(call.Boolean(true));
                        list.Append(call.Number(1.5));
                        list.Append(call.Integer(-7));
                        list.Append(call.String(std::string_view("a\0b", 3)));
                        call.Return(list);
                    },
                    Doc("Builds one value of each scalar kind.").ReturnType(Type::Custom("unknown[]", "any[]")))
                .Function(
                    "keys",
                    [](Call &call) {
                        Value keys = call.NewArray();
                        for (const std::string &key : call.Arg(0).Keys()) {
                            keys.Append(call.String(key));
                        }
                        call.Return(keys);
                    },
                    Doc("Lists the keys of an object.").Param("object", Type::Any(), "Object to list.").ReturnType(TypeOf<std::vector<std::string>>()))
                .Function(
                    "get",
                    [](Call &call) {
                        call.Return(call.Arg(0).Get(call.Arg(1).AsString()));
                        ++reachedEnd;
                    },
                    Doc("Reads a key.").Param("object", Type::Any(), "Object to read.").Param("key", TypeOf<std::string>(), "Key to read.").ReturnType(Type::Any()))
                .Function(
                    "set",
                    [](Call &call) {
                        call.Arg(0).Set(call.Arg(1).AsString(), call.Arg(2));
                        call.Return(call.Arg(0));
                    },
                    Doc("Writes a key.").Param("object", Type::Any(), "Object to write.").Param("key", TypeOf<std::string>(), "Key to write.").Param("value", Type::Any(), "Value to store.").ReturnType(Type::Any()))
                .Function(
                    "length",
                    [](Call &call) {
                        call.Return(call.Arg(0).Length());
                    },
                    Doc("Measures an array.").Param("array", Type::Any(), "Array to measure.").ReturnType(TypeOf<int>()))
                .Function(
                    "at",
                    [](Call &call) {
                        call.Return(call.Arg(0).At(static_cast<size_t>(call.Arg(1).AsInteger().value_or(0))));
                    },
                    Doc("Reads an element.").Param("array", Type::Any(), "Array to read.").Param("index", TypeOf<int>(), "Zero-based index.").ReturnType(Type::Any()))
                .Function(
                    "append",
                    [](Call &call) {
                        call.Arg(0).Append(call.Arg(1));
                        call.Return(call.Arg(0));
                    },
                    Doc("Appends an element.").Param("array", Type::Any(), "Array to grow.").Param("value", Type::Any(), "Element to add.").ReturnType(Type::Any()))
                .Function(
                    "integer",
                    [](Call &call) {
                        const std::optional<int64_t> integer = call.Arg(0).AsInteger();
                        call.Return(integer ? std::to_string(*integer) : std::string("none"));
                    },
                    Doc("Reads an integer.").Param("value", Type::Any(), "Value to read.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "strict",
                    [](Call &call) {
                        const Value value = call.Arg(0);
                        call.Return("[" + value.AsString() + "]" + (value.AsBoolean() ? "t" : "f") + (value.AsNumber() == 0.0 ? "0" : "n"));
                    },
                    Doc("Reads a value as a string, a boolean and a number.").Param("value", Type::Any(), "Value to read.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "display",
                    [](Call &call) {
                        call.Return(call.Arg(0).ToDisplayString());
                        ++reachedEnd;
                    },
                    Doc("Spells a value.").Param("value", Type::Any(), "Value to spell.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "isList",
                    [](Call &call) {
                        call.Return(call.Arg(0).IsList());
                    },
                    Doc("Tells whether a value passes for a list.").Param("value", Type::Any(), "Value to inspect.").ReturnType(TypeOf<bool>()))
                .Function(
                    "null",
                    [](Call &call) {
                        call.Return(call.Null());
                    },
                    Doc("Returns null.").ReturnType(Type::Any()))
                .Function(
                    "nullKind",
                    [](Call &call) {
                        Value object = call.NewObject();
                        object.Set("k", call.Null());
                        call.Return(std::string(KindName(call.Null().Kind())) + KindName(object.Get("k").Kind()) + std::to_string(object.Keys().size()));
                    },
                    Doc("Names the kind of null, alone and stored.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "thisKind",
                    [](Call &call) {
                        call.Return(KindName(call.This().Kind()));
                    },
                    Doc("Names the kind of its receiver.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "capture",
                    [](Call &call) {
                        // The mistake a handle has to survive: it is kept beyond its call.
                        call.Return(call.NewFunction([kept = call.Arg(0)](Call &inner) {
                            inner.Return(kept);
                        }));
                    },
                    Doc("Makes a function that returns a value it should not have kept.").Param("value", Type::Any(), "Value to keep.").ReturnType(Type::Custom("() => unknown", "fun(): any")))
                .Function(
                    "stale",
                    [](Call &call) {
                        call.Return(call.NewFunction([kept = call.Arg(0)](Call &inner) {
                            Value object = inner.NewObject();
                            object.Set("k", inner.Integer(1));
                            object.Set("k", kept);
                            Value list = inner.NewArray();
                            list.Append(kept);
                            inner.Return(std::string(KindName(kept.Kind())) + "[" + kept.AsString() + kept.ToDisplayString() + "]" + std::to_string(kept.Length()) + std::to_string(kept.Keys().size()) + KindName(kept.Get("a").Kind()) + KindName(kept.At(0).Kind()) + ":"
                                         + KindName(object.Get("k").Kind()) + KindName(list.At(0).Kind()));
                        }));
                    },
                    Doc("Makes a function that reads a value it should not have kept.").Param("value", Type::Any(), "Value to keep.").ReturnType(Type::Custom("(...args: unknown[]) => string", "fun(...): string")))
                .Function(
                    "nested",
                    [](Call &call) {
                        // The object spells itself by calling `peek`, which runs inside this call and reads its handle.
                        const Value outer = call.Arg(1);
                        call.Arg(0).Set("peek", call.NewFunction([outer](Call &inner) {
                            inner.Return(KindName(outer.Kind()));
                        }));
                        const std::string during = call.Arg(0).ToDisplayString();
                        call.Return(during + ":" + KindName(outer.Kind()));
                    },
                    Doc("Reads a handle of this call from a call nested in it, then from this one.").Param("object", Type::Any(), "Object that spells itself with peek().").Param("value", Type::Any(), "Value to read.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "spell",
                    [](Call &call) {
                        call.Arg(0).ToDisplayString();
                        thrownAfterDisplay = call.HasThrown();
                        call.Arg(1).Get("a");
                        ++reachedEnd;
                    },
                    Doc("Spells a value, then reads a key of another.").Param("value", Type::Any(), "Value to spell.").Param("object", Type::Any(), "Object to read.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "keep",
                    [](Call &call) {
                        Stepper stepper(1);
                        stepper.text = std::string(64, 's');
                        call.Wrap(std::move(stepper));
                        leftAfterWrap = stepper.text.size();
                    },
                    Doc("Wraps a stepper and notes what is left of it."))
                .Function(
                    "adder",
                    [](Call &call) {
                        const int64_t amount = call.Arg(0).AsInteger().value_or(0);
                        call.Return(call.NewFunction([amount](Call &inner) {
                            inner.Return(amount + inner.Arg(0).AsInteger().value_or(0));
                        }));
                    },
                    Doc("Makes a function that adds.").Param("amount", TypeOf<int>(), "What it adds.").ReturnType(Type::Custom("(value: number) => number", "fun(value: integer): integer")))
                .Function(
                    "hold",
                    [](Call &call) {
                        // The function keeps `held` alive for as long as it lives itself.
                        call.Return(call.NewFunction([held = Held()](Call &inner) {
                            inner.Return(*held);
                        }));
                    },
                    Doc("Makes a function that holds a value.").ReturnType(Type::Custom("() => number", "fun(): integer")))
                .Function(
                    "failType",
                    [](Call &call) {
                        const bool before = call.HasThrown();
                        call.ThrowTypeError("Probe.failType: expected (text)");
                        call.ThrowError("Probe.failType: thrown second");
                        call.Return(1);
                        if (!before && call.HasThrown()) {
                            ++reachedEnd;
                        }
                    },
                    Doc("Fails as a wrong argument does."))
                .Function(
                    "fail",
                    [](Call &call) {
                        call.Return(1);
                        call.ThrowError("Probe.fail: network peer unavailable");
                        call.ThrowTypeError("Probe.fail: thrown second");
                        ++reachedEnd;
                    },
                    Doc("Fails as a missing subsystem does."))
                .Function(
                    "raise",
                    [](Call &) {
                        throw std::runtime_error("Probe.raise: exception");
                    },
                    Doc("Throws a C++ exception."))
                .Function(
                    "bound",
                    [](Call &call) {
                        const Counter *counter = call.Arg(0).AsBound<Counter>();
                        call.Return(counter != nullptr ? counter->get() : -1);
                    },
                    Doc("Reads a counter.").Param("counter", Type::Any(), "Counter to read.").ReturnType(TypeOf<int>()))
                .Function(
                    "wrap",
                    [](Call &call) {
                        call.Return(call.Wrap(Counter(static_cast<int>(call.Arg(0).AsInteger().value_or(0)))));
                        ++reachedEnd;
                    },
                    Doc("Makes a counter.").Param("value", TypeOf<int>(), "Initial value.").ReturnType(Type::Class("CallCounter")))
                .Function(
                    "copy",
                    [](Call &call) {
                        if (const Counter *counter = call.Arg(0).AsBound<Counter>()) {
                            call.Return(call.Wrap(*counter));
                        }
                    },
                    Doc("Copies a counter.").Param("counter", Type::Class("CallCounter"), "Counter to copy.").ReturnType(Type::Class("CallCounter")))
                .Function(
                    "side",
                    [](Call &call) {
                        call.Return(call.GetSide() == Side::Client ? "client" : "server");
                    },
                    Doc("Names the side of the runtime.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "language",
                    [](Call &call) {
                        call.Return(call.GetLanguage() == Framework::Scripting::Language::Lua ? "lua" : "javascript");
                    },
                    Doc("Names the language of the caller.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "resource",
                    [](Call &call) {
                        call.Return(call.ResourceName());
                    },
                    Doc("Names the calling resource.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "manager",
                    [](Call &call) {
                        const ResourceManager *manager = call.GetResourceManager();
                        call.Return(manager == nullptr ? "none" : manager == expectedManager ? "registered" : "other");
                    },
                    Doc("Tells which resource manager the call has.").ReturnType(TypeOf<std::string>()))
                .Function(
                    "twice",
                    [](int value) {
                        return value * 2;
                    },
                    Doc("Doubles a number.").Param("value", "Number to double.").Returns("Twice the number."))
                .Function(
                    "serverOnly",
                    [](Call &call) {
                        call.Return(true);
                    },
                    Doc("Exists on the server.").ReturnType(TypeOf<bool>()), Side::Server)
                .Function(
                    "clientOnly",
                    [](Call &call) {
                        call.Return(true);
                    },
                    Doc("Exists on the client.").ReturnType(TypeOf<bool>()), Side::Client)
                .Constant("answer", 42.0, "The answer.")
                .Constant("ratio", 0.5, "A ratio.")
                .Constant("label", std::string("probe"), "A label.")
                .Constant("serverLabel", std::string("server"), "A label of the server.", Side::Server)
                .Constant("enabled", true, "A switch.")
                .Constant("count", 3, "A count.")
                .Constant("word", "literal", "A word.");
        return definition;
    }

    // The resource manager Probe.manager() finds when none is registered for the runtime.
    inline const char *UnregisteredManager() {
        Framework::Scripting::ScriptingModule *module = Framework::CoreModules::GetScriptingModule();
        return module != nullptr && module->GetResourceManager() != nullptr ? "other" : "none";
    }

    inline LuaRuntimeOptions LuaOptions(Side side) {
        LuaRuntimeOptions options = LuaRuntimeTest::Options();
        options.resourceName      = "lua-probe";
        options.side              = side;
        return options;
    }

    // A JavaScript runtime and a Lua runtime of one side, each with the value types, the counters and Probe as
    // globals. Lua also has dump(value), which spells a table the way JSON.stringify spells an object.
    class Runtimes final {
      public:
        explicit Runtimes(Side side = Side::Server, bool withCounters = true): _lua(LuaOptions(side)) {
            using namespace Framework::Scripting;
            static int count = 0;
            _engine.Init();
            std::string error;
            _js          = _engine.CreateRuntime(error);
            _catalogName = "bind_call/" + std::to_string(++count);
            SetScriptingCatalog(Isolate(), _catalogName);
            SetScriptingEnvironment(Isolate(), side == Side::Client);

            Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                v8::Local<v8::Object> global = context->Global();
                Builtins::RegisterValueTypes(isolate, global);
                if (withCounters) {
                    _counter = CounterDefinition().MaterialiseV8(isolate, Catalog(), side);
                    _stepper = StepperDefinition().MaterialiseV8(isolate, Catalog(), side);
                    global->Set(context, v8pp::to_v8(isolate, "CallCounter"), _counter->js_function_template()->GetFunction(context).ToLocalChecked()).Check();
                    global->Set(context, v8pp::to_v8(isolate, "CallStepper"), _stepper->js_function_template()->GetFunction(context).ToLocalChecked()).Check();
                }
                ProbeDefinition().MaterialiseV8(isolate, context, global, Catalog(), side);
            });

            lua_State *L = _lua.State();
            Builtins::RegisterValueTypesLua(L, side);
            if (withCounters) {
                CounterDefinition().RegisterLuaGlobal(L, side);
                StepperDefinition().RegisterLuaGlobal(L, side);
            }
            ProbeDefinition().RegisterLuaGlobal(L, side);
            LuaRuntimeTest::Run(_lua, R"lua(
                function dump(value)
                    if type(value) == 'string' then return '"' .. value .. '"' end
                    if type(value) ~= 'table' then return tostring(value) end
                    local parts = {}
                    if #value > 0 then
                        for index = 1, #value do parts[index] = dump(value[index]) end
                        return '[' .. table.concat(parts, ',') .. ']'
                    end
                    local keys = {}
                    for key in pairs(value) do keys[#keys + 1] = key end
                    table.sort(keys)
                    for index, key in ipairs(keys) do parts[index] = '"' .. key .. '":' .. dump(value[key]) end
                    return '{' .. table.concat(parts, ',') .. '}'
                end
            )lua");
        }

        ~Runtimes() {
            Bind::SetCallResourceManager(Isolate(), nullptr);
            Bind::SetCallResourceManager(&_lua, nullptr);
            _stepper.reset();
            _counter.reset();
            // The caches and the catalog are keyed by isolate address, which a later isolate may reuse.
            Framework::Scripting::Builtins::UnregisterAll(Isolate());
            Framework::Scripting::ClearScriptingCatalog(Isolate());
            _js.reset();
            _engine.Shutdown();
        }

        v8::Isolate *Isolate() const {
            return _js->GetIsolate();
        }

        LuaRuntime &LuaSide() {
            return _lua;
        }

        Framework::Scripting::NodeEngine &Engine() {
            return _engine;
        }

        template <typename Action>
        void Enter(Action action) {
            v8::Isolate *isolate = Isolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _js->GetContext();
            v8::Context::Scope contextScope(context);
            action(isolate, context);
        }

        // What `source` gives in JavaScript, as String() spells it, or "<threw> " and the exception.
        std::string Js(const std::string &source) {
            return NodeTest::Eval(*_js, source);
        }

        // What the expression gives in Lua, as tostring() spells it, or "<error> " and the first line of the error.
        std::string Lua(const std::string &expression) {
            return LuaRuntimeTest::Eval(_lua, expression);
        }

        // Empty when `js` and `lua` both give `expected`; otherwise what each of them gave instead.
        std::string Both(const std::string &js, const std::string &lua, const std::string &expected) {
            std::string mismatches;
            if (const std::string got = Js(js); got != expected) {
                mismatches += "\n\tjs  `" + js + "` gave \"" + got + "\", expected \"" + expected + "\"";
            }
            if (const std::string got = Lua(lua); got != expected) {
                mismatches += "\n\tlua `" + lua + "` gave \"" + got + "\", expected \"" + expected + "\"";
            }
            return mismatches;
        }

        // The same source in both languages.
        std::string Both(const std::string &source, const std::string &expected) {
            return Both(source, source, expected);
        }

        v8pp::metadata::registry &Catalog() const {
            return v8pp::metadata::catalog(_catalogName);
        }

      private:
        Framework::Scripting::NodeEngine _engine;
        std::unique_ptr<Framework::Scripting::NodeRuntime> _js;
        LuaRuntime _lua;
        std::string _catalogName;
        std::unique_ptr<v8pp::class_<Counter>> _counter;
        std::unique_ptr<v8pp::class_<Stepper>> _stepper;
    };

    // The symbol `name` of a catalog as it is exported, or null.
    inline nlohmann::json SymbolOf(const v8pp::metadata::registry &catalog, const std::string &name) {
        for (const nlohmann::json &symbol : nlohmann::json::parse(v8pp::metadata::to_json(catalog))["symbols"]) {
            if (symbol["name"] == name) {
                return symbol;
            }
        }
        return nullptr;
    }

    // The message of the std::logic_error `action` throws, or empty when it does not throw one.
    template <typename Action>
    std::string LogicErrorOf(Action action) {
        try {
            action();
        }
        catch (const std::logic_error &error) {
            return error.what();
        }
        return {};
    }
} // namespace BindCallTest

MODULE(bind_call, {
    using namespace BindCallTest;
    using Bind::Doc;
    using Bind::TypeOf;
    namespace Type = Bind::Type;

    // --- Describing a context function ---

    IT("describes a context function from its Doc alone", {
        const Doc doc = Doc("Writes a line.").Param("level", TypeOf<std::string>(), "How loud.").OptionalParam("tag", TypeOf<int>(), "A tag.").RestParam("values", Type::Custom("unknown[]", "any"), "What to write.").ReturnType(TypeOf<bool>()).Returns("Whether it was written.");
        const Bind::FunctionDesc desc = Bind::DescribeFunction<Bind::NativeFunction>("write", doc, true, Side::Client);

        EQUALS(desc.name, "write");
        EQUALS(desc.description, "Writes a line.");
        EQUALS(desc.isStatic, true);
        EQUALS(desc.side == Side::Client, true);
        EQUALS(Bind::ToTypeScript(desc.returns), "boolean");
        EQUALS(desc.returnsDescription, "Whether it was written.");
        EQUALS(desc.params.size(), 3u);
        EQUALS(desc.params[0].name + ":" + Bind::ToTypeScript(desc.params[0].type) + ":" + desc.params[0].description, "level:string:How loud.");
        EQUALS(desc.params[0].optional || desc.params[0].rest, false);
        EQUALS(desc.params[1].name + ":" + Bind::ToLuaAnnotation(desc.params[1].type), "tag:integer");
        EQUALS(desc.params[1].optional && !desc.params[1].rest, true);
        EQUALS(desc.params[2].name + ":" + Bind::ToTypeScript(desc.params[2].type) + ":" + Bind::ToLuaAnnotation(desc.params[2].type), "values:unknown[]:any");
        EQUALS(desc.params[2].rest && !desc.params[2].optional, true);

        const auto lambda = [](Call &) {};
        EQUALS(Bind::ToTypeScript(Bind::DescribeFunction<decltype(lambda)>("quiet", Doc("Does nothing."), false, Side::Shared).returns), "void");
    });

    IT("refuses a Doc a function cannot have", {
        EQUALS(LogicErrorOf([] {
            Bind::DescribeFunction<Bind::NativeFunction>("Probe.write", Doc("Writes.").Param("level", "How loud."), true, Side::Shared);
        }),
            "Bind: 'Probe.write' is a context function, so its Doc gives every parameter a type, and 'level' has none");
        EQUALS(LogicErrorOf([] {
            Doc("Writes.").RestParam("values", Type::Any(), "What to write.").Param("level", "How loud.");
        }),
            "Bind::Doc: 'level' comes after the rest parameter 'values', which is the last one");
        EQUALS(LogicErrorOf([] {
            Bind::DescribeFunction<int (*)(int)>("Probe.twice", Doc("Doubles.").RestParam("values", Type::Any(), "Numbers."), true, Side::Shared);
        }),
            "Bind: 'Probe.twice' documents the rest parameter 'values', which only a context function takes");
    });

    IT("keeps the type a Doc gives a parameter of a typed callable", {
        const Bind::FunctionDesc desc = Bind::DescribeFunction<int (*)(int)>("pick", Doc("Picks.").Param("which", Type::Custom("0 | 1", "integer"), "Which one."), true, Side::Shared);
        EQUALS(Bind::ToTypeScript(desc.params[0].type), "0 | 1");
    });

    // --- Namespace, as a definition ---

    IT("describes a namespace and records it in the catalog", {
        const Bind::ClassDesc &desc = ProbeDefinition().Describe();
        EQUALS(desc.name, "Probe");
        EQUALS(desc.description, "Exercises the dynamic interface.");
        EQUALS(desc.isNamespace, true);
        EQUALS(Bind::ApiCatalog::Get().Find("Probe") == &desc, true);

        const auto sum = std::find_if(desc.functions.begin(), desc.functions.end(), [](const Bind::FunctionDesc &function) {
            return function.name == "sum";
        });
        EQUALS(sum != desc.functions.end(), true);
        EQUALS(sum->isStatic, true);
        EQUALS(sum->params.size(), 1u);
        EQUALS(sum->params[0].rest, true);
        EQUALS(Bind::ToLuaAnnotation(sum->returns), "number");

        const auto twice = std::find_if(desc.functions.begin(), desc.functions.end(), [](const Bind::FunctionDesc &function) {
            return function.name == "twice";
        });
        EQUALS(twice != desc.functions.end(), true);
        EQUALS(twice->params[0].name + ":" + Bind::ToLuaAnnotation(twice->params[0].type) + "->" + Bind::ToLuaAnnotation(twice->returns), "value:integer->integer");

        const auto serverOnly = std::find_if(desc.functions.begin(), desc.functions.end(), [](const Bind::FunctionDesc &function) {
            return function.name == "serverOnly";
        });
        EQUALS(serverOnly != desc.functions.end() && serverOnly->side == Side::Server, true);

        EQUALS(desc.properties.size(), 7u);
        EQUALS(desc.properties[4].name + ":" + Bind::ToTypeScript(desc.properties[4].type) + ":" + Bind::ToLuaAnnotation(desc.properties[4].type), "enabled:boolean:boolean");
        EQUALS(desc.properties[5].name + ":" + Bind::ToTypeScript(desc.properties[5].type), "count:number");
        EQUALS(desc.properties[6].name + ":" + Bind::ToTypeScript(desc.properties[6].type), "word:string");
        EQUALS(desc.properties[0].name + ":" + Bind::ToTypeScript(desc.properties[0].type) + ":" + desc.properties[0].description, "answer:number:The answer.");
        EQUALS(desc.properties[0].readonly && desc.properties[0].isStatic, true);
        EQUALS(desc.properties[2].name + ":" + Bind::ToTypeScript(desc.properties[2].type), "label:string");
        EQUALS(desc.properties[3].side == Side::Server, true);
    });

    IT("refuses a second member of the same name, function or constant", {
        Bind::Namespace twice("ProbeTwice", "Declares members twice.");
        twice.Function("run", [](Call &) {}, Doc("Runs.")).Constant("limit", 1.0, "A limit.");

        const std::string expected = "Bind: 'ProbeTwice.run' is declared twice, and a member cannot be overloaded";
        EQUALS(LogicErrorOf([&] {
            twice.Function("run", [](Call &) {}, Doc("Runs again."));
        }),
            expected);
        EQUALS(LogicErrorOf([&] {
            twice.Function(
                "run",
                [](int value) {
                    return value;
                },
                Doc("Runs typed.").Param("value", "A value."));
        }),
            expected);
        EQUALS(LogicErrorOf([&] {
            twice.Constant("run", 2.0, "A constant.");
        }),
            expected);
        EQUALS(LogicErrorOf([&] {
            twice.Constant("run", std::string("text"), "A constant.");
        }),
            expected);
        EQUALS(LogicErrorOf([&] {
            twice.Function("limit", [](Call &) {}, Doc("Limits."));
        }),
            "Bind: 'ProbeTwice.limit' is declared twice, and a member cannot be overloaded");
        EQUALS(twice.Describe().functions.size(), 1u);
        EQUALS(twice.Describe().properties.size(), 1u);
    });

    // --- Values ---

    IT("tells the kind of a value alike in both languages", {
        Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.kind()", "Nil");
        mismatches += runtimes.Both("Probe.kind(undefined)", "Probe.kind(nil)", "Nil");
        mismatches += runtimes.Both("Probe.kind(null)", "Probe.kind(nil, 1)", "Nil");
        mismatches += runtimes.Both("Probe.kind(true)", "Boolean");
        mismatches += runtimes.Both("Probe.kind(false)", "Boolean");
        mismatches += runtimes.Both("Probe.kind(0)", "Number");
        mismatches += runtimes.Both("Probe.kind(1.5)", "Number");
        mismatches += runtimes.Both("Probe.kind(0 / 0)", "Number");
        mismatches += runtimes.Both("Probe.kind('')", "String");
        mismatches += runtimes.Both("Probe.kind('text')", "String");
        mismatches += runtimes.Both("Probe.kind(Probe.kind)", "Function");
        mismatches += runtimes.Both("Probe.kind(function () {})", "Probe.kind(function() end)", "Function");
        mismatches += runtimes.Both("Probe.kind(Probe.adder(1))", "Function");
        mismatches += runtimes.Both("Probe.kind([1])", "Probe.kind({1})", "Array");
        mismatches += runtimes.Both("Probe.kind([1, 'a', [2]])", "Probe.kind({1, 'a', {2}})", "Array");
        mismatches += runtimes.Both("Probe.kind({})", "Object");
        mismatches += runtimes.Both("Probe.kind({ a: 1 })", "Probe.kind({ a = 1 })", "Object");
        mismatches += runtimes.Both("Probe.kind({ 1: 'a', b: 2 })", "Probe.kind({ 'a', b = 2 })", "Object");
        mismatches += runtimes.Both("Probe.kind(Probe)", "Object");
        mismatches += runtimes.Both("Probe.kind(new Vector3(1, 2, 3))", "Probe.kind(Vector3.new(1, 2, 3))", "Bound");
        mismatches += runtimes.Both("Probe.kind(new CallCounter(1))", "Probe.kind(CallCounter.new(1))", "Bound");
        mismatches += runtimes.Both("Probe.kind(new CallStepper(1))", "Probe.kind(CallStepper.new(1))", "Bound");
        mismatches += runtimes.Both("Probe.kind(Symbol('s'))", "Probe.kind(coroutine.create(function() end))", "Other");
        EQUALS(mismatches, "");

        // Where the languages have no common ground: an empty array is only told from an empty object in JavaScript,
        // and a Lua table is an array only when its keys are exactly 1..n.
        EQUALS(runtimes.Js("Probe.kind([])"), "Array");
        EQUALS(runtimes.Js("Probe.kind(10n)"), "Other");
        EQUALS(runtimes.Js("Probe.kind(new Date(0))"), "Object");
        EQUALS(runtimes.Js("Probe.kind(Vector3)"), "Function");
        EQUALS(runtimes.Lua("Probe.kind({ [1] = 'a', [3] = 'c' })"), "Object");
        EQUALS(runtimes.Lua("Probe.kind({ [0] = 'a', 'b' })"), "Object");
        EQUALS(runtimes.Lua("Probe.kind({ [1.5] = 'a' })"), "Object");
        EQUALS(runtimes.Lua("Probe.kind(Vector3)"), "Object");
        EQUALS(runtimes.Lua("Probe.kind(setmetatable({ 1, 2 }, { __index = function() error('ran') end }))"), "Array");
    });

    IT("answers the kind shorthands", {
        Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.flags()", "n-------");
        mismatches += runtimes.Both("Probe.flags(false)", "-b------");
        mismatches += runtimes.Both("Probe.flags(2)", "--d-----");
        mismatches += runtimes.Both("Probe.flags('2')", "---s----");
        mismatches += runtimes.Both("Probe.flags(Probe.flags)", "----f---");
        mismatches += runtimes.Both("Probe.flags([2])", "Probe.flags({2})", "-----a--");
        mismatches += runtimes.Both("Probe.flags({})", "------o-");
        mismatches += runtimes.Both("Probe.flags(new Vector3(1, 2, 3))", "Probe.flags(Vector3.new(1, 2, 3))", "-------c");
        EQUALS(mismatches, "");
    });

    IT("tells a list from an object with keys", {
        Runtimes runtimes;
        std::string mismatches;
        // The empty table of Lua is an Object, and a list all the same.
        mismatches += runtimes.Both("Probe.isList([])", "Probe.isList({})", "true");
        mismatches += runtimes.Both("Probe.isList({})", "true");
        mismatches += runtimes.Both("Probe.isList(['a', 'b'])", "Probe.isList({ 'a', 'b' })", "true");
        mismatches += runtimes.Both("Probe.isList([[]])", "Probe.isList({ {} })", "true");
        mismatches += runtimes.Both("Probe.isList({ a: 1 })", "Probe.isList({ a = 1 })", "false");
        mismatches += runtimes.Both("Probe.isList({ 0: 'a', 1: 'b' })", "Probe.isList({ [0] = 'a', [1] = 'b' })", "false");
        mismatches += runtimes.Both("Probe.isList()", "false");
        mismatches += runtimes.Both("Probe.isList('text')", "false");
        mismatches += runtimes.Both("Probe.isList(3)", "false");
        mismatches += runtimes.Both("Probe.isList(Probe.isList)", "false");
        mismatches += runtimes.Both("Probe.isList(new Vector3(1, 2, 3))", "Probe.isList(Vector3.new(1, 2, 3))", "false");
        mismatches += runtimes.Both("Probe.length([]) + ':' + Probe.kind(Probe.at([], 0))", "Probe.length({}) .. ':' .. Probe.kind(Probe.at({}, 0))", "0:Nil");
        EQUALS(mismatches, "");

        // A hole: JavaScript still has an array, Lua a table that is no sequence any more.
        EQUALS(runtimes.Js("Probe.isList(['a', , 'c']) + Probe.kind(['a', , 'c'])"), "trueArray");
        EQUALS(runtimes.Lua("tostring(Probe.isList({ 'a', nil, 'c' })) .. Probe.kind({ 'a', nil, 'c' })"), "falseObject");
        EQUALS(runtimes.Lua("Probe.isList({ [1.5] = 'a' })"), "false");
        EQUALS(runtimes.Lua("Probe.isList(setmetatable({}, { __index = { 'a' } }))"), "true");
    });

    IT("reads a scalar strictly, without converting it", {
        Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.strict('text')", "[text]f0");
        mismatches += runtimes.Both("Probe.strict(true)", "[]t0");
        mismatches += runtimes.Both("Probe.strict(false)", "[]f0");
        mismatches += runtimes.Both("Probe.strict(12)", "[]fn");
        mismatches += runtimes.Both("Probe.strict('12')", "[12]f0");
        mismatches += runtimes.Both("Probe.strict()", "[]f0");
        mismatches += runtimes.Both("Probe.strict({})", "[]f0");

        mismatches += runtimes.Both("Probe.integer(7)", "7");
        mismatches += runtimes.Both("Probe.integer(-7.0)", "-7");
        mismatches += runtimes.Both("Probe.integer(7.5)", "none");
        mismatches += runtimes.Both("Probe.integer('7')", "none");
        mismatches += runtimes.Both("Probe.integer(1 / 0)", "none");
        mismatches += runtimes.Both("Probe.integer(0 / 0)", "none");
        mismatches += runtimes.Both("Probe.integer(2 ** 53)", "Probe.integer(2 ^ 53)", "9007199254740992");
        mismatches += runtimes.Both("Probe.integer(2 ** 63)", "Probe.integer(2 ^ 63)", "none");
        mismatches += runtimes.Both("Probe.integer(-(2 ** 63))", "Probe.integer(-(2 ^ 63))", "-9223372036854775808");
        mismatches += runtimes.Both("Probe.integer(true)", "none");
        mismatches += runtimes.Both("Probe.integer()", "none");
        EQUALS(mismatches, "");
    });

    IT("spells a value the way the language does", {
        Runtimes runtimes;
        reachedEnd = 0;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.display('text')", "text");
        mismatches += runtimes.Both("Probe.display(12)", "12");
        mismatches += runtimes.Both("Probe.display(true)", "true");
        mismatches += runtimes.Both("Probe.display(new Vector3(1, 2, 3))", "Probe.display(Vector3.new(1, 2, 3))", "Vector3(1, 2, 3)");
        mismatches += runtimes.Both("Probe.display({ toString() { return 'mine' } })", "Probe.display(setmetatable({}, { __tostring = function() return 'mine' end }))", "mine");
        // A value that fails to spell itself is spelled as nothing, and the function goes on.
        mismatches += runtimes.Both("Probe.display({ toString() { throw new Error('no') } })", "Probe.display(setmetatable({}, { __tostring = function() error('no') end }))", "");
        EQUALS(mismatches, "");
        EQUALS(reachedEnd, 12);

        EQUALS(runtimes.Js("Probe.display()"), "undefined");
        EQUALS(runtimes.Js("Probe.display(null)"), "null");
        EQUALS(runtimes.Lua("Probe.display()"), "nil");
        EQUALS(runtimes.Lua("Probe.display(1.0)"), "1.0");
    });

    IT("passes a value through unchanged", {
        Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.echo('text')", "text");
        mismatches += runtimes.Both("Probe.echo(1.5)", "1.5");
        mismatches += runtimes.Both("Probe.echo(false)", "false");
        mismatches += runtimes.Both("{ const o = {}; Probe.echo(o) === o }", "(function() local o = {} return rawequal(Probe.echo(o), o) end)()", "true");
        mismatches += runtimes.Both("{ const f = () => 1; Probe.echo(f) === f }", "(function() local f = function() end return rawequal(Probe.echo(f), f) end)()", "true");
        mismatches += runtimes.Both("{ const v = new Vector3(1, 2, 3); Probe.echo(v) === v }", "(function() local v = Vector3.new(1, 2, 3) return rawequal(Probe.echo(v), v) end)()", "true");
        mismatches += runtimes.Both("Probe.echo(null) === null", "Probe.echo(nil) == nil", "true");
        mismatches += runtimes.Both("Probe.last()", "last");
        mismatches += runtimes.Both("Probe.argc()", "0");
        mismatches += runtimes.Both("Probe.argc(1, 'a', {})", "3");
        mismatches += runtimes.Both("Probe.argc(undefined, null)", "Probe.argc(nil, nil)", "2");
        mismatches += runtimes.Both("Probe.sum(1, 2, 3.5)", "6.5");
        mismatches += runtimes.Both("Probe.sum() === 0", "Probe.sum() == 0", "true");
        mismatches += runtimes.Both("Probe.sum(1, 'x', 2) === 3", "Probe.sum(1, 'x', 2) == 3", "true");
        mismatches += runtimes.Both("Probe.twice(21)", "42");
        EQUALS(mismatches, "");

        // null is JavaScript's, and nil in Lua.
        EQUALS(runtimes.Js("Probe.null() === null"), "true");
        EQUALS(runtimes.Js("Probe.none() === undefined"), "true");
        EQUALS(runtimes.Lua("select('#', Probe.null()) .. type(Probe.null())"), "1nil");
        EQUALS(runtimes.Js("Probe.nullKind()"), "NilNil1");
        EQUALS(runtimes.Lua("Probe.nullKind()"), "NilNil0");

        // Returning nothing is undefined in JavaScript and no value in Lua, where returning nil is one value.
        EQUALS(runtimes.Js("Probe.nothing()"), "undefined");
        EQUALS(runtimes.Js("Probe.none()"), "undefined");
        EQUALS(runtimes.Lua("select('#', Probe.nothing())"), "0");
        EQUALS(runtimes.Lua("select('#', Probe.none())"), "1");
        EQUALS(runtimes.Lua("select('#', Probe.echo(nil))"), "1");
        EQUALS(runtimes.Lua("math.type(Probe.sum(1, 2))"), "float");
        EQUALS(runtimes.Lua("math.type(Probe.argc(1, 2))"), "integer");
    });

    IT("builds objects and arrays", {
        Runtimes runtimes;
        const std::string made = R"({"a":1,"list":[1,2,3],"nested":{"b":"x"}})";
        EQUALS(runtimes.Both("JSON.stringify(Probe.make())", "dump(Probe.make())", made), "");
        EQUALS(runtimes.Both("Array.isArray(Probe.make().list)", "#Probe.make().list == 3", "true"), "");
        EQUALS(runtimes.Both("Probe.kind(Probe.make().list) + Probe.kind(Probe.make())", "Probe.kind(Probe.make().list) .. Probe.kind(Probe.make())", "ArrayObject"), "");

        EQUALS(runtimes.Js("JSON.stringify(Probe.scalars())"), R"([true,1.5,-7,"a\u0000b"])");
        EQUALS(runtimes.Lua("(function() local s = Probe.scalars() return tostring(s[1]) .. ',' .. s[2] .. ',' .. math.type(s[3]) .. s[3] .. ',' .. #s[4] .. s[4]:byte(2) end)()"), "true,1.5,integer-7,30");
    });

    IT("reads and writes the keys of an object", {
        Runtimes runtimes;
        reachedEnd = 0;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.keys({ a: 2, b: 1, c: 3 }).join()", "table.concat(Probe.keys({ b = 1, a = 2, c = 3 }), ',')", "a,b,c");
        mismatches += runtimes.Both("Probe.keys({}).length", "#Probe.keys({})", "0");
        mismatches += runtimes.Both("Probe.keys('text').length", "#Probe.keys('text')", "0");
        mismatches += runtimes.Both("Probe.get({ a: 5 }, 'a')", "Probe.get({ a = 5 }, 'a')", "5");
        mismatches += runtimes.Both("Probe.get({ 10: 'j' }, '10')", "Probe.get({ [10] = 'j' }, '10')", "j");
        mismatches += runtimes.Both("Probe.kind(Probe.get({ a: 5 }, 'b'))", "Probe.kind(Probe.get({ a = 5 }, 'b'))", "Nil");
        mismatches += runtimes.Both("Probe.kind(Probe.get(5, 'a'))", "Nil");
        mismatches += runtimes.Both("Probe.kind(Probe.get())", "Nil");
        mismatches += runtimes.Both("{ const o = {}; Probe.set(o, 'k', 9) === o && o.k }", "(function() local o = {} return rawequal(Probe.set(o, 'k', 9), o) and o.k end)()", "9");
        mismatches += runtimes.Both("JSON.stringify(Probe.set({ a: 1 }, 'b', [true]))", "dump(Probe.set({ a = 1 }, 'b', { true }))", R"({"a":1,"b":[true]})");
        mismatches += runtimes.Both("Probe.set(5, 'k', 9)", "5");
        EQUALS(mismatches, "");
        EQUALS(reachedEnd, 10);
    });

    IT("lists keys in one order, integers before strings", {
        Runtimes runtimes;
        std::string mismatches;
        // JavaScript keeps the order strings were added in; Lua, which has none, sorts them.
        mismatches += runtimes.Both("Probe.keys({ x: 1, 10: 'j', y: 2, 2: 'b' }).join()", "table.concat(Probe.keys({ y = 2, [10] = 'j', x = 1, [2] = 'b' }), ',')", "2,10,x,y");
        mismatches += runtimes.Both("Probe.keys({ 3: 'c', 1: 'a' }).join()", "table.concat(Probe.keys({ [3] = 'c', [1] = 'a' }), ',')", "1,3");
        EQUALS(mismatches, "");
        EQUALS(runtimes.Lua("table.concat(Probe.keys({ zeta = 1, alpha = 2, Mid = 3, [true] = 4, [1.5] = 5 }), ',')"), "Mid,alpha,zeta");
        EQUALS(runtimes.Lua("table.concat(Probe.keys({ [5] = 'e', [-2] = 'm', [0] = 'z' }), ',')"), "-2,0,5");
    });

    IT("counts the keys of an array from zero", {
        Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.keys(['a', 'b', 'c']).join()", "table.concat(Probe.keys({ 'a', 'b', 'c' }), ',')", "0,1,2");
        mismatches += runtimes.Both("Probe.get(['a', 'b', 'c'], '0')", "Probe.get({ 'a', 'b', 'c' }, '0')", "a");
        mismatches += runtimes.Both("Probe.get(['a', 'b', 'c'], '2')", "Probe.get({ 'a', 'b', 'c' }, '2')", "c");
        mismatches += runtimes.Both("Probe.kind(Probe.get(['a', 'b', 'c'], '3'))", "Probe.kind(Probe.get({ 'a', 'b', 'c' }, '3'))", "Nil");
        mismatches += runtimes.Both("Probe.set(['a', 'b', 'c'], '0', 'z').join()", "table.concat(Probe.set({ 'a', 'b', 'c' }, '0', 'z'), ',')", "z,b,c");
        mismatches += runtimes.Both("Probe.set(['a', 'b', 'c'], '3', 'd').join()", "table.concat(Probe.set({ 'a', 'b', 'c' }, '3', 'd'), ',')", "a,b,c,d");
        // Every key Keys gives reads the element it names.
        mismatches +=
            runtimes.Both("{ const a = ['x', 'y']; Probe.keys(a).map((key) => Probe.get(a, key)).join() }", "(function() local a, read = { 'x', 'y' }, {} for i, key in ipairs(Probe.keys(a)) do read[i] = Probe.get(a, key) end return table.concat(read, ',') end)()", "x,y");
        EQUALS(mismatches, "");
    });

    IT("writes a Lua integer key under the text Keys gave it", {
        Runtimes runtimes;
        const std::string roundTrip =
            "(function(t) for _, key in ipairs(Probe.keys(t)) do Probe.set(t, key, 'k') end local count, kinds = 0, '' for key, value in pairs(t) do count = count + 1 kinds = kinds .. (math.type(key) or type(key)) .. value end return count .. kinds end)";
        EQUALS(runtimes.Lua(roundTrip + "({ [10] = 'j' })"), "1integerk");
        // A key that is the text itself stays one.
        EQUALS(runtimes.Lua(roundTrip + "({ ['10'] = 'j' })"), "1stringk");
        EQUALS(runtimes.Lua("(function() local t = Probe.set({}, '10', 'x') return math.type(next(t)) .. t[10] end)()"), "integerx");
        EQUALS(runtimes.Lua("(function() local t = Probe.set({ a = 1 }, '-3', 'x') return t[-3] end)()"), "x");
        // What only looks like an integer is a string key.
        EQUALS(runtimes.Lua("(function() local t = Probe.set({}, '010', 'x') return type(next(t)) .. t['010'] end)()"), "stringx");
        EQUALS(runtimes.Js("JSON.stringify(Probe.set({}, '10', 'x'))"), R"({"10":"x"})");
    });

    IT("finds no keys in a bound object or a function", {
        Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.kind(Probe.get(new Vector3(1, 2, 3), 'y'))", "Probe.kind(Probe.get(Vector3.new(1, 2, 3), 'y'))", "Nil");
        mismatches += runtimes.Both("Probe.kind(Probe.get(new Vector3(1, 2, 3), 'add'))", "Probe.kind(Probe.get(Vector3.new(1, 2, 3), 'add'))", "Nil");
        mismatches += runtimes.Both("Probe.keys(new Vector3(1, 2, 3)).length", "#Probe.keys(Vector3.new(1, 2, 3))", "0");
        mismatches += runtimes.Both("{ const v = new Vector3(1, 2, 3); Probe.set(v, 'y', 9); Probe.set(v, 'extra', 9); v.y + ':' + typeof v.extra }",
            "(function() local v = Vector3.new(1, 2, 3) Probe.set(v, 'y', 9) Probe.set(v, 'extra', 9) return math.tointeger(v.y) .. ':' .. (v.extra == nil and 'undefined' or 'set') end)()", "2:undefined");
        mismatches += runtimes.Both("Probe.kind(Probe.get(Probe.kind, 'name'))", "Nil");
        mismatches += runtimes.Both("Probe.keys(Probe.kind).length", "#Probe.keys(Probe.kind)", "0");
        mismatches += runtimes.Both("{ const f = function () {}; Probe.set(f, 'tag', 9); typeof f.tag }", "(function() local f = function() end Probe.set(f, 'tag', 9) return 'undefined' end)()", "undefined");
        mismatches += runtimes.Both("Probe.kind(Probe.at(new Vector3(1, 2, 3), 0)) + Probe.length(Probe.kind)", "Probe.kind(Probe.at(Vector3.new(1, 2, 3), 0)) .. Probe.length(Probe.kind)", "Nil0");
        EQUALS(mismatches, "");

        // What a script put on a function is not a key of it either.
        EQUALS(runtimes.Js("{ const f = function () {}; f.tag = 5; Probe.kind(Probe.get(f, 'tag')) + Probe.keys(f).length }"), "Nil0");
    });

    IT("reads a Lua table raw, and a JavaScript object as a script does", {
        Runtimes runtimes;
        reachedEnd = 0;

        // No metamethod runs inside the function: a proxy table shows what it holds itself.
        EQUALS(runtimes.Lua("Probe.kind(Probe.get(setmetatable({}, { __index = function() error('ran') end }), 'a'))"), "Nil");
        EQUALS(runtimes.Lua("(function() local seen = 0 local t = setmetatable({}, { __newindex = function() seen = seen + 1 end }) Probe.set(t, 'a', 1) Probe.append(t, 2) return seen .. ':' .. rawget(t, 'a') .. rawget(t, 1) end)()"), "0:12");
        EQUALS(runtimes.Lua("Probe.length(setmetatable({ 1, 2 }, { __len = function() return 9 end }))"), "2");
        EQUALS(runtimes.Lua("#Probe.keys(setmetatable({}, { __pairs = function() error('ran') end, __index = { a = 1 } }))"), "0");

        // A table behind a protected metatable, as a class or a namespace is, is not written to.
        EQUALS(runtimes.Lua("(function() Probe.set(Probe, 'kind', 1) Probe.set(Vector3, 'zero', 1) Probe.append(Probe, 1) return type(Probe.kind) .. type(Vector3.zero) .. tostring(rawget(Probe, 1)) end)()"), "functionfunctionnil");
        EQUALS(runtimes.Lua("(function() local t = setmetatable({}, { __metatable = 'locked' }) Probe.set(t, 'a', 1) return tostring(rawget(t, 'a')) end)()"), "nil");

        // What a getter or a proxy throws is thrown once the function has returned.
        EQUALS(runtimes.Js("Probe.get({ get a() { throw new RangeError('getter ran') } }, 'a')"), "<threw> RangeError: getter ran");
        EQUALS(runtimes.Js("Probe.get({ get a() { return 7 } }, 'a')"), "7");
        EQUALS(runtimes.Js("Probe.kind(Probe.set(Object.freeze({}), 'a', 1).a)"), "Nil");
        EQUALS(reachedEnd, 3);
    });

    IT("reads and grows an array from zero", {
        Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.length(['a', 'b', 'c'])", "Probe.length({ 'a', 'b', 'c' })", "3");
        mismatches += runtimes.Both("Probe.length({})", "0");
        mismatches += runtimes.Both("Probe.length('text')", "0");
        mismatches += runtimes.Both("Probe.length()", "0");
        mismatches += runtimes.Both("Probe.at(['a', 'b', 'c'], 0)", "Probe.at({ 'a', 'b', 'c' }, 0)", "a");
        mismatches += runtimes.Both("Probe.at(['a', 'b', 'c'], 2)", "Probe.at({ 'a', 'b', 'c' }, 2)", "c");
        mismatches += runtimes.Both("Probe.kind(Probe.at(['a'], 1))", "Probe.kind(Probe.at({ 'a' }, 1))", "Nil");
        mismatches += runtimes.Both("Probe.kind(Probe.at('text', 0))", "Nil");
        mismatches += runtimes.Both("JSON.stringify(Probe.append(Probe.append([], 'a'), 'b'))", "dump(Probe.append(Probe.append({}, 'a'), 'b'))", R"(["a","b"])");
        mismatches += runtimes.Both("Probe.append(5, 'a')", "5");
        EQUALS(mismatches, "");
    });

    // --- Functions ---

    IT("returns a function that calls a native one", {
        Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.adder(2)(3)", "5");
        mismatches += runtimes.Both("{ const add = Probe.adder(10); add(1) + add(2) }", "(function() local add = Probe.adder(10) return add(1) + add(2) end)()", "23");
        mismatches += runtimes.Both("Probe.adder(1) === Probe.adder(1)", "rawequal(Probe.adder(1), Probe.adder(1))", "false");
        mismatches += runtimes.Both("typeof Probe.adder(1)", "type(Probe.adder(1))", "function");
        EQUALS(mismatches, "");
    });

    IT("reads a handle kept beyond its call as nil", {
        Runtimes runtimes;
        EQUALS(runtimes.Js("typeof Probe.capture('old')()"), "undefined");
        EQUALS(runtimes.Lua("type(Probe.capture('old')())"), "nil");
        // The call it is read in has another value in the same place.
        EQUALS(runtimes.Js("typeof Probe.capture('old')('new', 'newer')"), "undefined");
        EQUALS(runtimes.Lua("type(Probe.capture('old')('new', 'newer'))"), "nil");
        EQUALS(runtimes.Lua("select('#', Probe.capture('old')())"), "1");

        // It reads as the nil of the call it is read in, and is stored as one.
        EQUALS(runtimes.Js("Probe.stale('old')()"), "Nil[undefined]00NilNil:NilNil");
        EQUALS(runtimes.Js("Probe.stale(['a', 'b'])('new', ['c'])"), "Nil[undefined]00NilNil:NilNil");
        EQUALS(runtimes.Js("Probe.stale({ a: 1 })({ a: 2 })"), "Nil[undefined]00NilNil:NilNil");
        EQUALS(runtimes.Lua("Probe.stale('old')()"), "Nil[nil]00NilNil:NilNil");
        EQUALS(runtimes.Lua("Probe.stale({ 'a', 'b' })('new', { 'c' })"), "Nil[nil]00NilNil:NilNil");
        EQUALS(runtimes.Lua("Probe.stale({ a = 1 })({ a = 2 })"), "Nil[nil]00NilNil:NilNil");
    });

    IT("reads a handle only in the call it belongs to", {
        Runtimes runtimes;
        // The call nested in it sees nil; back in its own call the handle is good again.
        EQUALS(runtimes.Js("Probe.nested({ toString() { return this.peek() } }, 'text')"), "Nil:String");
        EQUALS(runtimes.Lua("Probe.nested(setmetatable({}, { __tostring = function(self) return self.peek() end }), 'text')"), "Nil:String");
    });

    IT("keeps a native function for as long as the script function lives", {
        HeldSlot()                     = std::make_shared<int>(7);
        const std::weak_ptr<int> alive = HeldSlot();
        {
            Runtimes runtimes;
            EQUALS(runtimes.Both("globalThis.kept = Probe.hold(); kept()", "(function() kept = Probe.hold() return kept() end)()", "7"), "");
            HeldSlot().reset();
            EQUALS(alive.expired(), false);
            EQUALS(runtimes.Both("kept()", "7"), "");

            // Lua frees it when the function is collected; JavaScript at the latest with its runtime.
            LuaRuntimeTest::Run(runtimes.LuaSide(), "kept = nil");
            lua_gc(runtimes.LuaSide().State(), LUA_GCCOLLECT);
            EQUALS(alive.expired(), false);
        }
        EQUALS(alive.expired(), true);

        HeldSlot()                   = std::make_shared<int>(8);
        const std::weak_ptr<int> lua = HeldSlot();
        {
            LuaRuntime runtime(LuaOptions(Side::Server));
            ProbeDefinition().RegisterLuaGlobal(runtime.State(), Side::Server);
            EQUALS(LuaRuntimeTest::Eval(runtime, "(function() kept = Probe.hold() return kept() end)()"), "8");
            HeldSlot().reset();
            EQUALS(lua.expired(), false);
            LuaRuntimeTest::Run(runtime, "kept = nil");
            lua_gc(runtime.State(), LUA_GCCOLLECT);
            EQUALS(lua.expired(), true);
        }
    });

    IT("makes a function of a native one outside any call", {
        Runtimes runtimes;
        runtimes.Enter([](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            const v8::Local<v8::Function> function = Bind::MakeV8Function(isolate, context, [](Call &call) {
                call.Return(call.Arg(0).AsString() + "!");
            });
            context->Global()->Set(context, v8pp::to_v8(isolate, "shout"), function).Check();
        });
        lua_State *L = runtimes.LuaSide().State();
        Bind::PushLuaFunction(L, [](Call &call) {
            call.Return(call.Arg(0).AsString() + "!");
        });
        lua_setglobal(L, "shout");
        EQUALS(runtimes.Both("shout('hey')", "hey!"), "");

        // A state no LuaRuntime made runs one too, as long as the function asks for nothing of the runtime.
        LuaTest::State plain;
        Bind::PushLuaFunction(plain.L, [](Call &call) {
            call.Return(call.ArgCount());
        });
        lua_setglobal(plain.L, "count");
        EQUALS(LuaTest::Eval(plain.L, "count(1, 2, 3)"), "3");
    });

    // --- Errors ---

    IT("throws when the function returns, and the first throw wins", {
        Runtimes runtimes;
        reachedEnd = 0;

        EQUALS(runtimes.Js("Probe.failType()"), "<threw> TypeError: Probe.failType: expected (text)");
        EQUALS(runtimes.Js("Probe.fail()"), "<threw> Error: Probe.fail: network peer unavailable");
        EQUALS(runtimes.Js("{ let caught; try { Probe.failType() } catch (error) { caught = error } caught instanceof TypeError }"), "true");
        EQUALS(runtimes.Js("{ let caught; try { Probe.fail() } catch (error) { caught = error } caught instanceof Error && !(caught instanceof TypeError) }"), "true");
        EQUALS(reachedEnd, 4);

        EQUALS(runtimes.Lua("Probe.failType()"), "<error> eval:1: Probe.failType: expected (text)");
        EQUALS(runtimes.Lua("Probe.fail()"), "<error> eval:1: Probe.fail: network peer unavailable");
        EQUALS(runtimes.Lua("(function() local ok, message = pcall(function() Probe.fail() end) return tostring(ok) .. ' ' .. type(message) .. ' ' .. message end)()"), "false string eval:1: Probe.fail: network peer unavailable");
        EQUALS(runtimes.Lua("select(2, pcall(Probe.failType))"), "Probe.failType: expected (text)");
        EQUALS(reachedEnd, 8);

        // The runtimes are as good as before.
        EQUALS(runtimes.Both("Probe.twice(4)", "8"), "");
    });

    IT("turns a C++ exception into an error of the script", {
        Runtimes runtimes;
        EQUALS(runtimes.Js("Probe.raise()"), "<threw> Error: Probe.raise: exception");
        EQUALS(runtimes.Lua("Probe.raise()"), "<error> eval:1: Probe.raise: exception");
        EQUALS(runtimes.Both("Probe.twice(4)", "8"), "");

        // A typed function keeps the checks of its language.
        EQUALS(runtimes.Lua("Probe.twice('a')"), "<error> eval:1: bad argument #1 to 'Probe.twice' (integer expected, got string)");
        EQUALS(runtimes.Lua("Probe.twice()"), "<error> eval:1: wrong number of arguments to 'Probe.twice' (1..1 expected, got 0)");
        EQUALS(runtimes.Js("Probe.twice()").starts_with("<threw> Error: Argument count does not match function definition"), true);
    });

    IT("lets what Lua raises inside the function unwind it", {
        LuaRuntime runtime(LuaRuntimeTest::Options(4u << 20));
        Bind::PushLuaFunction(runtime.State(), [](Call &call) {
            const Unwinding unwinding;
            const std::string block(64 * 1024, 'x');
            Value list = call.NewArray();
            for (int i = 0; i < 1024; ++i) {
                list.Append(call.String(block));
            }
            ++reachedEnd;
        });
        lua_setglobal(runtime.State(), "flood");
        unwound    = 0;
        reachedEnd = 0;

        EQUALS(LuaRuntimeTest::Eval(runtime, "flood()"), "<error> not enough memory");
        EQUALS(unwound, 1);
        EQUALS(reachedEnd, 0);
        EQUALS(LuaRuntimeTest::Eval(runtime, "1 + 1"), "2");
    });

    IT("goes on when the deadline stops what it asked a script for, and the script is stopped after it", {
        LuaRuntime runtime(LuaRuntimeTest::Options(0, 50));
        LuaRuntimeTest::AddSpin(runtime);
        ProbeDefinition().RegisterLuaGlobal(runtime.State(), Side::Server);
        reachedEnd = 0;

        const std::string error = LuaRuntimeTest::Run(runtime, "Probe.display(setmetatable({}, { __tostring = function() spin() return 'late' end })) survived = true");
        EQUALS(LuaRuntimeTest::Contains(LuaRuntimeTest::FirstLine(error), "script ran too long"), true);
        EQUALS(reachedEnd, 1);
        EQUALS(LuaRuntimeTest::Eval(runtime, "survived"), "nil");
    });

    IT("lets nothing more run in an isolate that is being terminated", {
        Runtimes runtimes;
        runtimes.Enter([](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            const v8::Local<v8::Function> stop = Bind::MakeV8Function(isolate, context, [isolate](Call &) {
                isolate->TerminateExecution();
            });
            context->Global()->Set(context, v8pp::to_v8(isolate, "stop"), stop).Check();
        });
        reachedEnd = 0;

        // What a script is terminated with is no exception of its own: V8 has it as null.
        EQUALS(runtimes.Js("Probe.get({ get a() { stop(); for (;;) {} } }, 'a'); globalThis.survived = true"), "<threw> null");
        EQUALS(reachedEnd, 1);
        runtimes.Enter([](v8::Isolate *isolate, v8::Local<v8::Context>) {
            isolate->CancelTerminateExecution();
        });
        EQUALS(runtimes.Js("typeof survived"), "undefined");
        EQUALS(runtimes.Both("Probe.twice(4)", "8"), "");

        // Terminated while it spells a value, the function is told as well, and asks the script for nothing more.
        reachedEnd         = 0;
        thrownAfterDisplay = false;
        EQUALS(runtimes.Js("Probe.spell({ toString() { stop(); for (;;) {} } }, { get a() { globalThis.asked = true; return 1 } })"), "<threw> null");
        EQUALS(reachedEnd, 1);
        EQUALS(thrownAfterDisplay, true);
        runtimes.Enter([](v8::Isolate *isolate, v8::Local<v8::Context>) {
            isolate->CancelTerminateExecution();
        });
        EQUALS(runtimes.Js("typeof asked"), "undefined");
        EQUALS(runtimes.Both("Probe.twice(4)", "8"), "");
    });

    // --- Bound classes ---

    IT("gives a method written as a context function its receiver", {
        Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("{ const c = new CallCounter(1); c.bump(); c.bump(5); c.get() }", "(function() local c = CallCounter.new(1) c:bump() c:bump(5) return c:get() end)()", "7");
        // The receiver is not an argument.
        mismatches += runtimes.Both("new CallCounter(1).bump()", "CallCounter.new(1):bump()", "0");
        mismatches += runtimes.Both("new CallCounter(1).bump(2, 'x', null)", "CallCounter.new(1):bump(2, 'x', nil)", "3");
        // A class derived from the receiver's passes for it.
        mismatches += runtimes.Both("{ const s = new CallStepper(3); s.bump(4); s.get() }", "(function() local s = CallStepper.new(3) s:bump(4) return s:get() end)()", "7");
        mismatches += runtimes.Both("CallCounter.receiver()", "false");
        // The receiver as a value, for a method that returns it.
        mismatches += runtimes.Both("{ const c = new CallCounter(1); c.chain() === c }", "(function() local c = CallCounter.new(1) return rawequal(c:chain(), c) end)()", "true");
        mismatches += runtimes.Both("new CallCounter(1).chain().chain().get()", "CallCounter.new(1):chain():chain():get()", "3");
        mismatches += runtimes.Both("{ const s = new CallStepper(1); s.chain() === s }", "(function() local s = CallStepper.new(1) return rawequal(s:chain(), s) end)()", "true");
        mismatches += runtimes.Both("Probe.thisKind()", "Nil");
        mismatches += runtimes.Both("Probe.thisKind.call({})", "Probe.thisKind({})", "Nil");
        mismatches += runtimes.Both("Probe.adder.call({}, 1)(1)", "Probe.adder(1)(1)", "2");
        mismatches += runtimes.Both("{ const c = new CallCounter(1); c.serverBump(); c.get() }", "(function() local c = CallCounter.new(1) c:serverBump() return c:get() end)()", "101");
        EQUALS(mismatches, "");

        // Without a receiver of the class the function finds none, and here returns without effect.
        EQUALS(runtimes.Js("new CallCounter(1).bump.call({}, 5)"), "undefined");
        EQUALS(runtimes.Js("{ const bump = new CallCounter(1).bump; bump(5) }"), "undefined");
        EQUALS(runtimes.Js("new CallCounter(1).bump.call(new Vector3(1, 2, 3), 5)"), "undefined");
        EQUALS(runtimes.Lua("select('#', CallCounter.new(1).bump(5))"), "0");
        EQUALS(runtimes.Lua("select('#', CallCounter.new(1).bump())"), "0");
        EQUALS(runtimes.Lua("select('#', CallCounter.new(1).bump(Vector3.new(1, 2, 3), 5))"), "0");

        // Nor does it take what it made itself for one, where a Lua script passed nothing at all: the first
        // value it makes lands where the receiver would have been.
        EQUALS(runtimes.Both("{ const c = new CallCounter(1); c.late() + ' ' + c.get() }", "(function() local c = CallCounter.new(1) return c:late() .. ' ' .. c:get() end)()", "receiver:Bound:Bound 2"), "");
        EQUALS(runtimes.Lua("CallCounter.new(1).late()"), "none:Nil:Bound");
        EQUALS(runtimes.Lua("(function() local c = CallCounter.new(1) return c.late() .. ' ' .. c:get() end)()"), "none:Nil:Bound 1");
    });

    IT("leaves a method of another side out", {
        Runtimes runtimes(Side::Client);
        EQUALS(runtimes.Js("typeof new CallCounter(1).serverBump"), "undefined");
        EQUALS(runtimes.Lua("type(CallCounter.new(1).serverBump)"), "nil");
        EQUALS(runtimes.Both("new CallCounter(1).bump(1)", "CallCounter.new(1):bump(1)", "1"), "");
    });

    IT("finds the object in a bound value, through its bases", {
        Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.bound(new CallCounter(5))", "Probe.bound(CallCounter.new(5))", "5");
        mismatches += runtimes.Both("Probe.bound(new CallStepper(6))", "Probe.bound(CallStepper.new(6))", "6");
        mismatches += runtimes.Both("Probe.bound(new Vector3(1, 2, 3))", "Probe.bound(Vector3.new(1, 2, 3))", "-1");
        mismatches += runtimes.Both("Probe.bound({})", "-1");
        mismatches += runtimes.Both("Probe.bound(5)", "-1");
        mismatches += runtimes.Both("Probe.bound()", "-1");
        EQUALS(mismatches, "");
    });

    IT("wraps a C++ object as an instance of its class", {
        Runtimes runtimes;
        reachedEnd = 0;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.wrap(5).get()", "Probe.wrap(5):get()", "5");
        mismatches += runtimes.Both("Probe.wrap(5) instanceof CallCounter", "Probe.kind(Probe.wrap(5)) == 'Bound'", "true");
        mismatches += runtimes.Both("{ const w = Probe.wrap(5); w.bump(2); w.get() }", "(function() local w = Probe.wrap(5) w:bump(2) return w:get() end)()", "7");
        mismatches += runtimes.Both("{ const a = new CallCounter(1); const b = Probe.copy(a); b.bump(1); a.get() * 10 + b.get() }", "(function() local a = CallCounter.new(1) local b = Probe.copy(a) b:bump(1) return a:get() * 10 + b:get() end)()", "12");
        EQUALS(mismatches, "");
        EQUALS(reachedEnd, 6);
    });

    IT("throws for a class the runtime does not have", {
        Runtimes runtimes(Side::Server, false);
        reachedEnd = 0;
        EQUALS(runtimes.Js("Probe.wrap(5)"), "<threw> Error: CallCounter is not available in this JavaScript runtime");
        EQUALS(runtimes.Lua("Probe.wrap(5)"), "<error> eval:1: CallCounter is not available in this Lua state");
        EQUALS(reachedEnd, 2);
        // The object it was to move from is left whole.
        leftAfterWrap = 0;
        EQUALS(runtimes.Js("Probe.keep()"), "<threw> Error: CallStepper is not available in this JavaScript runtime");
        EQUALS(leftAfterWrap, 64u);
        leftAfterWrap = 0;
        EQUALS(runtimes.Lua("Probe.keep()"), "<error> eval:1: CallStepper is not available in this Lua state");
        EQUALS(leftAfterWrap, 64u);
        EQUALS(runtimes.Both("Probe.bound({})", "-1"), "");
    });

    // --- The call ---

    IT("tells the language, the side and the resource of the caller", {
        {
            Runtimes runtimes;
            EQUALS(runtimes.Js("Probe.language()"), "javascript");
            EQUALS(runtimes.Lua("Probe.language()"), "lua");
            EQUALS(runtimes.Both("Probe.side()", "server"), "");

            // Lua has the name its runtime was created with; JavaScript asks the resource manager.
            EQUALS(runtimes.Lua("Probe.resource()"), "lua-probe");
            EQUALS(runtimes.Both("Probe.manager()", UnregisteredManager()), "");

            ResourceManager manager(&runtimes.Engine());
            expectedManager = &manager;
            Bind::SetCallResourceManager(runtimes.Isolate(), &manager);
            Bind::SetCallResourceManager(&runtimes.LuaSide(), &manager);
            EQUALS(runtimes.Both("Probe.manager()", "registered"), "");
            EQUALS(runtimes.Js("Probe.resource()"), "");
            manager.SetCurrentResourceContext("js-probe");
            EQUALS(runtimes.Js("Probe.resource()"), "js-probe");
            manager.SetCurrentResourceContext("");
            EQUALS(runtimes.Lua("Probe.resource()"), "lua-probe");

            Bind::SetCallResourceManager(runtimes.Isolate(), nullptr);
            Bind::SetCallResourceManager(&runtimes.LuaSide(), nullptr);
            EQUALS(runtimes.Both("Probe.manager()", UnregisteredManager()), "");
        }
        expectedManager = nullptr;

        Runtimes client(Side::Client);
        EQUALS(client.Both("Probe.side()", "client"), "");
    });

    // --- Namespace, materialised ---

    IT("publishes constants, and the members of the side only", {
        Runtimes server;
        std::string mismatches;
        mismatches += server.Both("Probe.answer", "42");
        mismatches += server.Both("Probe.ratio", "0.5");
        mismatches += server.Both("Probe.label", "probe");
        mismatches += server.Both("Probe.serverLabel", "server");
        mismatches += server.Both("Probe.serverOnly()", "true");
        mismatches += server.Both("Probe.enabled === true", "Probe.enabled == true", "true");
        mismatches += server.Both("typeof Probe.enabled", "type(Probe.enabled)", "boolean");
        mismatches += server.Both("Probe.count", "3");
        mismatches += server.Both("Probe.word", "literal");
        EQUALS(mismatches, "");
        EQUALS(server.Js("typeof Probe.clientOnly"), "undefined");
        EQUALS(server.Lua("type(Probe.clientOnly)"), "nil");
        EQUALS(server.Lua("math.type(Probe.answer) .. math.type(Probe.ratio)"), "integerfloat");

        Runtimes client(Side::Client);
        EQUALS(client.Both("Probe.clientOnly()", "true"), "");
        EQUALS(client.Js("typeof Probe.serverOnly + typeof Probe.serverLabel"), "undefinedundefined");
        EQUALS(client.Lua("type(Probe.serverOnly) .. type(Probe.serverLabel)"), "nilnil");
    });

    IT("makes the namespace read-only", {
        Runtimes runtimes;
        EQUALS(runtimes.Lua("(function() Probe.answer = 1 end)()"), "<error> eval:1: attempt to set 'answer' on Probe");
        EQUALS(runtimes.Lua("(function() Probe.extra = 1 end)()"), "<error> eval:1: attempt to set 'extra' on Probe");
        EQUALS(runtimes.Lua("getmetatable(Probe)"), "false");
        EQUALS(runtimes.Lua("(function() return (pcall(setmetatable, Probe, {})) end)()"), "false");
        EQUALS(runtimes.Lua("Probe.answer"), "42");
        // Empty itself, so that every read goes through the table it reads from.
        EQUALS(runtimes.Lua("next(Probe)"), "nil");
        EQUALS(runtimes.Lua("type(Probe.missing)"), "nil");

        // A constant is one in JavaScript too.
        EQUALS(runtimes.Js("Probe.answer = 1; Probe.answer"), "42");
        EQUALS(runtimes.Js("'use strict'; Probe.answer = 1").starts_with("<threw> TypeError: Cannot assign to read only property 'answer'"), true);
    });

    IT("puts the namespace back under its name in a state that has it", {
        LuaRuntime runtime(LuaOptions(Side::Server));
        lua_State *L = runtime.State();
        ProbeDefinition().RegisterLuaGlobal(L, Side::Server);
        LuaRuntimeTest::Run(runtime, "first = Probe; rawset(_G, 'Probe', nil)");
        EQUALS(LuaRuntimeTest::Eval(runtime, "type(Probe)"), "nil");
        ProbeDefinition().MaterialiseLua(L, Side::Client);
        EQUALS(LuaRuntimeTest::Eval(runtime, "rawequal(first, Probe)"), "true");
        EQUALS(LuaRuntimeTest::Eval(runtime, "type(Probe.serverOnly) .. type(Probe.clientOnly)"), "functionnil");
        EQUALS(lua_gettop(L), 0);
    });

    IT("takes the value of a runtime constant from each materialisation", {
        Bind::Namespace told("ProbeTold", "Says what its runtime was told.");
        told.RuntimeConstant<bool>("flag", "A switch of the runtime.")
            .RuntimeConstant<double>("level", "A level of the runtime.")
            .Constant("fixed", 7, "The same in every runtime.")
            .RuntimeConstant<std::string>("label", "A label of the runtime.")
            .RuntimeConstant<std::string>("serverLabel", "A label of a server.", Side::Server);

        // A client is given no value for what only a server has.
        const Bind::Namespace::RuntimeConstants first = {{"flag", true}, {"level", 3.0}, {"label", std::string("first")}};
        Runtimes client(Side::Client);
        v8pp::metadata::registry recorded;
        client.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            told.MaterialiseV8(isolate, context, context->Global(), recorded, Side::Client, first);
        });
        told.RegisterLuaGlobal(client.LuaSide().State(), Side::Client, first);
        std::string mismatches;
        mismatches += client.Both("ProbeTold.flag === true", "ProbeTold.flag == true", "true");
        mismatches += client.Both("ProbeTold.level", "3");
        mismatches += client.Both("ProbeTold.fixed", "7");
        mismatches += client.Both("ProbeTold.label", "first");
        EQUALS(mismatches, "");
        EQUALS(client.Js("typeof ProbeTold.serverLabel"), "undefined");
        EQUALS(client.Lua("type(ProbeTold.serverLabel)"), "nil");
        EQUALS(client.Lua("math.type(ProbeTold.level)"), "integer");

        // To a script it is a constant like any other.
        EQUALS(client.Js("ProbeTold.label = 'x'; ProbeTold.label"), "first");
        EQUALS(client.Js("Reflect.deleteProperty(ProbeTold, 'label')"), "false");
        EQUALS(client.Lua("(function() ProbeTold.label = 'x' end)()"), "<error> eval:1: attempt to set 'label' on ProbeTold");
        const nlohmann::json expected = {{"description", "A label of the runtime."}, {"name", "label"}, {"readonly", true}, {"static", false}, {"type", {{"cppName", ""}, {"name", "string"}, {"optional", false}}}};
        nlohmann::json label;
        for (const nlohmann::json &property : SymbolOf(recorded, "ProbeTold")["properties"]) {
            if (property["name"] == "label") {
                label = property;
            }
        }
        EQUALS(label.dump(), expected.dump());

        // Another runtime is told something else.
        const Bind::Namespace::RuntimeConstants second = {{"serverLabel", std::string("behind")}, {"label", std::string("second")}, {"level", 0.5}, {"flag", false}};
        Runtimes server;
        server.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            told.MaterialiseV8(isolate, context, context->Global(), recorded, Side::Server, second);
        });
        told.RegisterLuaGlobal(server.LuaSide().State(), Side::Server, second);
        mismatches += server.Both("ProbeTold.flag === false", "ProbeTold.flag == false", "true");
        mismatches += server.Both("ProbeTold.level", "0.5");
        mismatches += server.Both("ProbeTold.label", "second");
        mismatches += server.Both("ProbeTold.serverLabel", "behind");
        EQUALS(mismatches, "");
        EQUALS(client.Both("ProbeTold.label", "first"), "");

        // A state that has the namespace keeps what it was first told.
        told.MaterialiseLua(client.LuaSide().State(), Side::Client, second);
        EQUALS(client.Lua("ProbeTold.label"), "first");
    });

    IT("refuses a runtime constant that is given no value, or one of another type", {
        Bind::Namespace told("ProbeUntold", "Says what its runtime was told.");
        told.RuntimeConstant<bool>("flag", "A switch of the runtime.").RuntimeConstant<std::string>("label", "A label of the runtime.").RuntimeConstant<double>("serverLevel", "A level of a server.", Side::Server);
        const std::string refusedTwice = LogicErrorOf([&] {
            told.RuntimeConstant<bool>("label", "Declared before.");
        });
        EQUALS(refusedTwice, "Bind: 'ProbeUntold.label' is declared twice, and a member cannot be overloaded");

        const Bind::Namespace::RuntimeConstants missing = {{"flag", true}};
        const Bind::Namespace::RuntimeConstants mistyped = {{"flag", true}, {"label", 1.0}};
        const Bind::Namespace::RuntimeConstants whole    = {{"flag", true}, {"label", std::string("whole")}};
        const std::string noValue                        = "Bind: 'ProbeUntold.label' is a runtime constant, and the runtime gives it no value";
        const std::string otherType                      = "Bind: 'ProbeUntold.label' is a runtime constant, and the runtime gives it a value of another type";

        Runtimes runtimes(Side::Client);
        std::string refusedMissing;
        std::string refusedMistyped;
        std::string refusedOtherSide;
        runtimes.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            v8pp::metadata::registry &scratch = v8pp::metadata::catalog("bind_call/scratch");
            refusedMissing                    = LogicErrorOf([&] {
                told.MaterialiseV8(isolate, context, context->Global(), scratch, Side::Client, missing);
            });
            refusedMistyped = LogicErrorOf([&] {
                told.MaterialiseV8(isolate, context, context->Global(), scratch, Side::Client, mistyped);
            });
            // What only the other side has needs no value here, and needs one there.
            refusedOtherSide = LogicErrorOf([&] {
                told.MaterialiseV8(isolate, context, context->Global(), scratch, Side::Server, whole);
            });
        });
        EQUALS(refusedMissing, noValue);
        EQUALS(refusedMistyped, otherType);
        EQUALS(refusedOtherSide, "Bind: 'ProbeUntold.serverLevel' is a runtime constant, and the runtime gives it no value");
        EQUALS(runtimes.Js("typeof ProbeUntold"), "undefined");

        lua_State *L = runtimes.LuaSide().State();
        refusedMissing = LogicErrorOf([&] {
            told.MaterialiseLua(L, Side::Client, missing);
        });
        refusedMistyped = LogicErrorOf([&] {
            told.MaterialiseLua(L, Side::Client, mistyped);
        });
        EQUALS(refusedMissing, noValue);
        EQUALS(refusedMistyped, otherType);
        EQUALS(lua_gettop(L), 0);
        EQUALS(runtimes.Lua("type(ProbeUntold)"), "nil");

        told.MaterialiseLua(L, Side::Client, whole);
        EQUALS(runtimes.Lua("ProbeUntold.label"), "whole");
    });

    IT("refuses to give JavaScript a function it cannot receive the result of", {
        using BindClassLuaTest::Token;
        Bind::Namespace sealing("ProbeSealing", "Returns what cannot be copied.");
        sealing.Function(
            "shared",
            +[]() -> Token & {
                static Token token(1);
                return token;
            },
            Doc("Returns the one token."), Side::Server);

        Runtimes runtimes(Side::Client);
        std::string refusal;
        runtimes.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            // Not on the side it is declared for, so not refused.
            sealing.MaterialiseV8(isolate, context, context->Global(), v8pp::metadata::catalog("bind_call/scratch"), Side::Client);
            refusal = LogicErrorOf([&] {
                sealing.MaterialiseV8(isolate, context, context->Global(), v8pp::metadata::catalog("bind_call/scratch"), Side::Server);
            });
        });
        EQUALS(refusal, "Bind: 'ProbeSealing.shared' returns an object of a class that cannot be copied, which JavaScript cannot receive");
        EQUALS(runtimes.Js("typeof ProbeSealing.shared"), "undefined");
    });

    IT("returns the object it sets on the target", {
        Runtimes runtimes;
        bool same = false;
        runtimes.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            v8::Local<v8::Object> target = v8::Object::New(isolate);
            v8::Local<v8::Object> made   = ProbeDefinition().MaterialiseV8(isolate, context, target, v8pp::metadata::catalog("bind_call/scratch"), Side::Server);
            v8::Local<v8::Value> set;
            same = target->Get(context, v8pp::to_v8(isolate, "Probe")).ToLocal(&set) && set->StrictEquals(made);
        });
        EQUALS(same, true);
    });

    IT("records the namespace in the catalog as a hand-written builtin does", {
        using namespace v8pp::metadata;
        Bind::Namespace written("ProbeWritten", "Server-side delivery of structured chat messages to connected clients.");
        written
            .Function(
                "sendToAll", [](Call &) {},
                Doc("Broadcasts a structured chat message to all clients.")
                    .Param("text", TypeOf<std::string>(), "Message body broadcast to every connected client.")
                    .OptionalParam("options", Type::Custom("{ author?: string; color?: number | Color }", "table"), "Optional author label and color: a Color or a packed 0xRRGGBBAA integer."))
            .Function(
                "log", [](Call &) {}, Doc("Writes an informational log entry prefixed with the current resource name.").RestParam("values", Type::Custom("unknown[]", "any"), "Values formatted and joined with spaces."))
            .Function(
                "emit", [](Call &) {},
                Doc("Invokes every shared handler and waits for all synchronous and asynchronous results.")
                    .Param("eventName", TypeOf<std::string>(), "Shared event name.")
                    .RestParam("args", Type::Custom("unknown[]", "any"), "Arguments delivered to every matching handler.")
                    .ReturnType(Type::Custom("Promise<void>", "nil"))
                    .Returns("Promise rejected with an AggregateError when one or more handlers fail."))
            .Function(
                "count",
                [](const std::string &eventName) {
                    return static_cast<int>(eventName.size());
                },
                Doc("Counts persistent and one-shot shared handlers across resources.").Param("eventName", "Shared event name to inspect.").Returns("Number of matching handlers."))
            .Function(
                "serverOnly", [](Call &) {}, Doc("Exists on the server."), Side::Server)
            .Constant("isClient", true, "True in the sandboxed client scripting runtime.")
            .Constant("frameworkVersion", std::string("1.0"), "Local Framework release version.");

        registry byHand;
        symbol &expected = byHand.global_object("ProbeWritten", "Server-side delivery of structured chat messages to connected clients.");
        expected.record(function_of<v8::FunctionCallback>("sendToAll", docs("void",
                                                                           {
                                                                               param("text", "string", false, "Message body broadcast to every connected client."),
                                                                               param("options", "{ author?: string; color?: number | Color }", true, "Optional author label and color: a Color or a packed 0xRRGGBBAA integer."),
                                                                           },
                                                                           "Broadcasts a structured chat message to all clients.")));
        expected.record(function_of<v8::FunctionCallback>("log", docs("void", {rest_param("values", "unknown[]", "Values formatted and joined with spaces.")}, "Writes an informational log entry prefixed with the current resource name.")));
        expected.record(function_of<v8::FunctionCallback>("emit", docs("Promise<void>", {param("eventName", "string", false, "Shared event name."), rest_param("args", "unknown[]", "Arguments delivered to every matching handler.")},
                                                                      "Invokes every shared handler and waits for all synchronous and asynchronous results.", "Promise rejected with an AggregateError when one or more handlers fail.")));
        expected.record(function_of<v8::FunctionCallback>("count", docs("number", {param("eventName", "string", false, "Shared event name to inspect.")}, "Counts persistent and one-shot shared handlers across resources.", "Number of matching handlers.")));
        expected.add_property("isClient", "boolean", "True in the sandboxed client scripting runtime.", true);
        expected.add_property("frameworkVersion", "string", "Local Framework release version.", true);

        Runtimes runtimes(Side::Client);
        registry recorded;
        runtimes.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            written.MaterialiseV8(isolate, context, context->Global(), recorded, Side::Client);
        });
        EQUALS(SymbolOf(recorded, "ProbeWritten").dump(), SymbolOf(byHand, "ProbeWritten").dump());
        EQUALS(runtimes.Js("typeof ProbeWritten.sendToAll + typeof ProbeWritten.count + typeof ProbeWritten.serverOnly"), "functionfunctionundefined");
        EQUALS(runtimes.Js("ProbeWritten.count('four')"), "4");
        EQUALS(runtimes.Js("ProbeWritten.log.name"), "");
    });

    IT("records the functions and the parameters of Probe as a global object", {
        Runtimes runtimes;
        const nlohmann::json probe = SymbolOf(runtimes.Catalog(), "Probe");
        EQUALS(probe["kind"], "globalObject");
        EQUALS(probe["description"], "Exercises the dynamic interface.");

        const auto named = [](const nlohmann::json &list, const std::string &name) -> nlohmann::json {
            for (const nlohmann::json &element : list) {
                if (element["name"] == name) {
                    return element;
                }
            }
            return nullptr;
        };
        const nlohmann::json get = named(probe["functions"], "get");
        EQUALS(get["description"], "Reads a key.");
        EQUALS(get["static"], false);
        EQUALS(get["parameters"].size(), 2u);
        EQUALS(get["parameters"][1]["name"], "key");
        EQUALS(get["parameters"][1]["type"]["name"], "string");
        EQUALS(get["parameters"][1]["description"], "Key to read.");
        EQUALS(get["returns"]["type"]["name"], "any");
        EQUALS(named(probe["functions"], "sum")["parameters"][0]["variadic"], true);
        EQUALS(named(probe["functions"], "twice")["parameters"][0]["type"]["name"], "number");
        EQUALS(named(probe["functions"], "clientOnly").is_null(), true);
        EQUALS(named(probe["properties"], "answer")["readonly"], true);

        // A method and a static written as context functions are recorded as the typed ones are.
        const nlohmann::json counter = SymbolOf(runtimes.Catalog(), "CallCounter");
        const nlohmann::json bump    = named(counter["functions"], "bump");
        EQUALS(bump["static"], false);
        EQUALS(bump["parameters"][0]["name"], "amount");
        EQUALS(bump["parameters"][0]["type"]["optional"], true);
        EQUALS(bump["returns"]["type"]["name"], "number");
        EQUALS(named(counter["functions"], "receiver")["static"], true);
    });
});
