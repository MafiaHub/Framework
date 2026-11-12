/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "bind_class_lua_ut.h" // Counter, SubCounter and the other classes bound for Lua
#include "node_test_helpers.h"

#include "scripting/bind/class.h"
#include "scripting/builtins/builtins.h"
#include "scripting/builtins/vector3.h"
#include "scripting/class_cache.h"
#include "scripting/node_engine.h"
#include "scripting/scripting_catalog.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace BindClassV8Test {

    class Gauge {
      public:
        Gauge() = default;
        explicit Gauge(int level): _level(level) {}

        Gauge &raise(int amount) {
            _level += amount;
            return *this;
        }

        int level() const noexcept {
            return _level;
        }

        void setLevel(int level) {
            _level = level;
        }

        bool full() const {
            return _level >= limit();
        }

        std::string label(const std::string &prefix) const {
            return prefix + std::to_string(_level);
        }

        static Gauge of(int level) {
            return Gauge(level);
        }

        static int limit() noexcept {
            return 100;
        }

      private:
        int _level = 0;
    };

    inline void Drain(Gauge &gauge) {
        gauge.setLevel(0);
    }

    class Dial final: public Gauge {
      public:
        explicit Dial(int level): Gauge(level) {}

        int turns() const {
            return level() / 10;
        }
    };

    // Takes an argument a script may omit.
    class Span final {
      public:
        explicit Span(int from, std::optional<int> to = std::nullopt): _from(from), _to(to.value_or(from)) {}

        int length() const {
            return _to - _from;
        }

      private:
        int _from = 0;
        int _to   = 0;
    };

    // Takes and returns glm values, which a script sees as the builtin vector classes.
    class Compass final {
      public:
        glm::vec3 shift(const glm::vec3 &by) const {
            return _heading + by;
        }

        glm::vec3 heading() const {
            return _heading;
        }

        void setHeading(glm::vec3 heading) {
            _heading = heading;
        }

        static glm::vec3 north() {
            return {0.0f, 1.0f, 0.0f};
        }

      private:
        glm::vec3 _heading {0.0f};
    };

    struct Bare final {};

    struct Lenient final {
        explicit Lenient(int) {}
    };
} // namespace BindClassV8Test

FW_SCRIPT_CLASS(BindClassV8Test::Gauge, "Gauge");
FW_SCRIPT_CLASS(BindClassV8Test::Dial, "Dial");
FW_SCRIPT_CLASS(BindClassV8Test::Span, "Span");
FW_SCRIPT_CLASS(BindClassV8Test::Compass, "Compass");
FW_SCRIPT_CLASS(BindClassV8Test::Bare, "Bare");
FW_SCRIPT_CLASS(BindClassV8Test::Lenient, "Lenient");

namespace BindClassV8Test {

    inline int gaugeHookRuns = 0;

