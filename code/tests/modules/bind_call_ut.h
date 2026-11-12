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
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace BindCallTest {

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
    inline Framework::Scripting::ResourceManager *expectedManager = nullptr;

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

    inline const char *KindName(Framework::Scripting::Bind::ValueKind kind) {
        switch (kind) {
        case Framework::Scripting::Bind::ValueKind::Nil: return "Nil";
        case Framework::Scripting::Bind::ValueKind::Boolean: return "Boolean";
        case Framework::Scripting::Bind::ValueKind::Number: return "Number";
        case Framework::Scripting::Bind::ValueKind::String: return "String";
        case Framework::Scripting::Bind::ValueKind::Function: return "Function";
        case Framework::Scripting::Bind::ValueKind::Array: return "Array";
        case Framework::Scripting::Bind::ValueKind::Object: return "Object";
        case Framework::Scripting::Bind::ValueKind::Bound: return "Bound";
        case Framework::Scripting::Bind::ValueKind::Other: return "Other";
        }
        return "?";
    }

    inline const Framework::Scripting::Bind::Class<Counter> &CounterDefinition() {
        static const auto definition =
            Framework::Scripting::Bind::Class<Counter>("CallCounter", "Counts, through context functions.")
                .Constructor<int>(Framework::Scripting::Bind::Doc("Creates a counter.").Param("value", "Initial value."))
                .Method("get", &Counter::get, Framework::Scripting::Bind::Doc("Reads the counter.").Returns("Current value."))
                .Method(
                    "bump",
                    [](Framework::Scripting::Bind::Call &call) {
                        Counter *self = call.Self<Counter>();
                        if (self == nullptr) {
                            return;
                        }
                        self->set(self->get() + static_cast<int>(call.Arg(0).AsInteger().value_or(1)));
                        call.Return(call.ArgCount());
                    },
                    Framework::Scripting::Bind::Doc("Adds to the counter.").OptionalParam("amount", Framework::Scripting::Bind::TypeOf<int>(), "Amount to add, 1 when absent.").ReturnType(Framework::Scripting::Bind::TypeOf<int>()).Returns("How many arguments it was given."))
                .Method(
                    "chain",
                    [](Framework::Scripting::Bind::Call &call) {
                        if (Counter *self = call.Self<Counter>()) {
                            self->set(self->get() + 1);
                        }
                        call.Return(call.This());
                    },
                    Framework::Scripting::Bind::Doc("Adds one.").ReturnType(Framework::Scripting::Bind::Type::This()).Returns("This counter."))
                .Method(
                    "serverBump",
                    [](Framework::Scripting::Bind::Call &call) {
                        if (Counter *self = call.Self<Counter>()) {
                            self->set(self->get() + 100);
                        }
                    },
                    Framework::Scripting::Bind::Doc("Adds a hundred."), Framework::Scripting::Bind::Side::Server)
                .Method(
                    "late",
                    [](Framework::Scripting::Bind::Call &call) {
                        // What it makes first is where Lua would have put a receiver.
                        const Framework::Scripting::Bind::Value made = call.Wrap(Counter(1000));
                        Counter *self                                = call.Self<Counter>();
                        if (self != nullptr) {
                            self->set(self->get() + 1);
                        }
                        call.Return(std::string(self != nullptr ? "receiver" : "none") + ":" + KindName(call.This().Kind()) + ":" + KindName(made.Kind()));
                    },
                    Framework::Scripting::Bind::Doc("Makes a counter, then looks for its receiver.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Static(
                    "receiver",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Self<Counter>() != nullptr);
                    },
                    Framework::Scripting::Bind::Doc("Tells whether a static has a receiver.").ReturnType(Framework::Scripting::Bind::TypeOf<bool>()))
                .Property(
                    "level", Framework::Scripting::Bind::Type::Custom("number | string", "number|string"),
                    [](Framework::Scripting::Bind::Call &call) {
                        if (const Counter *self = call.Self<Counter>()) {
                            call.Return(self->get());
                        }
                    },
                    [](Framework::Scripting::Bind::Call &call) {
                        Counter *self = call.Self<Counter>();
                        if (self == nullptr) {
                            return;
                        }
                        if (const auto number = call.Arg(0).AsInteger()) {
                            self->set(static_cast<int>(*number));
                        }
                        else if (call.Arg(0).IsString()) {
                            self->set(static_cast<int>(call.Arg(0).AsString().size()));
                        }
                        else {
                            call.ThrowTypeError("CallCounter.level: expected a number or a string");
                        }
                    },
                    "The value, set from a number or from the length of a string.")
                .Property(
                    "doubled", Framework::Scripting::Bind::TypeOf<int>(),
                    [](Framework::Scripting::Bind::Call &call) {
                        if (const Counter *self = call.Self<Counter>()) {
                            call.Return(self->get() * 2);
                        }
                    },
                    {}, "Twice the value.", Framework::Scripting::Bind::Side::Server);
        return definition;
    }

    inline const Framework::Scripting::Bind::Class<Stepper> &StepperDefinition() {
        CounterDefinition();
        static const auto definition = Framework::Scripting::Bind::Class<Stepper>("CallStepper", "A counter with a label.").Inherit<Counter>().Constructor<int>(Framework::Scripting::Bind::Doc("Creates a stepper.").Param("value", "Initial value."));
        return definition;
    }

    inline std::string Flags(const Framework::Scripting::Bind::Value &value) {
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

    inline const Framework::Scripting::Bind::Namespace &ProbeDefinition() {
        static const Framework::Scripting::Bind::Namespace definition =
            Framework::Scripting::Bind::Namespace("Probe", "Exercises the dynamic interface.")
                .Function(
                    "kind",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(KindName(call.Arg(0).Kind()));
                    },
                    Framework::Scripting::Bind::Doc("Names the kind of a value.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to inspect.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()).Returns("Name of its kind."))
                .Function(
                    "member",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Arg(0).Member(call.Arg(1).AsString()));
                        ++reachedEnd;
                    },
                    Framework::Scripting::Bind::Doc("Reads a member as a script does.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to read.").Param("key", Framework::Scripting::Bind::TypeOf<std::string>(), "Member to read.").ReturnType(Framework::Scripting::Bind::Type::Any()))
                .Function(
                    "isNull",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Arg(0).IsNull());
                    },
                    Framework::Scripting::Bind::Doc("Tells null from a value left out.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to inspect.").ReturnType(Framework::Scripting::Bind::TypeOf<bool>()))
                .Function(
                    "flags",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(Flags(call.Arg(0)));
                    },
                    Framework::Scripting::Bind::Doc("Spells the kind shorthands of a value.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to inspect.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "sum",
                    [](Framework::Scripting::Bind::Call &call) {
                        double total = 0.0;
                        for (size_t i = 0; i < call.ArgCount(); ++i) {
                            total += call.Arg(i).AsNumber();
                        }
                        call.Return(total);
                    },
                    Framework::Scripting::Bind::Doc("Adds numbers up.").RestParam("values", Framework::Scripting::Bind::Type::Custom("number[]", "number"), "Numbers to add.").ReturnType(Framework::Scripting::Bind::TypeOf<double>()).Returns("Their sum."))
                .Function(
                    "argc",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Arg(call.ArgCount()).IsNil() && call.Arg(99).IsNil() ? static_cast<int64_t>(call.ArgCount()) : int64_t {-1});
                    },
                    Framework::Scripting::Bind::Doc("Counts its arguments.").RestParam("values", Framework::Scripting::Bind::Type::Custom("unknown[]", "any"), "Anything.").ReturnType(Framework::Scripting::Bind::TypeOf<int>()))
                .Function(
                    "echo",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Arg(0));
                    },
                    Framework::Scripting::Bind::Doc("Returns its argument.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to return.").ReturnType(Framework::Scripting::Bind::Type::Any()))
                .Function(
                    "handle",
                    [](Framework::Scripting::Bind::Call &call) {
                        // Out as a V8 handle and back in: the same object in JavaScript, nothing in Lua.
                        const v8::Local<v8::Value> handle = Framework::Scripting::Bind::V8ValueOf(call, call.Arg(0));
                        if (call.GetLanguage() == Framework::Scripting::Language::JavaScript) {
                            call.Return(Framework::Scripting::Bind::V8ValueIn(call, handle));
                        }
                        else {
                            call.Return(handle.IsEmpty() && Framework::Scripting::Bind::V8ValueIn(call, handle).IsNil());
                        }
                    },
                    Framework::Scripting::Bind::Doc("Returns its argument by its V8 handle.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to return.").ReturnType(Framework::Scripting::Bind::Type::Any()))
                .Function(
                    "nothing",
                    [](Framework::Scripting::Bind::Call &) {},
                    Framework::Scripting::Bind::Doc("Returns nothing."))
                .Function(
                    "none",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Nil());
                    },
                    Framework::Scripting::Bind::Doc("Returns no value on purpose.").ReturnType(Framework::Scripting::Bind::Type::Any()))
                .Function(
                    "last",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(1);
                        call.Return("last");
                    },
                    Framework::Scripting::Bind::Doc("Returns twice.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "make",
                    [](Framework::Scripting::Bind::Call &call) {
                        Framework::Scripting::Bind::Value list = call.NewArray();
                        list.Append(call.Integer(1));
                        list.Append(call.Integer(2));
                        list.Append(call.Integer(3));
                        Framework::Scripting::Bind::Value nested = call.NewObject();
                        nested.Set("b", call.String("x"));
                        Framework::Scripting::Bind::Value made = call.NewObject();
                        made.Set("a", call.Integer(1));
                        made.Set("list", list);
                        made.Set("nested", nested);
                        call.Return(made);
                    },
                    Framework::Scripting::Bind::Doc("Builds an object.").ReturnType(Framework::Scripting::Bind::Type::Custom("{ a: number; list: number[]; nested: { b: string } }", "table")))
                .Function(
                    "scalars",
                    [](Framework::Scripting::Bind::Call &call) {
                        Framework::Scripting::Bind::Value list = call.NewArray();
                        list.Append(call.Boolean(true));
                        list.Append(call.Number(1.5));
                        list.Append(call.Integer(-7));
                        list.Append(call.String(std::string_view("a\0b", 3)));
                        call.Return(list);
                    },
                    Framework::Scripting::Bind::Doc("Builds one value of each scalar kind.").ReturnType(Framework::Scripting::Bind::Type::Custom("unknown[]", "any[]")))
                .Function(
                    "keys",
                    [](Framework::Scripting::Bind::Call &call) {
                        Framework::Scripting::Bind::Value keys = call.NewArray();
                        for (const std::string &key : call.Arg(0).Keys()) {
                            keys.Append(call.String(key));
                        }
                        call.Return(keys);
                    },
                    Framework::Scripting::Bind::Doc("Lists the keys of an object.").Param("object", Framework::Scripting::Bind::Type::Any(), "Object to list.").ReturnType(Framework::Scripting::Bind::TypeOf<std::vector<std::string>>()))
                .Function(
                    "get",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Arg(0).Get(call.Arg(1).AsString()));
                        ++reachedEnd;
                    },
                    Framework::Scripting::Bind::Doc("Reads a key.").Param("object", Framework::Scripting::Bind::Type::Any(), "Object to read.").Param("key", Framework::Scripting::Bind::TypeOf<std::string>(), "Key to read.").ReturnType(Framework::Scripting::Bind::Type::Any()))
                .Function(
                    "set",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Arg(0).Set(call.Arg(1).AsString(), call.Arg(2));
                        call.Return(call.Arg(0));
                    },
                    Framework::Scripting::Bind::Doc("Writes a key.").Param("object", Framework::Scripting::Bind::Type::Any(), "Object to write.").Param("key", Framework::Scripting::Bind::TypeOf<std::string>(), "Key to write.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to store.").ReturnType(Framework::Scripting::Bind::Type::Any()))
                .Function(
                    "length",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Arg(0).Length());
                    },
                    Framework::Scripting::Bind::Doc("Measures an array.").Param("array", Framework::Scripting::Bind::Type::Any(), "Array to measure.").ReturnType(Framework::Scripting::Bind::TypeOf<int>()))
                .Function(
                    "at",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Arg(0).At(static_cast<size_t>(call.Arg(1).AsInteger().value_or(0))));
                    },
                    Framework::Scripting::Bind::Doc("Reads an element.").Param("array", Framework::Scripting::Bind::Type::Any(), "Array to read.").Param("index", Framework::Scripting::Bind::TypeOf<int>(), "Zero-based index.").ReturnType(Framework::Scripting::Bind::Type::Any()))
                .Function(
                    "append",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Arg(0).Append(call.Arg(1));
                        call.Return(call.Arg(0));
                    },
                    Framework::Scripting::Bind::Doc("Appends an element.").Param("array", Framework::Scripting::Bind::Type::Any(), "Array to grow.").Param("value", Framework::Scripting::Bind::Type::Any(), "Element to add.").ReturnType(Framework::Scripting::Bind::Type::Any()))
                .Function(
                    "integer",
                    [](Framework::Scripting::Bind::Call &call) {
                        const std::optional<int64_t> integer = call.Arg(0).AsInteger();
                        call.Return(integer ? std::to_string(*integer) : std::string("none"));
                    },
                    Framework::Scripting::Bind::Doc("Reads an integer.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to read.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "strict",
                    [](Framework::Scripting::Bind::Call &call) {
                        const Framework::Scripting::Bind::Value value = call.Arg(0);
                        call.Return("[" + value.AsString() + "]" + (value.AsBoolean() ? "t" : "f") + (value.AsNumber() == 0.0 ? "0" : "n"));
                    },
                    Framework::Scripting::Bind::Doc("Reads a value as a string, a boolean and a number.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to read.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "display",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Arg(0).ToDisplayString());
                        ++reachedEnd;
                    },
                    Framework::Scripting::Bind::Doc("Spells a value.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to spell.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "isList",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Arg(0).IsList());
                    },
                    Framework::Scripting::Bind::Doc("Tells whether a value passes for a list.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to inspect.").ReturnType(Framework::Scripting::Bind::TypeOf<bool>()))
                .Function(
                    "null",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Null());
                    },
                    Framework::Scripting::Bind::Doc("Returns null.").ReturnType(Framework::Scripting::Bind::Type::Any()))
                .Function(
                    "nullKind",
                    [](Framework::Scripting::Bind::Call &call) {
                        Framework::Scripting::Bind::Value object = call.NewObject();
                        object.Set("k", call.Null());
                        call.Return(std::string(KindName(call.Null().Kind())) + KindName(object.Get("k").Kind()) + std::to_string(object.Keys().size()));
                    },
                    Framework::Scripting::Bind::Doc("Names the kind of null, alone and stored.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "thisKind",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(KindName(call.This().Kind()));
                    },
                    Framework::Scripting::Bind::Doc("Names the kind of its receiver.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "capture",
                    [](Framework::Scripting::Bind::Call &call) {
                        // The mistake a handle has to survive: it is kept beyond its call.
                        call.Return(call.NewFunction([kept = call.Arg(0)](Framework::Scripting::Bind::Call &inner) {
                            inner.Return(kept);
                        }));
                    },
                    Framework::Scripting::Bind::Doc("Makes a function that returns a value it should not have kept.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to keep.").ReturnType(Framework::Scripting::Bind::Type::Custom("() => unknown", "fun(): any")))
                .Function(
                    "stale",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.NewFunction([kept = call.Arg(0)](Framework::Scripting::Bind::Call &inner) {
                            Framework::Scripting::Bind::Value object = inner.NewObject();
                            object.Set("k", inner.Integer(1));
                            object.Set("k", kept);
                            Framework::Scripting::Bind::Value list = inner.NewArray();
                            list.Append(kept);
                            inner.Return(std::string(KindName(kept.Kind())) + "[" + kept.AsString() + kept.ToDisplayString() + "]" + std::to_string(kept.Length()) + std::to_string(kept.Keys().size()) + KindName(kept.Get("a").Kind()) + KindName(kept.At(0).Kind()) + ":"
                                         + KindName(object.Get("k").Kind()) + KindName(list.At(0).Kind()));
                        }));
                    },
                    Framework::Scripting::Bind::Doc("Makes a function that reads a value it should not have kept.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to keep.").ReturnType(Framework::Scripting::Bind::Type::Custom("(...args: unknown[]) => string", "fun(...): string")))
                .Function(
                    "nested",
                    [](Framework::Scripting::Bind::Call &call) {
                        // The object spells itself by calling `peek`, which runs inside this call and reads its handle.
                        const Framework::Scripting::Bind::Value outer = call.Arg(1);
                        call.Arg(0).Set("peek", call.NewFunction([outer](Framework::Scripting::Bind::Call &inner) {
                            inner.Return(KindName(outer.Kind()));
                        }));
                        const std::string during = call.Arg(0).ToDisplayString();
                        call.Return(during + ":" + KindName(outer.Kind()));
                    },
                    Framework::Scripting::Bind::Doc("Reads a handle of this call from a call nested in it, then from this one.").Param("object", Framework::Scripting::Bind::Type::Any(), "Object that spells itself with peek().").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to read.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "spell",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Arg(0).ToDisplayString();
                        thrownAfterDisplay = call.HasThrown();
                        call.Arg(1).Get("a");
                        ++reachedEnd;
                    },
                    Framework::Scripting::Bind::Doc("Spells a value, then reads a key of another.").Param("value", Framework::Scripting::Bind::Type::Any(), "Value to spell.").Param("object", Framework::Scripting::Bind::Type::Any(), "Object to read.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "keep",
                    [](Framework::Scripting::Bind::Call &call) {
                        Stepper stepper(1);
                        stepper.text = std::string(64, 's');
                        call.Wrap(std::move(stepper));
                        leftAfterWrap = stepper.text.size();
                    },
                    Framework::Scripting::Bind::Doc("Wraps a stepper and notes what is left of it."))
                .Function(
                    "adder",
                    [](Framework::Scripting::Bind::Call &call) {
                        const int64_t amount = call.Arg(0).AsInteger().value_or(0);
                        call.Return(call.NewFunction([amount](Framework::Scripting::Bind::Call &inner) {
                            inner.Return(amount + inner.Arg(0).AsInteger().value_or(0));
                        }));
                    },
                    Framework::Scripting::Bind::Doc("Makes a function that adds.").Param("amount", Framework::Scripting::Bind::TypeOf<int>(), "What it adds.").ReturnType(Framework::Scripting::Bind::Type::Custom("(value: number) => number", "fun(value: integer): integer")))
                .Function(
                    "hold",
                    [](Framework::Scripting::Bind::Call &call) {
                        // The function keeps `held` alive for as long as it lives itself.
                        call.Return(call.NewFunction([held = Held()](Framework::Scripting::Bind::Call &inner) {
                            inner.Return(*held);
                        }));
                    },
                    Framework::Scripting::Bind::Doc("Makes a function that holds a value.").ReturnType(Framework::Scripting::Bind::Type::Custom("() => number", "fun(): integer")))
                .Function(
                    "carry",
                    [](Framework::Scripting::Bind::Call &call) {
                        // The function carries its argument and gives it back, with the kind Data has outside it.
                        const std::string outside = KindName(call.Data().Kind());
                        call.Return(call.NewFunction(
                            [outside](Framework::Scripting::Bind::Call &inner) {
                                if (inner.ArgCount() > 0) {
                                    inner.Return(outside + KindName(inner.Data().Kind()));
                                }
                                else {
                                    inner.Return(inner.Data());
                                }
                            },
                            call.Arg(0)));
                    },
                    Framework::Scripting::Bind::Doc("Makes a function that carries a value.").Param("value", Framework::Scripting::Bind::Type::Any(), "What it carries.").ReturnType(Framework::Scripting::Bind::Type::Custom("(kind?: boolean) => unknown", "fun(kind?: boolean): any")))
                .Function(
                    "failType",
                    [](Framework::Scripting::Bind::Call &call) {
                        const bool before = call.HasThrown();
                        call.ThrowTypeError("Probe.failType: expected (text)");
                        call.ThrowError("Probe.failType: thrown second");
                        call.Return(1);
                        if (!before && call.HasThrown()) {
                            ++reachedEnd;
                        }
                    },
                    Framework::Scripting::Bind::Doc("Fails as a wrong argument does."))
                .Function(
                    "failRange",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.ThrowRangeError("Probe.failRange: expected a tier from 0 to 2");
                        call.ThrowError("Probe.failRange: thrown second");
                        call.Return(1);
                    },
                    Framework::Scripting::Bind::Doc("Fails as a number out of range does."))
                .Function(
                    "fail",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(1);
                        call.ThrowError("Probe.fail: network peer unavailable");
                        call.ThrowTypeError("Probe.fail: thrown second");
                        ++reachedEnd;
                    },
                    Framework::Scripting::Bind::Doc("Fails as a missing subsystem does."))
                .Function(
                    "raise",
                    [](Framework::Scripting::Bind::Call &) {
                        throw std::runtime_error("Probe.raise: exception");
                    },
                    Framework::Scripting::Bind::Doc("Throws a C++ exception."))
                .Function(
                    "bound",
                    [](Framework::Scripting::Bind::Call &call) {
                        const Counter *counter = call.Arg(0).AsBound<Counter>();
                        call.Return(counter != nullptr ? counter->get() : -1);
                    },
                    Framework::Scripting::Bind::Doc("Reads a counter.").Param("counter", Framework::Scripting::Bind::Type::Any(), "Counter to read.").ReturnType(Framework::Scripting::Bind::TypeOf<int>()))
                .Function(
                    "wrap",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.Wrap(Counter(static_cast<int>(call.Arg(0).AsInteger().value_or(0)))));
                        ++reachedEnd;
                    },
                    Framework::Scripting::Bind::Doc("Makes a counter.").Param("value", Framework::Scripting::Bind::TypeOf<int>(), "Initial value.").ReturnType(Framework::Scripting::Bind::Type::Class("CallCounter")))
                .Function(
                    "copy",
                    [](Framework::Scripting::Bind::Call &call) {
                        if (const Counter *counter = call.Arg(0).AsBound<Counter>()) {
                            call.Return(call.Wrap(*counter));
                        }
                    },
                    Framework::Scripting::Bind::Doc("Copies a counter.").Param("counter", Framework::Scripting::Bind::Type::Class("CallCounter"), "Counter to copy.").ReturnType(Framework::Scripting::Bind::Type::Class("CallCounter")))
                .Function(
                    "side",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.GetSide() == Framework::Scripting::Bind::Side::Client ? "client" : "server");
                    },
                    Framework::Scripting::Bind::Doc("Names the side of the runtime.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "language",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.GetLanguage() == Framework::Scripting::Language::Lua ? "lua" : "javascript");
                    },
                    Framework::Scripting::Bind::Doc("Names the language of the caller.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "resource",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.ResourceName());
                    },
                    Framework::Scripting::Bind::Doc("Names the calling resource.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "ownerOf",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(call.OwnerOf(call.Arg(0)));
                    },
                    Framework::Scripting::Bind::Doc("Names the resource that owns a function.").Param("value", Framework::Scripting::Bind::Type::Any(), "The function.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "manager",
                    [](Framework::Scripting::Bind::Call &call) {
                        const Framework::Scripting::ResourceManager *manager = call.GetResourceManager();
                        call.Return(manager == nullptr ? "none" : manager == expectedManager ? "registered" : "other");
                    },
                    Framework::Scripting::Bind::Doc("Tells which resource manager the call has.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()))
                .Function(
                    "twice",
                    [](int value) {
                        return value * 2;
                    },
                    Framework::Scripting::Bind::Doc("Doubles a number.").Param("value", "Number to double.").Returns("Twice the number."))
                .Function(
                    "serverOnly",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(true);
                    },
                    Framework::Scripting::Bind::Doc("Exists on the server.").ReturnType(Framework::Scripting::Bind::TypeOf<bool>()), Framework::Scripting::Bind::Side::Server)
                .Function(
                    "clientOnly",
                    [](Framework::Scripting::Bind::Call &call) {
                        call.Return(true);
                    },
                    Framework::Scripting::Bind::Doc("Exists on the client.").ReturnType(Framework::Scripting::Bind::TypeOf<bool>()), Framework::Scripting::Bind::Side::Client)
                .Constant("answer", 42.0, "The answer.")
                .Constant("ratio", 0.5, "A ratio.")
                .Constant("label", std::string("probe"), "A label.")
                .Constant("serverLabel", std::string("server"), "A label of the server.", Framework::Scripting::Bind::Side::Server)
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

    inline Framework::Scripting::LuaRuntimeOptions LuaOptions(Framework::Scripting::Bind::Side side) {
        Framework::Scripting::LuaRuntimeOptions options = LuaRuntimeTest::Options();
        options.resourceName      = "lua-probe";
        options.side              = side;
        return options;
    }

    // A JavaScript runtime and a Lua runtime of one side, each with the value types, the counters and Probe as
    // globals. Lua also has dump(value), which spells a table the way JSON.stringify spells an object.
    class Runtimes final {
      public:
        explicit Runtimes(Framework::Scripting::Bind::Side side = Framework::Scripting::Bind::Side::Server, bool withCounters = true): _lua(LuaOptions(side)) {
            static int count = 0;
            _engine.Init();
            std::string error;
            _js          = _engine.CreateRuntime(error);
            _catalogName = "bind_call/" + std::to_string(++count);
            Framework::Scripting::SetScriptingCatalog(Isolate(), _catalogName);
            Framework::Scripting::SetScriptingEnvironment(Isolate(), side == Framework::Scripting::Bind::Side::Client);

            Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                v8::Local<v8::Object> global = context->Global();
                {
                    Framework::Scripting::Bind::Registrar registrar(isolate, isolate->GetCurrentContext(), global);
                    Framework::Scripting::Builtins::RegisterValueTypes(registrar);
                }
                if (withCounters) {
                    _counter = CounterDefinition().MaterialiseV8(isolate, Catalog(), side);
                    _stepper = StepperDefinition().MaterialiseV8(isolate, Catalog(), side);
                    global->Set(context, v8pp::to_v8(isolate, "CallCounter"), _counter->js_function_template()->GetFunction(context).ToLocalChecked()).Check();
                    global->Set(context, v8pp::to_v8(isolate, "CallStepper"), _stepper->js_function_template()->GetFunction(context).ToLocalChecked()).Check();
                }
                ProbeDefinition().MaterialiseV8(isolate, context, global, Catalog(), side);
            });

            lua_State *L = _lua.State();
            {
                Framework::Scripting::Bind::Registrar registrar;
                Framework::Scripting::Builtins::RegisterValueTypes(registrar);
                registrar.Lua().Apply(L, side);
            }
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
            Framework::Scripting::Bind::SetCallResourceManager(Isolate(), nullptr);
            Framework::Scripting::Bind::SetCallResourceManager(&_lua, nullptr);
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

        Framework::Scripting::LuaRuntime &LuaSide() {
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
        Framework::Scripting::LuaRuntime _lua;
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

    // --- Describing a context function ---

    IT("describes a context function from its Doc alone", {
        const Framework::Scripting::Bind::Doc doc           = Framework::Scripting::Bind::Doc("Writes a line.")
                                                                  .Param("level", Framework::Scripting::Bind::TypeOf<std::string>(), "How loud.")
                                                                  .OptionalParam("tag", Framework::Scripting::Bind::TypeOf<int>(), "A tag.")
                                                                  .RestParam("values", Framework::Scripting::Bind::Type::Custom("unknown[]", "any"), "What to write.")
                                                                  .ReturnType(Framework::Scripting::Bind::TypeOf<bool>())
                                                                  .Returns("Whether it was written.");
        const Framework::Scripting::Bind::FunctionDesc desc = Framework::Scripting::Bind::DescribeFunction<Framework::Scripting::Bind::NativeFunction>("write", doc, true, Framework::Scripting::Bind::Side::Client);

        EQUALS(desc.name, "write");
        EQUALS(desc.description, "Writes a line.");
        EQUALS(desc.isStatic, true);
        EQUALS(desc.side == Framework::Scripting::Bind::Side::Client, true);
        EQUALS(Framework::Scripting::Bind::ToTypeScript(desc.returns), "boolean");
        EQUALS(desc.returnsDescription, "Whether it was written.");
        EQUALS(desc.params.size(), 3u);
        EQUALS(desc.params[0].name + ":" + Framework::Scripting::Bind::ToTypeScript(desc.params[0].type) + ":" + desc.params[0].description, "level:string:How loud.");
        EQUALS(desc.params[0].optional || desc.params[0].rest, false);
        EQUALS(desc.params[1].name + ":" + Framework::Scripting::Bind::ToLuaAnnotation(desc.params[1].type), "tag:integer");
        EQUALS(desc.params[1].optional && !desc.params[1].rest, true);
        EQUALS(desc.params[2].name + ":" + Framework::Scripting::Bind::ToTypeScript(desc.params[2].type) + ":" + Framework::Scripting::Bind::ToLuaAnnotation(desc.params[2].type), "values:unknown[]:any");
        EQUALS(desc.params[2].rest && !desc.params[2].optional, true);

        const auto lambda = [](Framework::Scripting::Bind::Call &) {};
        EQUALS(Framework::Scripting::Bind::ToTypeScript(Framework::Scripting::Bind::DescribeFunction<decltype(lambda)>("quiet", Framework::Scripting::Bind::Doc("Does nothing."), false, Framework::Scripting::Bind::Side::Shared).returns), "void");
    });

    IT("refuses a Doc a function cannot have", {
        EQUALS(BindCallTest::LogicErrorOf([] {
            Framework::Scripting::Bind::DescribeFunction<Framework::Scripting::Bind::NativeFunction>("Probe.write", Framework::Scripting::Bind::Doc("Writes.").Param("level", "How loud."), true, Framework::Scripting::Bind::Side::Shared);
        }),
            "Bind: 'Probe.write' is a context function, so its Doc gives every parameter a type, and 'level' has none");
        EQUALS(BindCallTest::LogicErrorOf([] {
            Framework::Scripting::Bind::Doc("Writes.").RestParam("values", Framework::Scripting::Bind::Type::Any(), "What to write.").Param("level", "How loud.");
        }),
            "Bind::Doc: 'level' comes after the rest parameter 'values', which is the last one");
        EQUALS(BindCallTest::LogicErrorOf([] {
            Framework::Scripting::Bind::DescribeFunction<int (*)(int)>("Probe.twice", Framework::Scripting::Bind::Doc("Doubles.").RestParam("values", Framework::Scripting::Bind::Type::Any(), "Numbers."), true, Framework::Scripting::Bind::Side::Shared);
        }),
            "Bind: 'Probe.twice' documents the rest parameter 'values', which only a context function takes");
    });

    IT("keeps the type a Doc gives a parameter of a typed callable", {
        const Framework::Scripting::Bind::FunctionDesc desc =
            Framework::Scripting::Bind::DescribeFunction<int (*)(int)>("pick", Framework::Scripting::Bind::Doc("Picks.").Param("which", Framework::Scripting::Bind::Type::Custom("0 | 1", "integer"), "Which one."), true, Framework::Scripting::Bind::Side::Shared);
        EQUALS(Framework::Scripting::Bind::ToTypeScript(desc.params[0].type), "0 | 1");
    });

    // --- Namespace, as a definition ---

    IT("describes a namespace and records it in the catalog", {
        const Framework::Scripting::Bind::ClassDesc &desc = BindCallTest::ProbeDefinition().Describe();
        EQUALS(desc.name, "Probe");
        EQUALS(desc.description, "Exercises the dynamic interface.");
        EQUALS(desc.isNamespace, true);
        EQUALS(Framework::Scripting::Bind::ApiCatalog::Get().Find("Probe") == &desc, true);

        const auto sum = std::find_if(desc.functions.begin(), desc.functions.end(), [](const Framework::Scripting::Bind::FunctionDesc &function) {
            return function.name == "sum";
        });
        EQUALS(sum != desc.functions.end(), true);
        EQUALS(sum->isStatic, true);
        EQUALS(sum->params.size(), 1u);
        EQUALS(sum->params[0].rest, true);
        EQUALS(Framework::Scripting::Bind::ToLuaAnnotation(sum->returns), "number");

        const auto twice = std::find_if(desc.functions.begin(), desc.functions.end(), [](const Framework::Scripting::Bind::FunctionDesc &function) {
            return function.name == "twice";
        });
        EQUALS(twice != desc.functions.end(), true);
        EQUALS(twice->params[0].name + ":" + Framework::Scripting::Bind::ToLuaAnnotation(twice->params[0].type) + "->" + Framework::Scripting::Bind::ToLuaAnnotation(twice->returns), "value:integer->integer");

        const auto serverOnly = std::find_if(desc.functions.begin(), desc.functions.end(), [](const Framework::Scripting::Bind::FunctionDesc &function) {
            return function.name == "serverOnly";
        });
        EQUALS(serverOnly != desc.functions.end() && serverOnly->side == Framework::Scripting::Bind::Side::Server, true);

        EQUALS(desc.properties.size(), 7u);
        EQUALS(desc.properties[4].name + ":" + Framework::Scripting::Bind::ToTypeScript(desc.properties[4].type) + ":" + Framework::Scripting::Bind::ToLuaAnnotation(desc.properties[4].type), "enabled:boolean:boolean");
        EQUALS(desc.properties[5].name + ":" + Framework::Scripting::Bind::ToTypeScript(desc.properties[5].type), "count:number");
        EQUALS(desc.properties[6].name + ":" + Framework::Scripting::Bind::ToTypeScript(desc.properties[6].type), "word:string");
        EQUALS(desc.properties[0].name + ":" + Framework::Scripting::Bind::ToTypeScript(desc.properties[0].type) + ":" + desc.properties[0].description, "answer:number:The answer.");
        EQUALS(desc.properties[0].readonly && desc.properties[0].isStatic, true);
        EQUALS(desc.properties[2].name + ":" + Framework::Scripting::Bind::ToTypeScript(desc.properties[2].type), "label:string");
        EQUALS(desc.properties[3].side == Framework::Scripting::Bind::Side::Server, true);
    });

    IT("refuses a second member of the same name, function or constant", {
        Framework::Scripting::Bind::Namespace twice("ProbeTwice", "Declares members twice.");
        twice.Function("run", [](Framework::Scripting::Bind::Call &) {}, Framework::Scripting::Bind::Doc("Runs.")).Constant("limit", 1.0, "A limit.");

        const std::string expected = "Bind: 'ProbeTwice.run' is declared twice, and a member cannot be overloaded";
        EQUALS(BindCallTest::LogicErrorOf([&] {
            twice.Function("run", [](Framework::Scripting::Bind::Call &) {}, Framework::Scripting::Bind::Doc("Runs again."));
        }),
            expected);
        EQUALS(BindCallTest::LogicErrorOf([&] {
            twice.Function(
                "run",
                [](int value) {
                    return value;
                },
                Framework::Scripting::Bind::Doc("Runs typed.").Param("value", "A value."));
        }),
            expected);
        EQUALS(BindCallTest::LogicErrorOf([&] {
            twice.Constant("run", 2.0, "A constant.");
        }),
            expected);
        EQUALS(BindCallTest::LogicErrorOf([&] {
            twice.Constant("run", std::string("text"), "A constant.");
        }),
            expected);
        EQUALS(BindCallTest::LogicErrorOf([&] {
            twice.Function("limit", [](Framework::Scripting::Bind::Call &) {}, Framework::Scripting::Bind::Doc("Limits."));
        }),
            "Bind: 'ProbeTwice.limit' is declared twice, and a member cannot be overloaded");
        EQUALS(twice.Describe().functions.size(), 1u);
        EQUALS(twice.Describe().properties.size(), 1u);
    });

    // --- Values ---

    IT("tells the kind of a value alike in both languages", {
        BindCallTest::Runtimes runtimes;
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
        BindCallTest::Runtimes runtimes;
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
        BindCallTest::Runtimes runtimes;
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
        BindCallTest::Runtimes runtimes;
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
        BindCallTest::Runtimes runtimes;
        BindCallTest::reachedEnd = 0;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.display('text')", "text");
        mismatches += runtimes.Both("Probe.display(12)", "12");
        mismatches += runtimes.Both("Probe.display(true)", "true");
        mismatches += runtimes.Both("Probe.display(new Vector3(1, 2, 3))", "Probe.display(Vector3.new(1, 2, 3))", "Vector3(1, 2, 3)");
        mismatches += runtimes.Both("Probe.display({ toString() { return 'mine' } })", "Probe.display(setmetatable({}, { __tostring = function() return 'mine' end }))", "mine");
        // A value that fails to spell itself is spelled as nothing, and the function goes on.
        mismatches += runtimes.Both("Probe.display({ toString() { throw new Error('no') } })", "Probe.display(setmetatable({}, { __tostring = function() error('no') end }))", "");
        EQUALS(mismatches, "");
        EQUALS(BindCallTest::reachedEnd, 12);

        EQUALS(runtimes.Js("Probe.display()"), "undefined");
        EQUALS(runtimes.Js("Probe.display(null)"), "null");
        EQUALS(runtimes.Lua("Probe.display()"), "nil");
        EQUALS(runtimes.Lua("Probe.display(1.0)"), "1.0");
    });

    IT("passes a value through unchanged", {
        BindCallTest::Runtimes runtimes;
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

    IT("makes a function that carries a script value the collector sees", {
        BindCallTest::Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("{ const o = {}; Probe.carry(o)() === o }", "(function() local o = {} return rawequal(Probe.carry(o)(), o) end)()", "true");
        mismatches += runtimes.Both("{ const f = () => 1; Probe.carry(f)() === f }", "(function() local f = function() end return rawequal(Probe.carry(f)(), f) end)()", "true");
        mismatches += runtimes.Both("Probe.carry('text')()", "text");
        mismatches += runtimes.Both("Probe.carry({})(true)", "NilObject");
        mismatches += runtimes.Both("Probe.carry()(true)", "NilNil");
        mismatches += runtimes.Both("{ const f = Probe.carry(5); f() + f() }", "(function() local f = Probe.carry(5) return f() + f() end)()", "10");
        EQUALS(mismatches, "");

        // A value that leads back to the function that carries it is collected with it: the host roots neither.
        EQUALS(LuaRuntimeTest::Run(runtimes.LuaSide(), "kept = setmetatable({}, {__mode = 'k'}) for i = 1, 50 do local o = {} o.f = Probe.carry(o) kept[o] = true end"), "");
        lua_gc(runtimes.LuaSide().State(), LUA_GCCOLLECT);
        EQUALS(runtimes.Lua("(function() local n = 0 for _ in pairs(kept) do n = n + 1 end return n end)()"), "0");

        EQUALS(runtimes.Js("globalThis.plain = []; for (let i = 0; i < 50; i++) { const o = {}; plain.push(new WeakRef(o)); } plain.length"), "50");
        // A WeakRef holds its target until the job that made it has ended: the collection is told it has.
        runtimes.Enter([](v8::Isolate *isolate, v8::Local<v8::Context>) {
            isolate->ClearKeptObjects();
            isolate->LowMemoryNotification();
        });
        EQUALS(runtimes.Js("plain.filter((ref) => ref.deref() !== undefined).length"), "0");
        EQUALS(runtimes.Js("globalThis.refs = []; for (let i = 0; i < 50; i++) { const o = {}; o.f = Probe.carry(o); refs.push(new WeakRef(o)); } refs.length"), "50");
        runtimes.Enter([](v8::Isolate *isolate, v8::Local<v8::Context>) {
            isolate->ClearKeptObjects();
            isolate->LowMemoryNotification();
        });
        EQUALS(runtimes.Js("refs.filter((ref) => ref.deref() !== undefined).length"), "0");
    });

    IT("keeps what a function carries out of the reach of a script that tampers with the prototypes", {
        BindCallTest::Runtimes runtimes;
        // Accessors where an ordinary read or write of an array or an object would look: none of them is asked,
        // nothing is handed to the script, and the functions work.
        EQUALS(runtimes.Js(R"((() => {
            let result = 'none';
            globalThis.leaked = [];
            const hostile = [[Array.prototype, 0], [Array.prototype, 1], [Object.prototype, 0], [Object.prototype, 1], [Object.prototype, 'framework.carrier.holder'], [Object.prototype, 'framework.carrier.value']];
            for (const [target, key] of hostile) {
                Object.defineProperty(target, key, { set(value) { leaked.push(String(key)); }, get() { leaked.push(String(key)); return {}; }, configurable: true });
            }
            try {
                const o = {};
                const carry = Probe.carry(o);
                const plain = Probe.adder(2);
                result = [carry() === o, carry(true), plain(3)].join('|');
            } finally {
                for (const [target, key] of hostile) { delete target[key]; }
            }
            return result + '|' + leaked.length;
        })())"),
            "true|NilObject|5|0");
        EQUALS(runtimes.Js("[].length + Object.keys({}).length"), "0");
    });

    IT("gives the V8 handle of a JavaScript value, and takes one back", {
        BindCallTest::Runtimes runtimes;
        EQUALS(runtimes.Js("{ const o = {}; Probe.handle(o) === o }"), "true");
        EQUALS(runtimes.Js("Probe.handle(7)"), "7");
        EQUALS(runtimes.Js("Probe.handle() === undefined"), "true");
        EQUALS(runtimes.Lua("Probe.handle({})"), "true");
    });

    IT("builds objects and arrays", {
        BindCallTest::Runtimes runtimes;
        const std::string made = R"({"a":1,"list":[1,2,3],"nested":{"b":"x"}})";
        EQUALS(runtimes.Both("JSON.stringify(Probe.make())", "dump(Probe.make())", made), "");
        EQUALS(runtimes.Both("Array.isArray(Probe.make().list)", "#Probe.make().list == 3", "true"), "");
        EQUALS(runtimes.Both("Probe.kind(Probe.make().list) + Probe.kind(Probe.make())", "Probe.kind(Probe.make().list) .. Probe.kind(Probe.make())", "ArrayObject"), "");

        EQUALS(runtimes.Js("JSON.stringify(Probe.scalars())"), R"([true,1.5,-7,"a\u0000b"])");
        EQUALS(runtimes.Lua("(function() local s = Probe.scalars() return tostring(s[1]) .. ',' .. s[2] .. ',' .. math.type(s[3]) .. s[3] .. ',' .. #s[4] .. s[4]:byte(2) end)()"), "true,1.5,integer-7,30");
    });

    IT("reads and writes the keys of an object", {
        BindCallTest::Runtimes runtimes;
        BindCallTest::reachedEnd = 0;
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
        EQUALS(BindCallTest::reachedEnd, 10);
    });

    IT("lists keys in one order, integers before strings", {
        BindCallTest::Runtimes runtimes;
        std::string mismatches;
        // JavaScript keeps the order strings were added in; Lua, which has none, sorts them.
        mismatches += runtimes.Both("Probe.keys({ x: 1, 10: 'j', y: 2, 2: 'b' }).join()", "table.concat(Probe.keys({ y = 2, [10] = 'j', x = 1, [2] = 'b' }), ',')", "2,10,x,y");
        mismatches += runtimes.Both("Probe.keys({ 3: 'c', 1: 'a' }).join()", "table.concat(Probe.keys({ [3] = 'c', [1] = 'a' }), ',')", "1,3");
        EQUALS(mismatches, "");
        EQUALS(runtimes.Lua("table.concat(Probe.keys({ zeta = 1, alpha = 2, Mid = 3, [true] = 4, [1.5] = 5 }), ',')"), "Mid,alpha,zeta");
        EQUALS(runtimes.Lua("table.concat(Probe.keys({ [5] = 'e', [-2] = 'm', [0] = 'z' }), ',')"), "-2,0,5");
    });

    IT("counts the keys of an array from zero", {
        BindCallTest::Runtimes runtimes;
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
        BindCallTest::Runtimes runtimes;
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
        BindCallTest::Runtimes runtimes;
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

    IT("reads a member of a bound object as a script does, and of a table as its key", {
        BindCallTest::Runtimes runtimes;
        std::string mismatches;
        // A property of the class, of whatever class it is.
        mismatches += runtimes.Both("Probe.member(new Vector3(1, 2, 3), 'y')", "math.tointeger(Probe.member(Vector3.new(1, 2, 3), 'y'))", "2");
        mismatches += runtimes.Both("Probe.member(new Color(0.5, 0, 0, 1), 'r')", "Probe.member(Color.new(0.5, 0, 0, 1), 'r')", "0.5");
        mismatches += runtimes.Both("Probe.kind(Probe.member(new Vector3(1, 2, 3), 'w'))", "Probe.kind(Probe.member(Vector3.new(1, 2, 3), 'w'))", "Nil");
        // A plain object or table is read as Get reads it.
        mismatches += runtimes.Both("Probe.member({ id: 7 }, 'id')", "Probe.member({ id = 7 }, 'id')", "7");
        mismatches += runtimes.Both("Probe.member([4, 5], '1')", "Probe.member({ 4, 5 }, '1')", "5");
        mismatches += runtimes.Both("Probe.kind(Probe.member({}, 'id'))", "Nil");
        // Nothing else has members: a scalar, a function.
        mismatches += runtimes.Both("Probe.kind(Probe.member('text', 'length')) + Probe.kind(Probe.member(5, 'id')) + Probe.kind(Probe.member(Probe.kind, 'name'))", "Probe.kind(Probe.member('text', 'length')) .. Probe.kind(Probe.member(5, 'id')) .. Probe.kind(Probe.member(Probe.kind, 'name'))", "NilNilNil");
        EQUALS(mismatches, "");

        // A method is what JavaScript reads under its name, and no property in Lua.
        EQUALS(runtimes.Js("Probe.kind(Probe.member(new Vector3(1, 2, 3), 'add'))"), "Function");
        EQUALS(runtimes.Lua("Probe.kind(Probe.member(Vector3.new(1, 2, 3), 'add'))"), "Nil");
        // JavaScript reads what a script put on the object too.
        EQUALS(runtimes.Js("{ const v = new Vector3(1, 2, 3); v.id = 9; Probe.member(v, 'id') }"), "9");
        // Of a script's subclass, what it set on the instance and what the bound class answers: the instance
        // is the one the bound constructor made, so a getter of the subclass is not on it.
        EQUALS(runtimes.Js("{ class Sub extends Vector3 { constructor() { super(1, 2, 3); this.own = 4; } get id() { return 5; } }; const s = new Sub(); [Probe.kind(Probe.member(s, 'id')), Probe.member(s, 'own'), Probe.member(s, 'x')].join() }"), "Nil,4,1");
        // A Lua table is read raw, as Get reads it.
        EQUALS(runtimes.Lua("Probe.kind(Probe.member(setmetatable({}, { __index = function() error('ran') end }), 'a'))"), "Nil");

        // What reading throws is thrown once the function has returned.
        BindCallTest::reachedEnd = 0;
        EQUALS(runtimes.Js("{ const v = new Vector3(1, 2, 3); Object.defineProperty(v, 'id', { get() { throw new RangeError('accessor ran'); } }); Probe.member(v, 'id') }"), "<threw> RangeError: accessor ran");
        EQUALS(BindCallTest::reachedEnd, 1);
    });

    IT("tells the null of JavaScript from a value left out, and finds none in Lua", {
        BindCallTest::Runtimes runtimes;
        EQUALS(runtimes.Js("[Probe.isNull(null), Probe.isNull(undefined), Probe.isNull(), Probe.isNull(false), Probe.isNull(0), Probe.isNull({}), Probe.isNull(Probe.get({ a: null }, 'a')), Probe.isNull(Probe.get({}, 'a'))].join()"),
            "true,false,false,false,false,false,true,false");
        EQUALS(runtimes.Js("Probe.kind(null) + Probe.kind(undefined)"), "NilNil");
        EQUALS(runtimes.Lua("tostring(Probe.isNull(nil)) .. tostring(Probe.isNull()) .. tostring(Probe.isNull(false)) .. tostring(Probe.isNull({}))"), "falsefalsefalsefalse");
    });

    IT("reads a Lua table raw, and a JavaScript object as a script does", {
        BindCallTest::Runtimes runtimes;
        BindCallTest::reachedEnd = 0;

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
        EQUALS(BindCallTest::reachedEnd, 3);
    });

    IT("reads and grows an array from zero", {
        BindCallTest::Runtimes runtimes;
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
        BindCallTest::Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.adder(2)(3)", "5");
        mismatches += runtimes.Both("{ const add = Probe.adder(10); add(1) + add(2) }", "(function() local add = Probe.adder(10) return add(1) + add(2) end)()", "23");
        mismatches += runtimes.Both("Probe.adder(1) === Probe.adder(1)", "rawequal(Probe.adder(1), Probe.adder(1))", "false");
        mismatches += runtimes.Both("typeof Probe.adder(1)", "type(Probe.adder(1))", "function");
        EQUALS(mismatches, "");
    });

    IT("reads a handle kept beyond its call as nil", {
        BindCallTest::Runtimes runtimes;
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
        BindCallTest::Runtimes runtimes;
        // The call nested in it sees nil; back in its own call the handle is good again.
        EQUALS(runtimes.Js("Probe.nested({ toString() { return this.peek() } }, 'text')"), "Nil:String");
        EQUALS(runtimes.Lua("Probe.nested(setmetatable({}, { __tostring = function(self) return self.peek() end }), 'text')"), "Nil:String");
    });

    IT("keeps a native function for as long as the script function lives", {
        BindCallTest::HeldSlot()       = std::make_shared<int>(7);
        const std::weak_ptr<int> alive = BindCallTest::HeldSlot();
        {
            BindCallTest::Runtimes runtimes;
            EQUALS(runtimes.Both("globalThis.kept = Probe.hold(); kept()", "(function() kept = Probe.hold() return kept() end)()", "7"), "");
            BindCallTest::HeldSlot().reset();
            EQUALS(alive.expired(), false);
            EQUALS(runtimes.Both("kept()", "7"), "");

            // Lua frees it when the function is collected; JavaScript at the latest with its runtime.
            LuaRuntimeTest::Run(runtimes.LuaSide(), "kept = nil");
            lua_gc(runtimes.LuaSide().State(), LUA_GCCOLLECT);
            EQUALS(alive.expired(), false);
        }
        EQUALS(alive.expired(), true);

        BindCallTest::HeldSlot()     = std::make_shared<int>(8);
        const std::weak_ptr<int> lua = BindCallTest::HeldSlot();
        {
            Framework::Scripting::LuaRuntime runtime(BindCallTest::LuaOptions(Framework::Scripting::Bind::Side::Server));
            BindCallTest::ProbeDefinition().RegisterLuaGlobal(runtime.State(), Framework::Scripting::Bind::Side::Server);
            EQUALS(LuaRuntimeTest::Eval(runtime, "(function() kept = Probe.hold() return kept() end)()"), "8");
            BindCallTest::HeldSlot().reset();
            EQUALS(lua.expired(), false);
            LuaRuntimeTest::Run(runtime, "kept = nil");
            lua_gc(runtime.State(), LUA_GCCOLLECT);
            EQUALS(lua.expired(), true);
        }
    });

    IT("makes a function of a native one outside any call", {
        BindCallTest::Runtimes runtimes;
        runtimes.Enter([](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            const v8::Local<v8::Function> function = Framework::Scripting::Bind::MakeV8Function(isolate, context, [](Framework::Scripting::Bind::Call &call) {
                call.Return(call.Arg(0).AsString() + "!");
            });
            context->Global()->Set(context, v8pp::to_v8(isolate, "shout"), function).Check();
        });
        lua_State *L = runtimes.LuaSide().State();
        Framework::Scripting::Bind::PushLuaFunction(L, [](Framework::Scripting::Bind::Call &call) {
            call.Return(call.Arg(0).AsString() + "!");
        });
        lua_setglobal(L, "shout");
        EQUALS(runtimes.Both("shout('hey')", "hey!"), "");

        // A state no LuaRuntime made runs one too, as long as the function asks for nothing of the runtime.
        LuaTest::State plain;
        Framework::Scripting::Bind::PushLuaFunction(plain.L, [](Framework::Scripting::Bind::Call &call) {
            call.Return(call.ArgCount());
        });
        lua_setglobal(plain.L, "count");
        EQUALS(LuaTest::Eval(plain.L, "count(1, 2, 3)"), "3");
    });

    // --- Errors ---

    IT("throws when the function returns, and the first throw wins", {
        BindCallTest::Runtimes runtimes;
        BindCallTest::reachedEnd = 0;

        EQUALS(runtimes.Js("Probe.failType()"), "<threw> TypeError: Probe.failType: expected (text)");
        EQUALS(runtimes.Js("Probe.fail()"), "<threw> Error: Probe.fail: network peer unavailable");
        EQUALS(runtimes.Js("{ let caught; try { Probe.failType() } catch (error) { caught = error } caught instanceof TypeError }"), "true");
        EQUALS(runtimes.Js("{ let caught; try { Probe.fail() } catch (error) { caught = error } caught instanceof Error && !(caught instanceof TypeError) }"), "true");
        EQUALS(BindCallTest::reachedEnd, 4);

        // A number out of range is a RangeError in JavaScript, and the raised string every error is in Lua.
        EQUALS(runtimes.Js("Probe.failRange()"), "<threw> RangeError: Probe.failRange: expected a tier from 0 to 2");
        EQUALS(runtimes.Js("{ let caught; try { Probe.failRange() } catch (error) { caught = error } caught instanceof RangeError }"), "true");
        EQUALS(runtimes.Lua("Probe.failRange()"), "<error> eval:1: Probe.failRange: expected a tier from 0 to 2");

        EQUALS(runtimes.Lua("Probe.failType()"), "<error> eval:1: Probe.failType: expected (text)");
        EQUALS(runtimes.Lua("Probe.fail()"), "<error> eval:1: Probe.fail: network peer unavailable");
        EQUALS(runtimes.Lua("(function() local ok, message = pcall(function() Probe.fail() end) return tostring(ok) .. ' ' .. type(message) .. ' ' .. message end)()"), "false string eval:1: Probe.fail: network peer unavailable");
        EQUALS(runtimes.Lua("select(2, pcall(Probe.failType))"), "Probe.failType: expected (text)");
        EQUALS(BindCallTest::reachedEnd, 8);

        // The runtimes are as good as before.
        EQUALS(runtimes.Both("Probe.twice(4)", "8"), "");
    });

    IT("turns a C++ exception into an error of the script", {
        BindCallTest::Runtimes runtimes;
        EQUALS(runtimes.Js("Probe.raise()"), "<threw> Error: Probe.raise: exception");
        EQUALS(runtimes.Lua("Probe.raise()"), "<error> eval:1: Probe.raise: exception");
        EQUALS(runtimes.Both("Probe.twice(4)", "8"), "");

        // A typed function keeps the checks of its language.
        EQUALS(runtimes.Lua("Probe.twice('a')"), "<error> eval:1: bad argument #1 to 'Probe.twice' (integer expected, got string)");
        EQUALS(runtimes.Lua("Probe.twice()"), "<error> eval:1: wrong number of arguments to 'Probe.twice' (1..1 expected, got 0)");
        EQUALS(runtimes.Js("Probe.twice()").starts_with("<threw> Error: Argument count does not match function definition"), true);
    });

    IT("lets what Lua raises inside the function unwind it", {
        Framework::Scripting::LuaRuntime runtime(LuaRuntimeTest::Options(4u << 20));
        Framework::Scripting::Bind::PushLuaFunction(runtime.State(), [](Framework::Scripting::Bind::Call &call) {
            const BindCallTest::Unwinding unwinding;
            const std::string block(64 * 1024, 'x');
            Framework::Scripting::Bind::Value list = call.NewArray();
            for (int i = 0; i < 1024; ++i) {
                list.Append(call.String(block));
            }
            ++BindCallTest::reachedEnd;
        });
        lua_setglobal(runtime.State(), "flood");
        BindCallTest::unwound    = 0;
        BindCallTest::reachedEnd = 0;

        EQUALS(LuaRuntimeTest::Eval(runtime, "flood()"), "<error> not enough memory");
        EQUALS(BindCallTest::unwound, 1);
        EQUALS(BindCallTest::reachedEnd, 0);
        EQUALS(LuaRuntimeTest::Eval(runtime, "1 + 1"), "2");
    });

    IT("goes on when the deadline stops what it asked a script for, and the script is stopped after it", {
        Framework::Scripting::LuaRuntime runtime(LuaRuntimeTest::Options(0, 50));
        LuaRuntimeTest::AddSpin(runtime);
        BindCallTest::ProbeDefinition().RegisterLuaGlobal(runtime.State(), Framework::Scripting::Bind::Side::Server);
        BindCallTest::reachedEnd = 0;

        const std::string error = LuaRuntimeTest::Run(runtime, "Probe.display(setmetatable({}, { __tostring = function() spin() return 'late' end })) survived = true");
        EQUALS(LuaRuntimeTest::Contains(LuaRuntimeTest::FirstLine(error), "script ran too long"), true);
        EQUALS(BindCallTest::reachedEnd, 1);
        EQUALS(LuaRuntimeTest::Eval(runtime, "survived"), "nil");
    });

    IT("lets nothing more run in an isolate that is being terminated", {
        BindCallTest::Runtimes runtimes;
        runtimes.Enter([](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            const v8::Local<v8::Function> stop = Framework::Scripting::Bind::MakeV8Function(isolate, context, [isolate](Framework::Scripting::Bind::Call &) {
                isolate->TerminateExecution();
            });
            context->Global()->Set(context, v8pp::to_v8(isolate, "stop"), stop).Check();
        });
        BindCallTest::reachedEnd = 0;

        // What a script is terminated with is no exception of its own: V8 has it as null.
        EQUALS(runtimes.Js("Probe.get({ get a() { stop(); for (;;) {} } }, 'a'); globalThis.survived = true"), "<threw> null");
        EQUALS(BindCallTest::reachedEnd, 1);
        runtimes.Enter([](v8::Isolate *isolate, v8::Local<v8::Context>) {
            isolate->CancelTerminateExecution();
        });
        EQUALS(runtimes.Js("typeof survived"), "undefined");
        EQUALS(runtimes.Both("Probe.twice(4)", "8"), "");

        // Terminated while it spells a value, the function is told as well, and asks the script for nothing more.
        BindCallTest::reachedEnd         = 0;
        BindCallTest::thrownAfterDisplay = false;
        EQUALS(runtimes.Js("Probe.spell({ toString() { stop(); for (;;) {} } }, { get a() { globalThis.asked = true; return 1 } })"), "<threw> null");
        EQUALS(BindCallTest::reachedEnd, 1);
        EQUALS(BindCallTest::thrownAfterDisplay, true);
        runtimes.Enter([](v8::Isolate *isolate, v8::Local<v8::Context>) {
            isolate->CancelTerminateExecution();
        });
        EQUALS(runtimes.Js("typeof asked"), "undefined");
        EQUALS(runtimes.Both("Probe.twice(4)", "8"), "");
    });

    // --- Bound classes ---

    IT("gives a method written as a context function its receiver", {
        BindCallTest::Runtimes runtimes;
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

        // Without a receiver of the class a JavaScript method finds none, and here returns without effect.
        EQUALS(runtimes.Js("new CallCounter(1).bump.call({}, 5)"), "undefined");
        EQUALS(runtimes.Js("{ const bump = new CallCounter(1).bump; bump(5) }"), "undefined");
        EQUALS(runtimes.Js("new CallCounter(1).bump.call(new Vector3(1, 2, 3), 5)"), "undefined");
        // A Lua one is not called at all: it raises, as a typed method called with `.` does.
        EQUALS(runtimes.Lua("CallCounter.new(1).bump(5)"), "<error> eval:1: bad receiver for 'CallCounter:bump' (CallCounter expected, got number)");
        EQUALS(runtimes.Lua("CallCounter.new(1).bump()"), "<error> eval:1: bad receiver for 'CallCounter:bump' (CallCounter expected, got no value)");
        EQUALS(runtimes.Lua("CallCounter.new(1).bump(Vector3.new(1, 2, 3), 5)"), "<error> eval:1: bad receiver for 'CallCounter:bump' (CallCounter expected, got Vector3)");
        EQUALS(runtimes.Lua("(function() local c = CallCounter.new(1) pcall(c.bump, 5) return c:get() end)()"), "1");
        // A method a base declares takes an object of a class derived from it, called either way.
        EQUALS(runtimes.Lua("(function() local s = CallStepper.new(3) CallCounter.new(1).bump(s, 4) return s:get() end)()"), "7");

        // The receiver is where Lua put it, and a value the function makes is another.
        EQUALS(runtimes.Both("{ const c = new CallCounter(1); c.late() + ' ' + c.get() }", "(function() local c = CallCounter.new(1) return c:late() .. ' ' .. c:get() end)()", "receiver:Bound:Bound 2"), "");
        EQUALS(runtimes.Lua("CallCounter.new(1).late()"), "<error> eval:1: bad receiver for 'CallCounter:late' (CallCounter expected, got no value)");
    });

    IT("reads and writes a property written as context functions", {
        BindCallTest::Runtimes runtimes;
        std::string mismatches;
        mismatches += runtimes.Both("new CallCounter(4).level", "CallCounter.new(4).level", "4");
        mismatches += runtimes.Both("{ const c = new CallCounter(1); c.level = 9; c.get() }", "(function() local c = CallCounter.new(1) c.level = 9 return c:get() end)()", "9");
        // The setter takes what no one signature says.
        mismatches += runtimes.Both("{ const c = new CallCounter(1); c.level = 'four'; c.level }", "(function() local c = CallCounter.new(1) c.level = 'four' return c.level end)()", "4");
        // A class derived from the receiver's has the property.
        mismatches += runtimes.Both("{ const s = new CallStepper(1); s.level = 6; s.level + s.doubled }", "(function() local s = CallStepper.new(1) s.level = 6 return s.level + s.doubled end)()", "18");
        EQUALS(mismatches, "");

        EQUALS(runtimes.Js("try { new CallCounter(1).level = true } catch (e) { e.name + ': ' + e.message }"), "TypeError: CallCounter.level: expected a number or a string");
        EQUALS(runtimes.Lua("(function() local c = CallCounter.new(1) c.level = true end)()"), "<error> eval:1: CallCounter.level: expected a number or a string");

        // Without a setter it is read-only, as a typed one is.
        EQUALS(runtimes.Js("{ const c = new CallCounter(2); c.doubled = 50; c.doubled }"), "4");
        EQUALS(runtimes.Lua("(function() local c = CallCounter.new(2) c.doubled = 50 end)()"), "<error> eval:1: attempt to set 'doubled' on CallCounter");

        const Framework::Scripting::Bind::ClassDesc &desc = BindCallTest::CounterDefinition().Describe();
        EQUALS(desc.properties.size(), 2u);
        EQUALS(desc.properties[0].name + ":" + Framework::Scripting::Bind::ToTypeScript(desc.properties[0].type) + ":" + Framework::Scripting::Bind::ToLuaAnnotation(desc.properties[0].type) + ":" + (desc.properties[0].readonly ? "readonly" : "writable"),
            "level:number | string:number|string:writable");
        EQUALS(desc.properties[1].name + ":" + Framework::Scripting::Bind::ToTypeScript(desc.properties[1].type) + ":" + (desc.properties[1].readonly ? "readonly" : "writable"), "doubled:number:readonly");
    });

    IT("leaves a method of another side out", {
        BindCallTest::Runtimes runtimes(Framework::Scripting::Bind::Side::Client);
        EQUALS(runtimes.Js("typeof new CallCounter(1).serverBump"), "undefined");
        EQUALS(runtimes.Lua("type(CallCounter.new(1).serverBump)"), "nil");
        EQUALS(runtimes.Js("typeof new CallCounter(1).doubled + typeof new CallCounter(1).level"), "undefinednumber");
        EQUALS(runtimes.Lua("type(CallCounter.new(1).doubled) .. type(CallCounter.new(1).level)"), "nilnumber");
        EQUALS(runtimes.Both("new CallCounter(1).bump(1)", "CallCounter.new(1):bump(1)", "1"), "");
    });

    IT("finds the object in a bound value, through its bases", {
        BindCallTest::Runtimes runtimes;
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
        BindCallTest::Runtimes runtimes;
        BindCallTest::reachedEnd = 0;
        std::string mismatches;
        mismatches += runtimes.Both("Probe.wrap(5).get()", "Probe.wrap(5):get()", "5");
        mismatches += runtimes.Both("Probe.wrap(5) instanceof CallCounter", "Probe.kind(Probe.wrap(5)) == 'Bound'", "true");
        mismatches += runtimes.Both("{ const w = Probe.wrap(5); w.bump(2); w.get() }", "(function() local w = Probe.wrap(5) w:bump(2) return w:get() end)()", "7");
        mismatches += runtimes.Both("{ const a = new CallCounter(1); const b = Probe.copy(a); b.bump(1); a.get() * 10 + b.get() }", "(function() local a = CallCounter.new(1) local b = Probe.copy(a) b:bump(1) return a:get() * 10 + b:get() end)()", "12");
        EQUALS(mismatches, "");
        EQUALS(BindCallTest::reachedEnd, 6);
    });

    IT("throws for a class the runtime does not have", {
        BindCallTest::Runtimes runtimes(Framework::Scripting::Bind::Side::Server, false);
        BindCallTest::reachedEnd = 0;
        EQUALS(runtimes.Js("Probe.wrap(5)"), "<threw> Error: CallCounter is not available in this JavaScript runtime");
        EQUALS(runtimes.Lua("Probe.wrap(5)"), "<error> eval:1: CallCounter is not available in this Lua state");
        EQUALS(BindCallTest::reachedEnd, 2);
        // The object it was to move from is left whole.
        BindCallTest::leftAfterWrap = 0;
        EQUALS(runtimes.Js("Probe.keep()"), "<threw> Error: CallStepper is not available in this JavaScript runtime");
        EQUALS(BindCallTest::leftAfterWrap, 64u);
        BindCallTest::leftAfterWrap = 0;
        EQUALS(runtimes.Lua("Probe.keep()"), "<error> eval:1: CallStepper is not available in this Lua state");
        EQUALS(BindCallTest::leftAfterWrap, 64u);
        EQUALS(runtimes.Both("Probe.bound({})", "-1"), "");
    });

    // --- The call ---

    IT("tells the language, the side and the resource of the caller", {
        {
            BindCallTest::Runtimes runtimes;
            EQUALS(runtimes.Js("Probe.language()"), "javascript");
            EQUALS(runtimes.Lua("Probe.language()"), "lua");
            EQUALS(runtimes.Both("Probe.side()", "server"), "");

            // Lua has the name its runtime was created with; JavaScript asks the resource manager.
            EQUALS(runtimes.Lua("Probe.resource()"), "lua-probe");
            EQUALS(runtimes.Both("Probe.manager()", BindCallTest::UnregisteredManager()), "");

            Framework::Scripting::ResourceManager manager(&runtimes.Engine());
            BindCallTest::expectedManager = &manager;
            Framework::Scripting::Bind::SetCallResourceManager(runtimes.Isolate(), &manager);
            Framework::Scripting::Bind::SetCallResourceManager(&runtimes.LuaSide(), &manager);
            EQUALS(runtimes.Both("Probe.manager()", "registered"), "");
            EQUALS(runtimes.Js("Probe.resource()"), "");
            manager.SetCurrentResourceContext("js-probe");
            EQUALS(runtimes.Js("Probe.resource()"), "js-probe");
            // The owner of a function is asked of the manager too, with the function; what is no function
            // has the owner the call has. A Lua state runs one resource.
            EQUALS(runtimes.Js("Probe.ownerOf(() => 1) + '|' + Probe.ownerOf(5) + '|' + Probe.ownerOf()"), "js-probe|js-probe|js-probe");
            manager.SetCurrentResourceContext("");
            EQUALS(runtimes.Js("Probe.ownerOf(() => 1)"), "");
            EQUALS(runtimes.Lua("Probe.resource()"), "lua-probe");
            EQUALS(runtimes.Lua("Probe.ownerOf(print) .. '|' .. Probe.ownerOf(5) .. '|' .. Probe.ownerOf()"), "lua-probe|lua-probe|lua-probe");

            Framework::Scripting::Bind::SetCallResourceManager(runtimes.Isolate(), nullptr);
            Framework::Scripting::Bind::SetCallResourceManager(&runtimes.LuaSide(), nullptr);
            EQUALS(runtimes.Both("Probe.manager()", BindCallTest::UnregisteredManager()), "");
        }
        BindCallTest::expectedManager = nullptr;

        BindCallTest::Runtimes client(Framework::Scripting::Bind::Side::Client);
        EQUALS(client.Both("Probe.side()", "client"), "");
    });

    IT("forgets the manager of a runtime when the manager or the isolate goes", {
        BindCallTest::Runtimes runtimes;
        {
            Framework::Scripting::ResourceManager manager(&runtimes.Engine());
            Framework::Scripting::Bind::SetCallResourceManager(runtimes.Isolate(), &manager);
            Framework::Scripting::Bind::SetCallResourceManager(&runtimes.LuaSide(), &manager);
            BindCallTest::expectedManager = &manager;
            EQUALS(runtimes.Both("Probe.manager()", "registered"), "");
        }
        BindCallTest::expectedManager = nullptr;
        EQUALS(runtimes.Both("Probe.manager()", BindCallTest::UnregisteredManager()), "");

        // An isolate that is unregistered is not found again under its address.
        Framework::Scripting::NodeEngine engine;
        engine.Init();
        std::string error;
        std::unique_ptr<Framework::Scripting::NodeRuntime> runtime = engine.CreateRuntime(error);
        Framework::Scripting::ResourceManager manager(&engine);
        Framework::Scripting::Bind::SetCallResourceManager(runtime->GetIsolate(), &manager);
        const bool registered = Framework::Scripting::Bind::Detail::CallResourceManager(runtime->GetIsolate()) == &manager;
        Framework::Scripting::Builtins::UnregisterAll(runtime->GetIsolate());
        const bool forgotten = Framework::Scripting::Bind::Detail::CallResourceManager(runtime->GetIsolate()) != &manager;
        runtime.reset();
        engine.Shutdown();
        EQUALS(registered, true);
        EQUALS(forgotten, true);
    });

    IT("writes a line to the output of the runtime of the caller", {
        Framework::Scripting::Bind::Namespace output("ProbeOutput", "Writes to the output of its runtime.");
        output
            .Function(
                "write",
                [](Framework::Scripting::Bind::Call &call) {
                    static constexpr Framework::Scripting::OutputLevel levels[] = {Framework::Scripting::OutputLevel::Debug, Framework::Scripting::OutputLevel::Info, Framework::Scripting::OutputLevel::Warning, Framework::Scripting::OutputLevel::Error};
                    call.WriteOutput(levels[call.Arg(0).AsInteger().value_or(1) & 3], call.Arg(1).AsString());
                },
                Framework::Scripting::Bind::Doc("Writes a line.").Param("level", Framework::Scripting::Bind::TypeOf<int>(), "0 for debug to 3 for error.").Param("line", Framework::Scripting::Bind::TypeOf<std::string>(), "What to write."))
            .Function(
                "bounded",
                [](Framework::Scripting::Bind::Call &call) {
                    call.Return(call.OutputLineLimit() == Framework::Scripting::LuaRuntime::kMaxLineBytes ? "as print" : call.OutputLineLimit() == std::numeric_limits<size_t>::max() ? "not" : "otherwise");
                },
                Framework::Scripting::Bind::Doc("Tells the bound of a line.").ReturnType(Framework::Scripting::Bind::TypeOf<std::string>()));

        BindCallTest::Runtimes runtimes;
        runtimes.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            output.MaterialiseV8(isolate, context, context->Global(), v8pp::metadata::catalog("bind_call/scratch"), Framework::Scripting::Bind::Side::Server);
        });
        output.RegisterLuaGlobal(runtimes.LuaSide().State(), Framework::Scripting::Bind::Side::Server);
        Framework::Scripting::ResourceManager manager(&runtimes.Engine());
        Framework::Scripting::Bind::SetCallResourceManager(runtimes.Isolate(), &manager);
        manager.SetCurrentResourceContext("js-probe");

        // The same calls in both languages, each under the name of its resource and at the level it gave.
        LuaRuntimeTest::LogCapture log(spdlog::level::debug);
        const std::string calls = "ProbeOutput.write(0, 'traced'); ProbeOutput.write(1, 'told'); ProbeOutput.write(2, 'warned'); ProbeOutput.write(3, 'failed')";
        runtimes.Js(calls);
        EQUALS(LuaRuntimeTest::Run(runtimes.LuaSide(), calls), "");
        // JavaScript writes the line alone when it cannot tell the resource, and all of a long one.
        manager.SetCurrentResourceContext("");
        runtimes.Js("ProbeOutput.write(1, 'unnamed'); ProbeOutput.write(1, 'j'.repeat(5000))");
        // Lua writes as print does: a long line is cut and a control character blanked.
        EQUALS(LuaRuntimeTest::Run(runtimes.LuaSide(), "ProbeOutput.write(1, ('l'):rep(5000)) ProbeOutput.write(2, 'two\\nlines')"), "");

        const std::vector<std::string> lines = log.LeveledLines();
        std::string missing;
        for (const std::string &expected : {std::string("debug [js-probe] traced"), std::string("info [js-probe] told"), std::string("warning [js-probe] warned"), std::string("error [js-probe] failed"), std::string("debug [lua-probe] traced"),
                 std::string("info [lua-probe] told"), std::string("warning [lua-probe] warned"), std::string("error [lua-probe] failed"), std::string("info unnamed"), "info " + std::string(5000, 'j'),
                 "info [lua-probe] " + std::string(Framework::Scripting::LuaRuntime::kMaxLineBytes, 'l') + std::string(LuaRuntimeTest::kTruncated), std::string("warning [lua-probe] two lines")}) {
            if (std::count(lines.begin(), lines.end(), expected) != 1) {
                missing += "\n\t" + expected.substr(0, 60);
            }
        }
        EQUALS(missing, "");
        EQUALS(runtimes.Js("ProbeOutput.bounded()"), "not");
        EQUALS(runtimes.Lua("ProbeOutput.bounded()"), "as print");

        // A print handler of the Lua runtime gets the line, and the log does not.
        LuaRuntimeTest::Printed printed(runtimes.LuaSide());
        EQUALS(LuaRuntimeTest::Run(runtimes.LuaSide(), "ProbeOutput.write(3, 'handled')"), "");
        EQUALS(printed.lines->size(), 1u);
        EQUALS((*printed.lines)[0], "handled");
        EQUALS(LuaRuntimeTest::CountStartingWith(log.LeveledLines(), "error [lua-probe] handled"), 0u);
    });

    // --- Namespace, materialised ---

    IT("publishes constants, and the members of the side only", {
        BindCallTest::Runtimes server;
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

        BindCallTest::Runtimes client(Framework::Scripting::Bind::Side::Client);
        EQUALS(client.Both("Probe.clientOnly()", "true"), "");
        EQUALS(client.Js("typeof Probe.serverOnly + typeof Probe.serverLabel"), "undefinedundefined");
        EQUALS(client.Lua("type(Probe.serverOnly) .. type(Probe.serverLabel)"), "nilnil");
    });

    IT("makes the namespace read-only", {
        BindCallTest::Runtimes runtimes;
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
        Framework::Scripting::LuaRuntime runtime(BindCallTest::LuaOptions(Framework::Scripting::Bind::Side::Server));
        lua_State *L = runtime.State();
        BindCallTest::ProbeDefinition().RegisterLuaGlobal(L, Framework::Scripting::Bind::Side::Server);
        LuaRuntimeTest::Run(runtime, "first = Probe; rawset(_G, 'Probe', nil)");
        EQUALS(LuaRuntimeTest::Eval(runtime, "type(Probe)"), "nil");
        BindCallTest::ProbeDefinition().MaterialiseLua(L, Framework::Scripting::Bind::Side::Client);
        EQUALS(LuaRuntimeTest::Eval(runtime, "rawequal(first, Probe)"), "true");
        EQUALS(LuaRuntimeTest::Eval(runtime, "type(Probe.serverOnly) .. type(Probe.clientOnly)"), "functionnil");
        EQUALS(lua_gettop(L), 0);
    });

    IT("takes the value of a runtime constant from each materialisation", {
        Framework::Scripting::Bind::Namespace told("ProbeTold", "Says what its runtime was told.");
        told.RuntimeConstant<bool>("flag", "A switch of the runtime.")
            .RuntimeConstant<double>("level", "A level of the runtime.")
            .Constant("fixed", 7, "The same in every runtime.")
            .RuntimeConstant<std::string>("label", "A label of the runtime.")
            .RuntimeConstant<std::string>("serverLabel", "A label of a server.", Framework::Scripting::Bind::Side::Server);

        // A client is given no value for what only a server has.
        const Framework::Scripting::Bind::Namespace::RuntimeConstants first = {{"flag", true}, {"level", 3.0}, {"label", std::string("first")}};
        BindCallTest::Runtimes client(Framework::Scripting::Bind::Side::Client);
        v8pp::metadata::registry recorded;
        client.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            told.MaterialiseV8(isolate, context, context->Global(), recorded, Framework::Scripting::Bind::Side::Client, first);
        });
        told.RegisterLuaGlobal(client.LuaSide().State(), Framework::Scripting::Bind::Side::Client, first);
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
        for (const nlohmann::json &property : BindCallTest::SymbolOf(recorded, "ProbeTold")["properties"]) {
            if (property["name"] == "label") {
                label = property;
            }
        }
        EQUALS(label.dump(), expected.dump());

        // Another runtime is told something else.
        const Framework::Scripting::Bind::Namespace::RuntimeConstants second = {{"serverLabel", std::string("behind")}, {"label", std::string("second")}, {"level", 0.5}, {"flag", false}};
        BindCallTest::Runtimes server;
        server.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            told.MaterialiseV8(isolate, context, context->Global(), recorded, Framework::Scripting::Bind::Side::Server, second);
        });
        told.RegisterLuaGlobal(server.LuaSide().State(), Framework::Scripting::Bind::Side::Server, second);
        mismatches += server.Both("ProbeTold.flag === false", "ProbeTold.flag == false", "true");
        mismatches += server.Both("ProbeTold.level", "0.5");
        mismatches += server.Both("ProbeTold.label", "second");
        mismatches += server.Both("ProbeTold.serverLabel", "behind");
        EQUALS(mismatches, "");
        EQUALS(client.Both("ProbeTold.label", "first"), "");

        // A state that has the namespace keeps what it was first told.
        told.MaterialiseLua(client.LuaSide().State(), Framework::Scripting::Bind::Side::Client, second);
        EQUALS(client.Lua("ProbeTold.label"), "first");
    });

    IT("refuses a runtime constant that is given no value, or one of another type", {
        Framework::Scripting::Bind::Namespace told("ProbeUntold", "Says what its runtime was told.");
        told.RuntimeConstant<bool>("flag", "A switch of the runtime.").RuntimeConstant<std::string>("label", "A label of the runtime.").RuntimeConstant<double>("serverLevel", "A level of a server.", Framework::Scripting::Bind::Side::Server);
        const std::string refusedTwice = BindCallTest::LogicErrorOf([&] {
            told.RuntimeConstant<bool>("label", "Declared before.");
        });
        EQUALS(refusedTwice, "Bind: 'ProbeUntold.label' is declared twice, and a member cannot be overloaded");

        const Framework::Scripting::Bind::Namespace::RuntimeConstants missing  = {{"flag", true}};
        const Framework::Scripting::Bind::Namespace::RuntimeConstants mistyped = {{"flag", true}, {"label", 1.0}};
        const Framework::Scripting::Bind::Namespace::RuntimeConstants whole    = {{"flag", true}, {"label", std::string("whole")}};
        const std::string noValue                        = "Bind: 'ProbeUntold.label' is a runtime constant, and the runtime gives it no value";
        const std::string otherType                      = "Bind: 'ProbeUntold.label' is a runtime constant, and the runtime gives it a value of another type";

        BindCallTest::Runtimes runtimes(Framework::Scripting::Bind::Side::Client);
        std::string refusedMissing;
        std::string refusedMistyped;
        std::string refusedOtherSide;
        runtimes.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            v8pp::metadata::registry &scratch = v8pp::metadata::catalog("bind_call/scratch");
            refusedMissing                    = BindCallTest::LogicErrorOf([&] {
                told.MaterialiseV8(isolate, context, context->Global(), scratch, Framework::Scripting::Bind::Side::Client, missing);
            });
            refusedMistyped                   = BindCallTest::LogicErrorOf([&] {
                told.MaterialiseV8(isolate, context, context->Global(), scratch, Framework::Scripting::Bind::Side::Client, mistyped);
            });
            // What only the other side has needs no value here, and needs one there.
            refusedOtherSide = BindCallTest::LogicErrorOf([&] {
                told.MaterialiseV8(isolate, context, context->Global(), scratch, Framework::Scripting::Bind::Side::Server, whole);
            });
        });
        EQUALS(refusedMissing, noValue);
        EQUALS(refusedMistyped, otherType);
        EQUALS(refusedOtherSide, "Bind: 'ProbeUntold.serverLevel' is a runtime constant, and the runtime gives it no value");
        EQUALS(runtimes.Js("typeof ProbeUntold"), "undefined");

        lua_State *L = runtimes.LuaSide().State();
        refusedMissing  = BindCallTest::LogicErrorOf([&] {
            told.MaterialiseLua(L, Framework::Scripting::Bind::Side::Client, missing);
        });
        refusedMistyped = BindCallTest::LogicErrorOf([&] {
            told.MaterialiseLua(L, Framework::Scripting::Bind::Side::Client, mistyped);
        });
        EQUALS(refusedMissing, noValue);
        EQUALS(refusedMistyped, otherType);
        EQUALS(lua_gettop(L), 0);
        EQUALS(runtimes.Lua("type(ProbeUntold)"), "nil");

        told.MaterialiseLua(L, Framework::Scripting::Bind::Side::Client, whole);
        EQUALS(runtimes.Lua("ProbeUntold.label"), "whole");
    });

    IT("refuses to give JavaScript a function it cannot receive the result of", {
        Framework::Scripting::Bind::Namespace sealing("ProbeSealing", "Returns what cannot be copied.");
        sealing.Function(
            "shared",
            +[]() -> BindClassLuaTest::Token & {
                static BindClassLuaTest::Token token(1);
                return token;
            },
            Framework::Scripting::Bind::Doc("Returns the one token."), Framework::Scripting::Bind::Side::Server);

        BindCallTest::Runtimes runtimes(Framework::Scripting::Bind::Side::Client);
        std::string refusal;
        runtimes.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            // Not on the side it is declared for, so not refused.
            sealing.MaterialiseV8(isolate, context, context->Global(), v8pp::metadata::catalog("bind_call/scratch"), Framework::Scripting::Bind::Side::Client);
            refusal = BindCallTest::LogicErrorOf([&] {
                sealing.MaterialiseV8(isolate, context, context->Global(), v8pp::metadata::catalog("bind_call/scratch"), Framework::Scripting::Bind::Side::Server);
            });
        });
        EQUALS(refusal, "Bind: 'ProbeSealing.shared' returns an object of a class that cannot be copied, which JavaScript cannot receive");
        EQUALS(runtimes.Js("typeof ProbeSealing.shared"), "undefined");
    });

    IT("returns the object it sets on the target", {
        BindCallTest::Runtimes runtimes;
        bool same = false;
        runtimes.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            v8::Local<v8::Object> target = v8::Object::New(isolate);
            v8::Local<v8::Object> made   = BindCallTest::ProbeDefinition().MaterialiseV8(isolate, context, target, v8pp::metadata::catalog("bind_call/scratch"), Framework::Scripting::Bind::Side::Server);
            v8::Local<v8::Value> set;
            same = target->Get(context, v8pp::to_v8(isolate, "Probe")).ToLocal(&set) && set->StrictEquals(made);
        });
        EQUALS(same, true);
    });

    IT("records the namespace in the catalog as a hand-written builtin does", {
        Framework::Scripting::Bind::Namespace written("ProbeWritten", "Server-side delivery of structured chat messages to connected clients.");
        written
            .Function(
                "sendToAll", [](Framework::Scripting::Bind::Call &) {},
                Framework::Scripting::Bind::Doc("Broadcasts a structured chat message to all clients.")
                    .Param("text", Framework::Scripting::Bind::TypeOf<std::string>(), "Message body broadcast to every connected client.")
                    .OptionalParam("options", Framework::Scripting::Bind::Type::Custom("{ author?: string; color?: number | Color }", "table"), "Optional author label and color: a Color or a packed 0xRRGGBBAA integer."))
            .Function(
                "log", [](Framework::Scripting::Bind::Call &) {},
                Framework::Scripting::Bind::Doc("Writes an informational log entry prefixed with the current resource name.").RestParam("values", Framework::Scripting::Bind::Type::Custom("unknown[]", "any"), "Values formatted and joined with spaces."))
            .Function(
                "emit", [](Framework::Scripting::Bind::Call &) {},
                Framework::Scripting::Bind::Doc("Invokes every shared handler and waits for all synchronous and asynchronous results.")
                    .Param("eventName", Framework::Scripting::Bind::TypeOf<std::string>(), "Shared event name.")
                    .RestParam("args", Framework::Scripting::Bind::Type::Custom("unknown[]", "any"), "Arguments delivered to every matching handler.")
                    .ReturnType(Framework::Scripting::Bind::Type::Custom("Promise<void>", "nil"))
                    .Returns("Promise rejected with an AggregateError when one or more handlers fail."))
            .Function(
                "count",
                [](const std::string &eventName) {
                    return static_cast<int>(eventName.size());
                },
                Framework::Scripting::Bind::Doc("Counts persistent and one-shot shared handlers across resources.").Param("eventName", "Shared event name to inspect.").Returns("Number of matching handlers."))
            .Function(
                "serverOnly", [](Framework::Scripting::Bind::Call &) {}, Framework::Scripting::Bind::Doc("Exists on the server."), Framework::Scripting::Bind::Side::Server)
            .Constant("isClient", true, "True in the sandboxed client scripting runtime.")
            .Constant("frameworkVersion", std::string("1.0"), "Local Framework release version.");

        v8pp::metadata::registry byHand;
        v8pp::metadata::symbol &expected = byHand.global_object("ProbeWritten", "Server-side delivery of structured chat messages to connected clients.");
        expected.record(v8pp::metadata::function_of<v8::FunctionCallback>("sendToAll", v8pp::metadata::docs("void",
                                                                                           {
                                                                                               v8pp::metadata::param("text", "string", false, "Message body broadcast to every connected client."),
                                                                                               v8pp::metadata::param("options", "{ author?: string; color?: number | Color }", true, "Optional author label and color: a Color or a packed 0xRRGGBBAA integer."),
                                                                                           },
                                                                                           "Broadcasts a structured chat message to all clients.")));
        expected.record(
            v8pp::metadata::function_of<v8::FunctionCallback>("log", v8pp::metadata::docs("void", {v8pp::metadata::rest_param("values", "unknown[]", "Values formatted and joined with spaces.")}, "Writes an informational log entry prefixed with the current resource name.")));
        expected.record(v8pp::metadata::function_of<v8::FunctionCallback>("emit",
            v8pp::metadata::docs("Promise<void>", {v8pp::metadata::param("eventName", "string", false, "Shared event name."), v8pp::metadata::rest_param("args", "unknown[]", "Arguments delivered to every matching handler.")},
                "Invokes every shared handler and waits for all synchronous and asynchronous results.", "Promise rejected with an AggregateError when one or more handlers fail.")));
        expected.record(v8pp::metadata::function_of<v8::FunctionCallback>("count",
            v8pp::metadata::docs("number", {v8pp::metadata::param("eventName", "string", false, "Shared event name to inspect.")}, "Counts persistent and one-shot shared handlers across resources.", "Number of matching handlers.")));
        expected.add_property("isClient", "boolean", "True in the sandboxed client scripting runtime.", true);
        expected.add_property("frameworkVersion", "string", "Local Framework release version.", true);

        BindCallTest::Runtimes runtimes(Framework::Scripting::Bind::Side::Client);
        v8pp::metadata::registry recorded;
        runtimes.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            written.MaterialiseV8(isolate, context, context->Global(), recorded, Framework::Scripting::Bind::Side::Client);
        });
        EQUALS(BindCallTest::SymbolOf(recorded, "ProbeWritten").dump(), BindCallTest::SymbolOf(byHand, "ProbeWritten").dump());
        EQUALS(runtimes.Js("typeof ProbeWritten.sendToAll + typeof ProbeWritten.count + typeof ProbeWritten.serverOnly"), "functionfunctionundefined");
        EQUALS(runtimes.Js("ProbeWritten.count('four')"), "4");
        EQUALS(runtimes.Js("ProbeWritten.log.name"), "");
    });

    IT("records the functions and the parameters of Probe as a global object", {
        BindCallTest::Runtimes runtimes;
        const nlohmann::json probe = BindCallTest::SymbolOf(runtimes.Catalog(), "Probe");
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
        const nlohmann::json counter = BindCallTest::SymbolOf(runtimes.Catalog(), "CallCounter");
        const nlohmann::json bump    = named(counter["functions"], "bump");
        EQUALS(bump["static"], false);
        EQUALS(bump["parameters"][0]["name"], "amount");
        EQUALS(bump["parameters"][0]["type"]["optional"], true);
        EQUALS(bump["returns"]["type"]["name"], "number");
        EQUALS(named(counter["functions"], "receiver")["static"], true);
    });
});