    // Anything that is not a number counts as zero, where the declared constructor refuses it.
    inline Gauge *MakeGauge(const v8::FunctionCallbackInfo<v8::Value> &info) {
        const double level = info.Length() > 0 && info[0]->IsNumber() ? info[0]->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0) : 0.0;
        return new Gauge(static_cast<int>(level));
    }

    inline void GaugeToJson(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        const Gauge *self    = v8pp::class_<Gauge>::unwrap_object(isolate, info.This());
        if (self == nullptr) {
            return;
        }
        v8::Local<v8::Object> object = v8::Object::New(isolate);
        object->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "level"), v8::Number::New(isolate, self->level())).Check();
        info.GetReturnValue().Set(object);
    }

    inline v8pp::metadata::function_options GaugeToJsonDocs() {
        return v8pp::metadata::docs("{ level: number }", {}, "Converts this gauge to a plain object for JSON.stringify.", "Object holding the level.");
    }

    // The side of the runtime an isolate belongs to, as a builtin reads it.
    inline Framework::Scripting::Bind::Side SideOf(v8::Isolate *isolate) {
        return Framework::Scripting::IsClientScripting(isolate) ? Framework::Scripting::Bind::Side::Client : Framework::Scripting::Bind::Side::Server;
    }

    template <typename T>
    Framework::Scripting::ClassCache<T> &CacheOf() {
        static Framework::Scripting::ClassCache<T> cache;
        return cache;
    }

    // The isolate's v8pp class of `definition`, built on first use: what the GetClass(isolate) of a builtin does.
    template <typename T>
    v8pp::class_<T> &ClassOf(v8::Isolate *isolate, const Framework::Scripting::Bind::Class<T> &definition) {
        return CacheOf<T>().Get(isolate, [&] {
            return definition.MaterialiseV8(isolate, Framework::Scripting::GetScriptingCatalog(isolate), SideOf(isolate));
        });
    }

    inline const Framework::Scripting::Bind::Class<Gauge> &GaugeDefinition() {
        static const auto definition =
            Framework::Scripting::Bind::Class<Gauge>("Gauge", "Shows a level.")
                .Constructor<int>(Framework::Scripting::Bind::Doc("Creates a gauge.").Param("level", "Initial level."))
                .V8Constructor(&MakeGauge)
                .Method("raise", &Gauge::raise, Framework::Scripting::Bind::Doc("Raises the level.").Param("amount", "Levels to add.").Returns("This gauge."))
                .Static("of", &Gauge::of, Framework::Scripting::Bind::Doc("Makes a gauge.").Param("level", "Its level.").Returns("New gauge."))
                .Method("label", &Gauge::label, Framework::Scripting::Bind::Doc("Labels the level.").Param("prefix", "Text put before the level.").Returns("The label.").ReturnType(Framework::Scripting::Bind::Type::Custom("`${string}${number}`", "string")))
                .Method("drain", &Drain, Framework::Scripting::Bind::Doc("Empties the gauge."))
                .Static("limit", &Gauge::limit, Framework::Scripting::Bind::Doc("Reads the highest level.").Returns("The limit."))
                .Property("level", &Gauge::level, &Gauge::setLevel, "Current level.")
                .Property("full", &Gauge::full, "Whether the gauge is at its limit.")
                .LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Len,
                    [](const Gauge &gauge) {
                        return gauge.level();
                    })
                .LuaHook([](lua_State *L, int, int classTableIndex) {
                    lua_pushinteger(L, Gauge::limit());
                    lua_setfield(L, classTableIndex, "LIMIT");
                })
                .V8Hook([](v8pp::class_<Gauge> &cls, v8::Isolate *) {
                    ++gaugeHookRuns;
                    cls.prototype_function("toJSON", &GaugeToJson, GaugeToJsonDocs());
                });
        return definition;
    }

    inline const Framework::Scripting::Bind::Class<Dial> &DialDefinition() {
        GaugeDefinition();
        static const auto definition = Framework::Scripting::Bind::Class<Dial>("Dial", "A gauge that turns.")
                                           .Inherit<Gauge>()
                                           .V8EnsureBase([](v8::Isolate *isolate) {
                                               ClassOf(isolate, GaugeDefinition());
                                           })
                                           .Constructor<int>(Framework::Scripting::Bind::Doc("Creates a dial.").Param("level", "Initial level."))
                                           .Method("turns", &Dial::turns, Framework::Scripting::Bind::Doc("Counts the full turns.").Returns("One turn for ten levels."))
                                           .Method("raiseBy", &Gauge::raise, Framework::Scripting::Bind::Doc("Raises the level.").Param("amount", "Levels to add.").Returns("This dial."))
                                           .Property("reading", &Gauge::level, "Current level.")
                                           .Method(
                                               "fill",
                                               [](Gauge &gauge) -> Gauge & {
                                                   gauge.setLevel(Gauge::limit());
                                                   return gauge;
                                               },
                                               Framework::Scripting::Bind::Doc("Fills the dial.").Returns("This dial."))
                                           .Property(
                                               "half",
                                               [](const Gauge &gauge) {
                                                   return gauge.level() / 2;
                                               },
                                               [](Gauge *gauge, int half) {
                                                   gauge->setLevel(half * 2);
                                               },
                                               "Half the level.");
        return definition;
    }

    inline const Framework::Scripting::Bind::Class<Span> &SpanDefinition() {
        static const auto definition = Framework::Scripting::Bind::Class<Span>("Span", "A range of integers.")
                                           .Constructor<int, std::optional<int>>(Framework::Scripting::Bind::Doc("Creates a span.").Param("from", "First value.").Param("to", "Last value; the first when omitted."))
                                           .Method("length", &Span::length, Framework::Scripting::Bind::Doc("Measures the span.").Returns("Last value minus first."))
                                           .Static(
                                               "lengthOf",
                                               [](const Span *span) {
                                                   return span != nullptr ? span->length() : -1;
                                               },
                                               Framework::Scripting::Bind::Doc("Measures a span.").Param("span", "Span to measure.").Returns("Its length, or -1 without a span."));
        return definition;
    }

    // Compass bound for V8 alone, member by member as Class<T> does. value_types_parity binds glm values through
    // Class<T>, for both languages.
    inline std::unique_ptr<v8pp::class_<Compass>> MaterialiseCompass(v8::Isolate *isolate, v8pp::metadata::registry &catalog) {
        Framework::Scripting::Bind::ClassDesc desc;
        desc.name        = "Compass";
        desc.description = "Points somewhere.";

        Framework::Scripting::Bind::PropertyDesc heading;
        heading.name        = "heading";
        heading.type        = Framework::Scripting::Bind::TypeOf<glm::vec3>();
        heading.description = "Where the compass points.";

        Framework::Scripting::Bind::Detail::V8Class<Compass> definition;
        definition.constructor = Framework::Scripting::Bind::Detail::V8ConstructorOf<Compass>(Framework::Scripting::Bind::Detail::V8FunctionOptions(Framework::Scripting::Bind::DescribeConstructor<>(Framework::Scripting::Bind::Doc("Creates a compass."))));
        definition.members.push_back(Framework::Scripting::Bind::Detail::V8Method<Compass>(Framework::Scripting::Bind::Detail::SignatureOf<decltype(&Compass::shift)>::Type {}, Framework::Scripting::Bind::Side::Shared, desc.name, "shift", &Compass::shift,
            Framework::Scripting::Bind::Detail::V8FunctionOptions(
                Framework::Scripting::Bind::DescribeFunction<decltype(&Compass::shift)>("shift", Framework::Scripting::Bind::Doc("Shifts the heading.").Param("by", "Offset to add.").Returns("The shifted heading."), false, Framework::Scripting::Bind::Side::Shared))));
        definition.members.push_back(Framework::Scripting::Bind::Detail::V8Static<Compass>(Framework::Scripting::Bind::Detail::SignatureOf<decltype(&Compass::north)>::Type {}, Framework::Scripting::Bind::Side::Shared, desc.name, "north", &Compass::north,
            Framework::Scripting::Bind::Detail::V8FunctionOptions(Framework::Scripting::Bind::DescribeFunction<decltype(&Compass::north)>("north", Framework::Scripting::Bind::Doc("Points north.").Returns("The north heading."), true, Framework::Scripting::Bind::Side::Shared))));
        definition.members.push_back(Framework::Scripting::Bind::Detail::V8Property<Compass, glm::vec3>(Framework::Scripting::Bind::Side::Shared, desc.name, "heading", &Compass::heading, &Compass::setHeading, Framework::Scripting::Bind::Detail::V8PropertyOptions(heading)));
        return Framework::Scripting::Bind::Detail::V8MaterialiseClass(definition, desc, isolate, catalog, Framework::Scripting::Bind::Side::Server);
    }

    // Gauge and Dial bound the way a builtin is written by hand, to compare with what their definitions produce.
    inline void HandWrittenGaugeAndDial(v8::Isolate *isolate, v8pp::metadata::registry &catalog, std::vector<std::shared_ptr<void>> &keep) {

        auto gauge = std::make_shared<v8pp::class_<Gauge>>(isolate, catalog, "Gauge", "Shows a level.");
        gauge->auto_wrap_objects(true);
        gauge->ctor<int>(v8pp::metadata::docs("void", {v8pp::metadata::param("level", "number", false, "Initial level.")}, "Creates a gauge."), &MakeGauge)
            .function("raise", &Gauge::raise, v8pp::metadata::docs("this", {v8pp::metadata::param("amount", "number", false, "Levels to add.")}, "Raises the level.", "This gauge."));
        gauge->js_function_template()->Set(isolate, "of", v8pp::wrap_function_template(isolate, &Gauge::of));
        catalog.constructor("Gauge").record(v8pp::metadata::function_of<decltype(&Gauge::of)>("of", v8pp::metadata::docs("Gauge", {v8pp::metadata::param("level", "number", false, "Its level.")}, "Makes a gauge.", "New gauge."), true));
        gauge->function("label", &Gauge::label, v8pp::metadata::docs("`${string}${number}`", {v8pp::metadata::param("prefix", "string", false, "Text put before the level.")}, "Labels the level.", "The label."));
        gauge->prototype_function(
            "drain",
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                if (Gauge *self = v8pp::class_<Gauge>::unwrap_object(info.GetIsolate(), info.This())) {
                    Drain(*self);
                }
            },
            v8pp::metadata::docs("void", {}, "Empties the gauge."));
        gauge->js_function_template()->Set(isolate, "limit", v8pp::wrap_function_template(isolate, static_cast<int (*)()>(&Gauge::limit)));
        catalog.constructor("Gauge").record(v8pp::metadata::function_of<int (*)()>("limit", v8pp::metadata::docs("number", {}, "Reads the highest level.", "The limit."), true));
        gauge->property("level", static_cast<int (Gauge::*)() const>(&Gauge::level), &Gauge::setLevel, v8pp::metadata::property_docs("number", "Current level."));
        gauge->property("full", &Gauge::full, v8pp::metadata::property_options {"Whether the gauge is at its limit.", "boolean", true, false});
        gauge->prototype_function("toJSON", &GaugeToJson, GaugeToJsonDocs());
        keep.push_back(gauge);

        auto dial = std::make_shared<v8pp::class_<Dial>>(isolate, catalog, "Dial", "A gauge that turns.");
        dial->auto_wrap_objects(true);
        dial->inherit<Gauge>();
        dial->ctor<int>(v8pp::metadata::docs("void", {v8pp::metadata::param("level", "number", false, "Initial level.")}, "Creates a dial."));
        dial->function("turns", &Dial::turns, v8pp::metadata::docs("number", {}, "Counts the full turns.", "One turn for ten levels."));
        dial->function("raiseBy", &Gauge::raise, v8pp::metadata::docs("this", {v8pp::metadata::param("amount", "number", false, "Levels to add.")}, "Raises the level.", "This dial."));
        dial->property("reading", static_cast<int (Gauge::*)() const>(&Gauge::level), v8pp::metadata::property_options {"Current level.", "number", true, false});
        dial->prototype_function(
            "fill",
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                if (Dial *self = v8pp::class_<Dial>::unwrap_object(info.GetIsolate(), info.This())) {
                    self->setLevel(Gauge::limit());
                    info.GetReturnValue().Set(info.This());
                }
            },
            v8pp::metadata::docs("this", {}, "Fills the dial.", "This dial."));
        dial->property(
            "half",
            [](const Dial &self) {
                return self.level() / 2;
            },
            [](Dial &self, int half) {
                self.setLevel(half * 2);
            },
            v8pp::metadata::property_docs("number", "Half the level."));
        keep.push_back(dial);
    }

    // The engine the runtimes of one test are created on.
    struct Host final {
        Framework::Scripting::NodeEngine engine;

        Host() {
            engine.Init();
        }

        ~Host() {
            engine.Shutdown();
        }
    };

    // One isolate of side `side`, recording what it binds in a catalog no other runtime writes to.
    class Runtime final {
      public:
        explicit Runtime(Host &host, Framework::Scripting::Bind::Side side = Framework::Scripting::Bind::Side::Server) {
            static int count = 0;
            std::string error;
            _runtime     = host.engine.CreateRuntime(error);
            _catalogName = "bind_class_v8/" + std::to_string(++count);
            Framework::Scripting::SetScriptingCatalog(Isolate(), _catalogName);
            Framework::Scripting::SetScriptingEnvironment(Isolate(), side == Framework::Scripting::Bind::Side::Client);
        }

        ~Runtime() {
            // The caches and the catalog are keyed by isolate address, which a later isolate may reuse.
            Forget<BindClassLuaTest::Counter, BindClassLuaTest::SubCounter, BindClassLuaTest::Tracked, BindClassLuaTest::Token, BindClassLuaTest::Sealed, Gauge, Dial, Span>();
            Framework::Scripting::Builtins::UnregisterAll(Isolate());
            Framework::Scripting::ClearScriptingCatalog(Isolate());
            _runtime.reset();
        }

        v8::Isolate *Isolate() const {
            return _runtime->GetIsolate();
        }

        // Runs `action(isolate, context)` inside the runtime.
        template <typename Action>
        void Enter(Action action) {
            v8::Isolate *isolate = Isolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _runtime->GetContext();
            v8::Context::Scope contextScope(context);
            action(isolate, context);
        }

        // Materialises `definition` for the side of the runtime and publishes it as a global.
        template <typename T>
        void Publish(const Framework::Scripting::Bind::Class<T> &definition) {
            Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                v8pp::class_<T> &cls = ClassOf(isolate, definition);
                context->Global()->Set(context, v8pp::to_v8(isolate, definition.Describe().name), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
            });
        }

        // Counter first: the definition of SubCounter ensures no base, so its caller does.
        void PublishCounters() {
            Publish(BindClassLuaTest::CounterDefinition());
            Publish(BindClassLuaTest::SubCounterDefinition());
        }

        std::string Eval(const std::string &source) {
            return NodeTest::Eval(*_runtime, source);
        }

        v8pp::metadata::registry &Catalog() const {
            return v8pp::metadata::catalog(_catalogName);
        }

        // The symbol `name` of the catalog as it is exported, or null.
        nlohmann::json Symbol(const std::string &name) const {
            return Named(nlohmann::json::parse(v8pp::metadata::to_json(Catalog()))["symbols"], name);
        }

        // The element of `list` named `name`, or null.
        static nlohmann::json Named(const nlohmann::json &list, const std::string &name) {
            for (const nlohmann::json &element : list) {
                if (element["name"] == name) {
                    return element;
                }
            }
            return nullptr;
        }

        // The names of the elements of `list`, in order, separated by spaces.
        static std::string Names(const nlohmann::json &list) {
            std::string names;
            for (const nlohmann::json &element : list) {
                names += (names.empty() ? "" : " ") + element["name"].get<std::string>();
            }
            return names;
        }

      private:
        template <typename... Ts>
        void Forget() {
            (CacheOf<Ts>().Unregister(Isolate()), ...);
        }

        std::unique_ptr<Framework::Scripting::NodeRuntime> _runtime;
        std::string _catalogName;
    };

    // The message of the exception of type `Error` that `action` throws, or empty when it does not throw one.
    template <typename Error, typename Action>
    std::string MessageOf(Action action) {
        try {
            action();
        }
        catch (const Error &error) {
            return error.what();
        }
        return {};
    }
} // namespace BindClassV8Test

MODULE(bind_class_v8, {

    // --- Construction and methods ---

    IT("constructs with new and chains on the receiver", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        EQUALS(runtime.Eval("typeof Counter"), "function");
        EQUALS(runtime.Eval("new Counter(2).add(3).get() === 5"), "true");
        EQUALS(runtime.Eval("new Counter(2) instanceof Counter"), "true");
        EQUALS(runtime.Eval("Counter(2).get()"), "2");
        EQUALS(runtime.Eval("var c = new Counter(1); c.add(1) === c"), "true");
        EQUALS(runtime.Eval("c.get()"), "2");
    });

    IT("puts methods on the prototype and statics on the constructor", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        EQUALS(runtime.Eval("Counter.make(4).get()"), "4");
        EQUALS(runtime.Eval("Counter.make(4) instanceof Counter"), "true");
        EQUALS(runtime.Eval("var c = new Counter(1); typeof Object.getPrototypeOf(c).add"), "function");
        EQUALS(runtime.Eval("Object.getOwnPropertyNames(c).length"), "0");
        EQUALS(runtime.Eval("typeof Counter.add"), "undefined");
        EQUALS(runtime.Eval("typeof c.make"), "undefined");
    });

    IT("reads and writes a property and ignores a write to a read-only one", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        EQUALS(runtime.Eval("var c = new Counter(2); c.value"), "2");
        EQUALS(runtime.Eval("c.value = 7; c.get()"), "7");
        EQUALS(runtime.Eval("c.doubled"), "14");
        EQUALS(runtime.Eval("c.doubled = 1; c.doubled"), "14");
        EQUALS(runtime.Eval("c.get()"), "7");
        EQUALS(runtime.Eval("(function () { 'use strict'; try { c.doubled = 1; } catch (error) { return error instanceof TypeError; } })()"), "true");
        EQUALS(runtime.Eval("c.negated"), "-7");
        EQUALS(runtime.Eval("c.negated = 3; c.value"), "-3");
        EQUALS(runtime.Eval("c.missing"), "undefined");
    });

    IT("leaves string conversion to JavaScript", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();
        runtime.Publish(BindClassLuaTest::TrackedDefinition());

        // A method named toString is one JavaScript itself calls; a class without it gets nothing in its place.
        EQUALS(runtime.Eval("new Counter(2).toString()"), "Counter(2)");
        EQUALS(runtime.Eval("String(new Counter(2))"), "Counter(2)");
        EQUALS(runtime.Eval("String(new Tracked(1))"), "[object Object]");
    });

    IT("turns a C++ exception into a JavaScript error and stays usable", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();
        runtime.Publish(BindClassLuaTest::TrackedDefinition());

        EQUALS(runtime.Eval("new Counter(1).throws()"), "<threw> Error: boom");
        EQUALS(runtime.Eval("new Counter(1).fragile"), "<threw> Error: getter failed");
        EQUALS(runtime.Eval("new Counter(1).fragile = 2"), "<threw> Error: setter failed");
        EQUALS(runtime.Eval("Counter.fail()"), "<threw> Error: static failed");
        EQUALS(runtime.Eval("new Tracked(-1)"), "<threw> Error: Tracked: id must not be negative");
        EQUALS(runtime.Eval("try { new Counter(1).throws(); } catch (error) { error instanceof Error && error.message }"), "boom");
        EQUALS(runtime.Eval("new Counter(1).add(1).get()"), "2");
    });

    IT("reports a wrong argument the way v8pp does", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        EQUALS(runtime.Eval("new Counter(1).add()"), "<threw> Error: Argument count does not match function definition. Expected 1 but got 0");
        EQUALS(runtime.Eval("new Counter(1).add(1, 2)"), "<threw> Error: Argument count does not match function definition. Expected 1 but got 2");
        EQUALS(runtime.Eval("new Counter(1).add('x')"), "<threw> Error: expected Number, typeof=string");
        EQUALS(runtime.Eval("new Counter(1).plus(5)"), "<threw> Error: expected Object, typeof=number");
        EQUALS(runtime.Eval("new Counter()"), "<threw> Error: Argument count does not match function definition. Expected 1 but got 0");
        EQUALS(runtime.Eval("Counter.make('x')"), "<threw> Error: expected Number, typeof=string");
        EQUALS(runtime.Eval("Object.getPrototypeOf(new Counter(1)).add.call({}, 1)"), "<threw> Error: method called on null instance");
    });

    IT("lets a trailing optional argument be omitted", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        EQUALS(runtime.Eval("var c = new Counter(3); c.scaled(2)"), "6");
        EQUALS(runtime.Eval("c.scaled(2, undefined)"), "6");
        EQUALS(runtime.Eval("c.scaled(2, 1)"), "7");
        EQUALS(runtime.Eval("c.scaled()"), "<threw> Error: Argument count does not match function definition. Expected 2 but got 0");
    });

    IT("returns the object a reference or a pointer designates and copies a value", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        EQUALS(runtime.Eval("var c = new Counter(1); var other = new Counter(9); c.view() === c"), "true");
        EQUALS(runtime.Eval("c.self() === c"), "true");
        EQUALS(runtime.Eval("c.pick(other, false) === c"), "true");
        EQUALS(runtime.Eval("c.pick(other, true) === other"), "true");
        EQUALS(runtime.Eval("c.clone() === c"), "false");
        EQUALS(runtime.Eval("c.clone().add(5).get()"), "6");
        EQUALS(runtime.Eval("c.get()"), "1");
    });

    IT("calls a function that takes the receiver first as a method of this", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        EQUALS(runtime.Eval("var c = new Counter(4); c.reset(); c.get()"), "0");
        EQUALS(runtime.Eval("c.addTwice(3) === c"), "true");
        EQUALS(runtime.Eval("c.get()"), "6");
        EQUALS(runtime.Eval("typeof Object.getPrototypeOf(c).reset"), "function");
        EQUALS(runtime.Eval("typeof Counter.reset"), "undefined");
        EQUALS(runtime.Eval("typeof Counter.addTwice"), "undefined");
        EQUALS(runtime.Eval("c.addTwice()"), "<threw> Error: Argument count does not match function definition. Expected 1 but got 0");
        EQUALS(runtime.Eval("c.addTwice(1, 2)"), "<threw> Error: Argument count does not match function definition. Expected 1 but got 2");
        EQUALS(runtime.Eval("c.addTwice('3')"), "<threw> Error: expected Number, typeof=string");
        EQUALS(runtime.Eval("Object.getPrototypeOf(c).reset.call({})"), "<threw> Error: method called on null instance");
        EQUALS(runtime.Eval("Object.getPrototypeOf(c).addTwice.call(5, 1)"), "<threw> Error: method called on null instance");
    });

    IT("converts vectors, optionals and pointers of bound objects", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        EQUALS(runtime.Eval("Counter.sum([new Counter(1), new Counter(2), new SubCounter(4)])"), "7");
        EQUALS(runtime.Eval("Counter.sum([])"), "0");
        EQUALS(runtime.Eval("Counter.maybe()"), "undefined");
        EQUALS(runtime.Eval("Counter.maybe(new Counter(3)).get()"), "3");
        EQUALS(runtime.Eval("var c = new Counter(1); Counter.bump(c); c.get()"), "2");
        EQUALS(runtime.Eval("new Counter(1).plus(new Counter(2))"), "3");
    });

    IT("lets a trailing optional argument of the constructor be omitted, as Lua does", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.Publish(BindClassV8Test::SpanDefinition());

        EQUALS(runtime.Eval("new Span(1).length()"), "0");
        EQUALS(runtime.Eval("new Span(1, undefined).length()"), "0");
        EQUALS(runtime.Eval("new Span(1, 4).length()"), "3");
        EQUALS(runtime.Eval("new Span()"), "<threw> Error: Argument count does not match function definition. Expected 2 but got 0");
        EQUALS(runtime.Eval("new Span(1, 4, 9)"), "<threw> Error: Argument count does not match function definition. Expected 2 but got 3");
        EQUALS(runtime.Eval("new Span('x')"), "<threw> Error: expected Number, typeof=string");
        EQUALS(runtime.Eval("new Span(1, 'x')"), "<threw> Error: expected Optional, typeof=string");
        EQUALS(runtime.Symbol("Span")["constructor"]["parameters"][1]["type"].dump(), R"({"cppName":"","name":"number","optional":true})");

        LuaTest::State state;
        BindClassLuaTest::Publish(state.L, BindClassV8Test::SpanDefinition(), Framework::Scripting::Bind::Side::Server);
        EQUALS(LuaTest::Eval(state.L, "Span.new(1):length()"), "0");
        EQUALS(LuaTest::Eval(state.L, "Span.new(1, 4):length()"), "3");
    });

    IT("gives a pointer parameter a null pointer for anything that is not a bound object, where Lua refuses", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.Publish(BindClassV8Test::SpanDefinition());

        EQUALS(runtime.Eval("Span.lengthOf(new Span(1, 4))"), "3");
        EQUALS(runtime.Eval("Span.lengthOf(5)"), "-1");
        EQUALS(runtime.Eval("Span.lengthOf('span')"), "-1");
        EQUALS(runtime.Eval("Span.lengthOf(null)"), "-1");
        EQUALS(runtime.Eval("Span.lengthOf(undefined)"), "-1");
        EQUALS(runtime.Eval("Span.lengthOf({})"), "-1");

        LuaTest::State state;
        BindClassLuaTest::Publish(state.L, BindClassV8Test::SpanDefinition(), Framework::Scripting::Bind::Side::Server);
        EQUALS(LuaTest::Eval(state.L, "Span.lengthOf(Span.new(1, 4))"), "3");
        EQUALS(LuaTest::Eval(state.L, "Span.lengthOf(5)"), "<error> eval:1: bad argument #1 to 'Span.lengthOf' (Span expected, got number)");
    });

    IT("takes and returns glm values as the builtin vector classes", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);

        std::unique_ptr<v8pp::class_<BindClassV8Test::Compass>> compass;
        runtime.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            Framework::Scripting::Builtins::Vector3::Register(isolate, context->Global());
            compass = BindClassV8Test::MaterialiseCompass(isolate, runtime.Catalog());
            context->Global()->Set(context, v8pp::to_v8(isolate, "Compass"), compass->js_function_template()->GetFunction(context).ToLocalChecked()).Check();
        });

        EQUALS(runtime.Eval("var c = new Compass(); var v = c.shift(new Vector3(1, 2, 3)); v instanceof Vector3"), "true");
        EQUALS(runtime.Eval("[v.x, v.y, v.z].join()"), "1,2,3");
        EQUALS(runtime.Eval("c.heading = new Vector3(4, 5, 6); c.heading instanceof Vector3"), "true");
        EQUALS(runtime.Eval("c.shift(new Vector3(1, 1, 1)).toString()"), "Vector3(5, 6, 7)");
        EQUALS(runtime.Eval("Compass.north().toString()"), "Vector3(0, 1, 0)");
        EQUALS(runtime.Eval("c.shift(5)"), "<threw> Error: expected object for vec3");

        const nlohmann::json shift = BindClassV8Test::Runtime::Named(runtime.Symbol("Compass")["functions"], "shift");
        EQUALS(shift["parameters"][0]["type"]["name"], "Vector3");
        EQUALS(shift["returns"]["type"]["name"], "Vector3");
        EQUALS(BindClassV8Test::Runtime::Named(runtime.Symbol("Compass")["properties"], "heading")["type"]["name"], "Vector3");
    });

    IT("wraps an object made in C++", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        runtime.Enter([](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            v8pp::class_<BindClassLuaTest::Counter> &cls = BindClassV8Test::ClassOf(isolate, BindClassLuaTest::CounterDefinition());
            context->Global()->Set(context, v8pp::to_v8(isolate, "made"), cls.import_external(isolate, new BindClassLuaTest::Counter(5))).Check();
        });

        EQUALS(runtime.Eval("made instanceof Counter"), "true");
        EQUALS(runtime.Eval("made.add(1).get()"), "6");
    });

    // --- Inheritance ---

    IT("gives a derived class the methods and properties of its base", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        EQUALS(runtime.Eval("var s = new SubCounter(2); s.add(3).get()"), "5");
        EQUALS(runtime.Eval("s.get()"), "5");
        EQUALS(runtime.Eval("s.twice()"), "10");
        EQUALS(runtime.Eval("s.value = 4; s.value"), "4");
        EQUALS(runtime.Eval("Object.keys(s).length"), "0");
        EQUALS(runtime.Eval("s.doubled"), "8");
        EQUALS(runtime.Eval("s.reset(); s.get()"), "0");
        EQUALS(runtime.Eval("s instanceof SubCounter && s instanceof Counter"), "true");
        EQUALS(runtime.Eval("new Counter(1) instanceof SubCounter"), "false");
        EQUALS(runtime.Eval("s.kind() + ' ' + new Counter(1).kind()"), "sub counter");
        EQUALS(runtime.Eval("typeof new Counter(1).twice"), "undefined");
        EQUALS(runtime.Eval("typeof SubCounter.make"), "undefined");
    });

    IT("passes a derived object where its base is expected, wherever the base lies in it", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        EQUALS(runtime.Eval("new Counter(1).plus(new SubCounter(4))"), "5");
        EQUALS(runtime.Eval("new SubCounter(1).absorb(new SubCounter(4))"), "5");
        EQUALS(runtime.Eval("new SubCounter(1).absorb(new Counter(4))"), "<threw> Error: failed to unwrap C++ object");
    });

    // v8pp finds the wrapper of a returned object by its address, and the Counter part of a SubCounter does not
    // lie at the address of the SubCounter. Lua returns the receiver here.
    IT("returns a copy typed as the base from a base method called on an object whose base part is not first", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        EQUALS(runtime.Eval("var s = new SubCounter(2); var r = s.add(1); r === s"), "false");
        EQUALS(runtime.Eval("r instanceof Counter && !(r instanceof SubCounter)"), "true");
        EQUALS(runtime.Eval("r.get() + ' ' + s.get()"), "3 3");

        // The second call of a chain therefore lands on the copy.
        EQUALS(runtime.Eval("s.add(1).add(1).get()"), "5");
        EQUALS(runtime.Eval("s.get()"), "4");
    });

    IT("materialises the bases a definition ensures before the class", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.Publish(BindClassV8Test::DialDefinition());

        EQUALS(runtime.Eval("typeof Gauge"), "undefined");
        EQUALS(runtime.Eval("var d = new Dial(42); d.turns()"), "4");
        EQUALS(runtime.Eval("d.raise(8) === d"), "true");
        EQUALS(runtime.Eval("d.level"), "50");
        EQUALS(runtime.Eval("d.label('at ')"), "at 50");
        EQUALS(runtime.Eval("JSON.stringify(d)"), "{\"level\":50}");
        EQUALS(runtime.Symbol("Dial")["bases"].dump(), "[\"Gauge\"]");
        EQUALS(runtime.Symbol("Gauge")["description"], "Shows a level.");

        // The class of the base is the one its own cache holds.
        runtime.Publish(BindClassV8Test::GaugeDefinition());
        EQUALS(runtime.Eval("d instanceof Gauge"), "true");
    });

    IT("fails as v8pp does when nothing made the base exist", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);

        const std::string message = BindClassV8Test::MessageOf<std::runtime_error>([&] {
            runtime.Publish(BindClassLuaTest::SubCounterDefinition());
        });
        EQUALS(message.find("is not registered in isolate") != std::string::npos, true);
    });

    IT("calls a function that takes a base of the receiver, by reference or by pointer", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.Publish(BindClassV8Test::DialDefinition());

        EQUALS(runtime.Eval("var d = new Dial(30); d.half"), "15");
        EQUALS(runtime.Eval("d.half = 21; d.level"), "42");
        EQUALS(runtime.Eval("d.fill() === d"), "true");
        EQUALS(runtime.Eval("d.full"), "true");
        EQUALS(runtime.Eval("d.drain(); d.level"), "0");
        EQUALS(runtime.Eval("typeof Dial.fill"), "undefined");
    });

    IT("binds a member function of a base as a member of the derived class", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.Publish(BindClassV8Test::DialDefinition());

        EQUALS(runtime.Eval("var d = new Dial(30); d.raiseBy(5) === d"), "true");
        EQUALS(runtime.Eval("d.reading"), "35");
        EQUALS(runtime.Eval("d.reading = 1; d.reading"), "35");
    });

    // --- Sides ---

    IT("materialises the members of the runtime's side only", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime server(host, Framework::Scripting::Bind::Side::Server);
        BindClassV8Test::Runtime client(host, Framework::Scripting::Bind::Side::Client);
        server.PublishCounters();
        client.PublishCounters();

        EQUALS(server.Eval("new Counter(1).kick('x')"), "true");
        EQUALS(server.Eval("Counter.restricted()"), "1");
        EQUALS(server.Eval("'client' in new Counter(1)"), "false");

        EQUALS(client.Eval("typeof new Counter(1).kick"), "undefined");
        EQUALS(client.Eval("typeof Counter.restricted"), "undefined");
        EQUALS(client.Eval("new Counter(3).client"), "3");
        EQUALS(client.Eval("new Counter(3).add(1).get()"), "4");

        EQUALS(BindClassV8Test::Runtime::Named(server.Symbol("Counter")["functions"], "kick").is_null(), false);
        EQUALS(BindClassV8Test::Runtime::Named(server.Symbol("Counter")["functions"], "restricted").is_null(), false);
        EQUALS(BindClassV8Test::Runtime::Named(server.Symbol("Counter")["properties"], "client").is_null(), true);
        EQUALS(BindClassV8Test::Runtime::Named(client.Symbol("Counter")["functions"], "kick").is_null(), true);
        EQUALS(BindClassV8Test::Runtime::Named(client.Symbol("Counter")["functions"], "restricted").is_null(), true);
        EQUALS(BindClassV8Test::Runtime::Named(client.Symbol("Counter")["properties"], "client").is_null(), false);
    });

    IT("builds a class of its own in each isolate", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime first(host);
        BindClassV8Test::Runtime second(host);
        first.PublishCounters();
        second.PublishCounters();

        EQUALS(first.Eval("var c = new Counter(1); c.add(1).get()"), "2");
        EQUALS(second.Eval("typeof c"), "undefined");
        EQUALS(second.Eval("new Counter(5).add(1).get()"), "6");
    });

    // --- Catalog ---

    IT("records the class, its constructor and its members in the catalog", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();

        const nlohmann::json counter = runtime.Symbol("Counter");
        EQUALS(counter["description"], "Counts up and down.");
        EQUALS(counter["kind"], "constructor");
        EQUALS(counter["bases"].dump(), "[]");

        EQUALS(counter["constructor"]["name"], "constructor");
        EQUALS(counter["constructor"]["description"], "Creates a counter.");
        EQUALS(counter["constructor"]["parameters"].dump(), R"([{"description":"Initial value.","name":"value","type":{"cppName":"","name":"number","optional":false}}])");
        EQUALS(counter["constructor"]["returns"]["type"]["name"], "void");

        const nlohmann::json add = BindClassV8Test::Runtime::Named(counter["functions"], "add");
        EQUALS(add["description"], "Adds to the counter.");
        EQUALS(add["static"], false);
        EQUALS(add["parameters"].dump(), R"([{"description":"Amount to add.","name":"amount","type":{"cppName":"","name":"number","optional":false}}])");
        EQUALS(add["returns"].dump(), R"({"description":"This counter.","type":{"cppName":"","name":"this","optional":false}})");

        const nlohmann::json make = BindClassV8Test::Runtime::Named(counter["functions"], "make");
        EQUALS(make["static"], true);
        EQUALS(make["returns"]["type"]["name"], "Counter");

        const nlohmann::json scaled = BindClassV8Test::Runtime::Named(counter["functions"], "scaled");
        EQUALS(scaled["parameters"][1]["type"].dump(), R"({"cppName":"","name":"number","optional":true})");
        EQUALS(BindClassV8Test::Runtime::Named(counter["functions"], "sum")["parameters"][0]["type"]["name"], "Counter[]");
        EQUALS(BindClassV8Test::Runtime::Named(counter["functions"], "get")["returns"]["type"]["name"], "number");
        EQUALS(BindClassV8Test::Runtime::Named(counter["functions"], "throws")["returns"]["type"]["name"], "void");

        EQUALS(BindClassV8Test::Runtime::Named(counter["properties"], "value").dump(), R"({"description":"Current value.","name":"value","readonly":false,"static":false,"type":{"cppName":"","name":"number","optional":false}})");
        EQUALS(BindClassV8Test::Runtime::Named(counter["properties"], "doubled").dump(), R"({"description":"Twice the value.","name":"doubled","readonly":true,"static":false,"type":{"cppName":"","name":"number","optional":false}})");

        EQUALS(runtime.Symbol("SubCounter")["bases"].dump(), "[\"Counter\"]");
        EQUALS(runtime.Symbol("SubCounter")["description"], "A counter that does more.");
    });

    IT("records the members in the order they were declared, and nothing for what exists in Lua only", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();
        runtime.Publish(BindClassV8Test::GaugeDefinition());

        EQUALS(BindClassV8Test::Runtime::Names(runtime.Symbol("Counter")["functions"]), "add get toString throws kick kind plus scaled clone pick view self reset addTwice make sum maybe bump fail restricted");
        EQUALS(BindClassV8Test::Runtime::Names(runtime.Symbol("Counter")["properties"]), "value doubled negated fragile");
        EQUALS(BindClassV8Test::Runtime::Names(runtime.Symbol("SubCounter")["functions"]), "twice absorb kind");
        EQUALS(BindClassV8Test::Runtime::Names(runtime.Symbol("Gauge")["functions"]), "raise of label drain limit toJSON");
        EQUALS(BindClassV8Test::Runtime::Names(runtime.Symbol("Gauge")["properties"]), "level full");
        EQUALS(runtime.Eval("Object.getOwnPropertyNames(Object.getPrototypeOf(new Gauge(1))).filter(name => name !== 'constructor').join(' ')"), "raise label drain level full toJSON");
        EQUALS(runtime.Eval("typeof Gauge.LIMIT"), "undefined");
    });

    IT("records and binds what a hand-written v8pp class does", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime bound(host);
        BindClassV8Test::Runtime written(host);
        bound.Publish(BindClassV8Test::GaugeDefinition());
        bound.Publish(BindClassV8Test::DialDefinition());

        std::vector<std::shared_ptr<void>> keep;
        written.Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            BindClassV8Test::HandWrittenGaugeAndDial(isolate, written.Catalog(), keep);
            context->Global()->Set(context, v8pp::to_v8(isolate, "Gauge"), v8pp::class_<BindClassV8Test::Gauge>::extend(isolate).js_function_template()->GetFunction(context).ToLocalChecked()).Check();
            context->Global()->Set(context, v8pp::to_v8(isolate, "Dial"), v8pp::class_<BindClassV8Test::Dial>::extend(isolate).js_function_template()->GetFunction(context).ToLocalChecked()).Check();
        });

        EQUALS(v8pp::metadata::to_json(bound.Catalog()), v8pp::metadata::to_json(written.Catalog()));

        const char *const expressions[] = {
            "new Gauge(3).raise(4).level",
            "new Gauge('x').level",
            "Gauge.of(7).label('at ')",
            "Gauge.limit()",
            "var g = new Gauge(100); g.full",
            "g.drain(); g.level + ' ' + g.full",
            "g.level = 12; JSON.stringify(g)",
            "g.full = true; g.full",
            "new Gauge(1).raise()",
            "new Gauge(1).label(5)",
            "var d = new Dial(40); d.fill() === d",
            "d.half = 8; d.level + ' ' + d.turns()",
            "d.raiseBy(4) === d",
            "d.reading = 1; d.reading",
            "new Dial()",
            "Object.getOwnPropertyNames(Gauge).sort().join(' ')",
            "Object.getOwnPropertyNames(Object.getPrototypeOf(g)).join(' ')",
            "Object.getOwnPropertyNames(Object.getPrototypeOf(d)).join(' ')",
            "JSON.stringify(Object.getOwnPropertyDescriptor(Object.getPrototypeOf(g), 'full'))",
            "JSON.stringify(Object.getOwnPropertyDescriptor(Object.getPrototypeOf(g), 'level'))",
            "JSON.stringify(Object.getOwnPropertyDescriptor(Object.getPrototypeOf(g), 'raise'))",
            "JSON.stringify(Object.getOwnPropertyDescriptor(Object.getPrototypeOf(g), 'drain'))",
            "JSON.stringify(Object.getOwnPropertyDescriptor(Gauge, 'of'))",
        };
        for (const char *expression : expressions) {
            EQUALS(bound.Eval(expression), written.Eval(expression));
        }
        EQUALS(bound.Eval("new Gauge(3).raise(4).level"), "7");
    });

    // --- What exists in JavaScript only ---

    IT("builds the object with the V8 constructor and documents the declared one", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.Publish(BindClassV8Test::GaugeDefinition());

        EQUALS(runtime.Eval("new Gauge(3).level"), "3");
        EQUALS(runtime.Eval("new Gauge().level"), "0");
        EQUALS(runtime.Eval("new Gauge('x', 2).level"), "0");

        const nlohmann::json constructor = runtime.Symbol("Gauge")["constructor"];
        EQUALS(constructor["description"], "Creates a gauge.");
        EQUALS(constructor["parameters"].dump(), R"([{"description":"Initial level.","name":"level","type":{"cppName":"","name":"number","optional":false}}])");
    });

    IT("keeps the declared constructor and what exists in Lua only for Lua", {
        LuaTest::State state;
        BindClassLuaTest::Publish(state.L, BindClassV8Test::GaugeDefinition(), Framework::Scripting::Bind::Side::Server);

        EQUALS(LuaTest::Eval(state.L, "Gauge.new(3):raise(4).level"), "7");
        EQUALS(LuaTest::Eval(state.L, "Gauge.new()"), "<error> eval:1: wrong number of arguments to 'Gauge.new' (1..1 expected, got 0)");
        EQUALS(LuaTest::Eval(state.L, "#Gauge.new(3)"), "3");
        EQUALS(LuaTest::Eval(state.L, "Gauge.LIMIT"), "100");
        EQUALS(LuaTest::Eval(state.L, "Gauge.new(3).toJSON"), "nil");
    });

    IT("runs a V8 hook at each materialisation, after the declared members", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime first(host);
        BindClassV8Test::Runtime second(host);

        BindClassV8Test::gaugeHookRuns = 0;
        first.Publish(BindClassV8Test::GaugeDefinition());
        EQUALS(BindClassV8Test::gaugeHookRuns, 1);
        second.Publish(BindClassV8Test::GaugeDefinition());
        EQUALS(BindClassV8Test::gaugeHookRuns, 2);

        EQUALS(second.Eval("JSON.stringify({ gauge: new Gauge(3) })"), "{\"gauge\":{\"level\":3}}");
        const nlohmann::json functions = second.Symbol("Gauge")["functions"];
        EQUALS(functions.back()["name"], "toJSON");
        EQUALS(functions.back()["returns"]["type"]["name"], "{ level: number }");
    });

    IT("binds a function declared noexcept", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.Publish(BindClassV8Test::GaugeDefinition());

        EQUALS(runtime.Eval("Gauge.limit()"), "100");
        EQUALS(runtime.Eval("new Gauge(3).level"), "3");
    });

    IT("refuses a V8 constructor for a class that declares none, and a second one", {
        EQUALS(BindClassV8Test::MessageOf<std::logic_error>([] {
            Framework::Scripting::Bind::Class<BindClassV8Test::Bare>("Bare", "Has no constructor.").V8Constructor([](const v8::FunctionCallbackInfo<v8::Value> &) {
                return new BindClassV8Test::Bare();
            });
        }),
            "Bind: 'Bare' is given a V8 constructor before it declares a constructor");

        EQUALS(BindClassV8Test::MessageOf<std::logic_error>([] {
            const auto make = [](const v8::FunctionCallbackInfo<v8::Value> &) {
                return new BindClassV8Test::Lenient(0);
            };
            Framework::Scripting::Bind::Class<BindClassV8Test::Lenient>("Lenient", "Has two.").Constructor<int>(Framework::Scripting::Bind::Doc("Creates one.").Param("value", "Ignored.")).V8Constructor(make).V8Constructor(make);
        }),
            "Bind: 'Lenient' is given a second V8 constructor");
    });

    // --- What JavaScript cannot have ---

    // A caller keeps a reference to the class it is given, so there is no quiet way to give none.
    IT("refuses to materialise a class in a runtime of a side it does not exist in", {
        const auto *definition = Framework::Scripting::Bind::Class<BindClassLuaTest::ServerOnly>::Find();
        EQUALS(definition != nullptr, true);
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime client(host, Framework::Scripting::Bind::Side::Client);
        v8::Isolate *isolate = client.Isolate();
        EQUALS(Framework::Scripting::Bind::ExistsIn(definition->Describe(), Framework::Scripting::Bind::Side::Client), false);
        const std::string before = v8pp::metadata::to_json(client.Catalog());
        std::string message;
        try {
            definition->MaterialiseV8(isolate, client.Catalog(), Framework::Scripting::Bind::Side::Client);
        }
        catch (const std::logic_error &error) {
            message = error.what();
        }
        EQUALS(message, "Bind: 'ServerOnly' does not exist in a client runtime");
        EQUALS(v8pp::metadata::to_json(client.Catalog()) == before, true);
        EQUALS(Framework::Scripting::Bind::SideName(Framework::Scripting::Bind::Side::Server) == "server" && Framework::Scripting::Bind::SideName(Framework::Scripting::Bind::Side::Client) == "client"
                   && Framework::Scripting::Bind::SideName(Framework::Scripting::Bind::Side::Shared) == "shared",
            true);
    });

    IT("constructs no object of a class that declares no constructor", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);
        runtime.PublishCounters();
        runtime.Publish(BindClassLuaTest::SealedDefinition());

        EQUALS(runtime.Eval("new Sealed()").find("has no constructor") != std::string::npos, true);
        EQUALS(runtime.Eval("Sealed()").find("has no constructor") != std::string::npos, true);
        EQUALS(runtime.Eval("Sealed.make().value"), "3");
        EQUALS(runtime.Eval("Sealed.counter().get()"), "1");
        EQUALS(runtime.Symbol("Sealed")["constructor"].is_null(), true);
    });

    IT("refuses to materialise a member that returns a class that cannot be copied", {
        BindClassV8Test::Host host;
        BindClassV8Test::Runtime runtime(host);

        EQUALS(BindClassV8Test::MessageOf<std::logic_error>([&] {
            runtime.Publish(BindClassLuaTest::TokenDefinition());
        }),
            "Bind: 'Token.make' returns an object of a class that cannot be copied, which JavaScript cannot receive");
        EQUALS(runtime.Symbol("Token").is_null(), true);
        EQUALS(runtime.Eval("typeof Token"), "undefined");
    });
});
