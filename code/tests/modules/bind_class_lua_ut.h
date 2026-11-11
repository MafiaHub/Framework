/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_test_helpers.h"

#include "scripting/bind/class.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace BindClassLuaTest {
    namespace Bind = Framework::Scripting::Bind;

    class Counter {
      public:
        Counter() = default;
        explicit Counter(int value): _value(value) {}

        Counter &add(int amount) {
            _value += amount;
            return *this;
        }

        int get() const {
            return _value;
        }

        void set(int value) {
            _value = value;
        }

        int doubled() const {
            return _value * 2;
        }

        std::string toString() const {
            return "Counter(" + std::to_string(_value) + ")";
        }

        void throws() const {
            throw std::runtime_error("boom");
        }

        bool kick(const std::string &reason) {
            return !reason.empty();
        }

        std::string kind() const {
            return "counter";
        }

        int plus(const Counter &other) const {
            return _value + other._value;
        }

        int scaled(int factor, std::optional<int> offset) const {
            return _value * factor + offset.value_or(0);
        }

        Counter clone() const {
            return *this;
        }

        // A reference to the receiver or to another counter, as the caller asks.
        Counter &pick(Counter &other, bool takeOther) {
            return takeOther ? other : *this;
        }

        const Counter &view() const {
            return *this;
        }

        Counter *self() {
            return this;
        }

        static Counter make(int value) {
            return Counter(value);
        }

      private:
        int _value = 0;
    };

    inline Counter &AddTwice(Counter *counter, int amount) {
        return counter->add(amount).add(amount);
    }

    inline Counter AddCounters(const Counter &a, const Counter &b) {
        return Counter(a.get() + b.get());
    }

    // Puts the Counter part of a SubCounter away from the start of the object, so a missing upcast shows.
    struct Padding {
        int64_t padding = 7;
    };

    class SubCounter final
        : public Padding
        , public Counter {
      public:
        explicit SubCounter(int value): Counter(value) {}

        int twice() const {
            return get() * 2;
        }

        int absorb(const SubCounter &other) {
            return add(other.get()).get();
        }

        std::string kind() const {
            return "sub";
        }
    };

    // Counts the instances alive, to see a userdata destroy what it holds.
    class Tracked final {
      public:
        explicit Tracked(int id): _id(id) {
            if (id < 0) {
                throw std::invalid_argument("Tracked: id must not be negative");
            }
            ++alive;
        }

        Tracked(const Tracked &other): _id(other._id) {
            ++alive;
        }

        Tracked &operator=(const Tracked &) = default;

        ~Tracked() {
            --alive;
        }

        int id() const {
            return _id;
        }

        static inline int alive = 0;

      private:
        int _id = 0;
    };

    // Cannot be copied, so a script gets one only by a move.
    class Token final {
      public:
        explicit Token(int value): _value(std::make_unique<int>(value)) {}

        int value() const {
            return *_value;
        }

      private:
        std::unique_ptr<int> _value;
    };

    // Has no script constructor.
    struct Sealed final {
        int value = 3;
    };

    struct Orphan {};
    struct OrphanChild final: Orphan {};

    struct Misnamed final {};

    struct Mismatch final {
        explicit Mismatch(int) {}

        void grow(int) {}
    };

    struct Twice final {};

    struct Arity final {
        explicit Arity(int) {}
    };

} // namespace BindClassLuaTest

FW_SCRIPT_CLASS(BindClassLuaTest::Counter, "Counter");
FW_SCRIPT_CLASS(BindClassLuaTest::SubCounter, "SubCounter");
FW_SCRIPT_CLASS(BindClassLuaTest::Tracked, "Tracked");
FW_SCRIPT_CLASS(BindClassLuaTest::Token, "Token");
FW_SCRIPT_CLASS(BindClassLuaTest::Sealed, "Sealed");
FW_SCRIPT_CLASS(BindClassLuaTest::Orphan, "Orphan");
FW_SCRIPT_CLASS(BindClassLuaTest::OrphanChild, "OrphanChild");
FW_SCRIPT_CLASS(BindClassLuaTest::Misnamed, "Misnamed");
FW_SCRIPT_CLASS(BindClassLuaTest::Mismatch, "Mismatch");
FW_SCRIPT_CLASS(BindClassLuaTest::Twice, "Twice");
FW_SCRIPT_CLASS(BindClassLuaTest::Arity, "Arity");

namespace BindClassLuaTest {
    inline int hookRuns = 0;

    inline const Bind::Class<Counter> &CounterDefinition() {
        using Bind::Doc;
        using Bind::LuaMetaEvent;
        static const auto definition = Bind::Class<Counter>("Counter", "Counts up and down.")
                                           .Constructor<int>(Doc("Creates a counter.").Param("value", "Initial value."))
                                           .Method("add", &Counter::add, Doc("Adds to the counter.").Param("amount", "Amount to add.").Returns("This counter."))
                                           .Method("get", &Counter::get, Doc("Reads the counter.").Returns("Current value."))
                                           .Method("toString", &Counter::toString, Doc("Formats the counter."))
                                           .Method("throws", &Counter::throws, Doc("Always fails."))
                                           .Method("kick", &Counter::kick, Doc("Kicks the counter.").Param("reason", "Why."), Bind::Side::Server)
                                           .Method("kind", &Counter::kind, Doc("Names the class."))
                                           .Method("plus", &Counter::plus, Doc("Adds two counters.").Param("other", "Counter to add."))
                                           .Method("scaled", &Counter::scaled, Doc("Scales the counter.").Param("factor", "Multiplier.").Param("offset", "Added after scaling."))
                                           .Method("clone", &Counter::clone, Doc("Copies the counter."))
                                           .Method("pick", &Counter::pick, Doc("Picks a counter.").Param("other", "Another counter.").Param("takeOther", "Whether to return it."))
                                           .Method("view", &Counter::view, Doc("Returns the counter."))
                                           .Method("self", &Counter::self, Doc("Returns the counter."))
                                           .Method(
                                               "reset",
                                               [](Counter &counter) {
                                                   counter.set(0);
                                               },
                                               Doc("Zeroes the counter."))
                                           .Method("addTwice", &AddTwice, Doc("Adds an amount twice.").Param("amount", "Amount to add."))
                                           .Property("value", &Counter::get, &Counter::set, "Current value.")
                                           .Property("doubled", &Counter::doubled, "Twice the value.")
                                           .Property(
                                               "negated",
                                               [](const Counter &counter) {
                                                   return -counter.get();
                                               },
                                               [](Counter &counter, int value) {
                                                   counter.set(-value);
                                               },
                                               "The value with its sign flipped.")
                                           .Property("client", &Counter::get, "Only on the client.", Bind::Side::Client)
                                           .Property(
                                               "fragile",
                                               [](const Counter &) -> int {
                                                   throw std::runtime_error("getter failed");
                                               },
                                               [](Counter &, int) {
                                                   throw std::runtime_error("setter failed");
                                               },
                                               "Always fails.")
                                           .Static("make", &Counter::make, Doc("Makes a counter.").Param("value", "Initial value."))
                                           .Static(
                                               "sum",
                                               [](const std::vector<Counter> &counters) {
                                                   int total = 0;
                                                   for (const Counter &counter : counters) {
                                                       total += counter.get();
                                                   }
                                                   return total;
                                               },
                                               Doc("Adds counters up.").Param("counters", "Counters to add."))
                                           .Static(
                                               "maybe",
                                               [](std::optional<Counter> counter) {
                                                   return counter;
                                               },
                                               Doc("Returns what it was given.").Param("counter", "A counter, or nothing."))
                                           .Static(
                                               "bump",
                                               [](Counter *counter) {
                                                   counter->add(1);
                                               },
                                               Doc("Adds one to a counter.").Param("counter", "Counter to change."))
                                           .Static(
                                               "fail",
                                               []() -> int {
                                                   throw std::out_of_range("static failed");
                                               },
                                               Doc("Always fails."))
                                           .Static(
                                               "restricted",
                                               [] {
                                                   return 1;
                                               },
                                               Doc("Only on the server."), Bind::Side::Server)
                                           .LuaMeta(LuaMetaEvent::Add, &AddCounters)
                                           .LuaMeta(LuaMetaEvent::Eq,
                                               [](const Counter &a, const Counter &b) {
                                                   return a.get() == b.get();
                                               })
                                           .LuaMeta(LuaMetaEvent::Lt,
                                               [](const Counter &a, const Counter &b) {
                                                   return a.get() < b.get();
                                               })
                                           .LuaMeta(LuaMetaEvent::Unm,
                                               [](const Counter &a) {
                                                   return Counter(-a.get());
                                               })
                                           .LuaMeta(LuaMetaEvent::Len,
                                               [](const Counter &a) {
                                                   return a.get();
                                               })
                                           .LuaMeta(LuaMetaEvent::Div,
                                               [](const Counter &, const Counter &) -> Counter {
                                                   throw std::domain_error("operator failed");
                                               })
                                           .LuaMeta(LuaMetaEvent::Mul,
                                               [](const Counter &a, int factor) {
                                                   return Counter(a.get() * factor);
                                               })
                                           .LuaMeta(LuaMetaEvent::Mul,
                                               [](int factor, const Counter &a) {
                                                   return Counter(a.get() * factor);
                                               })
                                           .LuaHook([](lua_State *L, int metatableIndex, int classTableIndex) {
                                               ++hookRuns;
                                               lua_pushinteger(L, 99);
                                               lua_setfield(L, classTableIndex, "LIMIT");
                                               lua_pushliteral(L, "hooked");
                                               lua_setfield(L, metatableIndex, "hook");
                                           });
        return definition;
    }

    inline const Bind::Class<SubCounter> &SubCounterDefinition() {
        using Bind::Doc;
        CounterDefinition();
        static const auto definition = Bind::Class<SubCounter>("SubCounter", "A counter that does more.")
                                           .Inherit<Counter>()
                                           .Constructor<int>(Doc("Creates a sub counter.").Param("value", "Initial value."))
                                           .Method("twice", &SubCounter::twice, Doc("Doubles the value."))
                                           .Method("absorb", &SubCounter::absorb, Doc("Takes the value of another.").Param("other", "Sub counter to take from."))
                                           .Method("kind", &SubCounter::kind, Doc("Names the class."));
        return definition;
    }

    inline const Bind::Class<Tracked> &TrackedDefinition() {
        using Bind::Doc;
        static const auto definition = Bind::Class<Tracked>("Tracked", "Counts its instances.")
                                           .Constructor<int>(Doc("Creates a tracked value.").Param("id", "Identifier, not negative."))
                                           .Method("id", &Tracked::id, Doc("Reads the identifier."))
                                           .Static(
                                               "idOf",
                                               [](const Tracked &tracked) {
                                                   return tracked.id();
                                               },
                                               Doc("Reads the identifier of a value.").Param("tracked", "Value to read."));
        return definition;
    }

    inline const Bind::Class<Token> &TokenDefinition() {
        using Bind::Doc;
        static const auto definition = Bind::Class<Token>("Token", "Cannot be copied.")
                                           .Constructor<int>(Doc("Creates a token.").Param("value", "Its value."))
                                           .Method("value", &Token::value, Doc("Reads the value."))
                                           .Static(
                                               "make",
                                               [](int value) {
                                                   return Token(value);
                                               },
                                               Doc("Makes a token.").Param("value", "Its value."));
        return definition;
    }

    inline const Bind::Class<Sealed> &SealedDefinition() {
        using Bind::Doc;
        static const auto definition = Bind::Class<Sealed>("Sealed", "Cannot be built by a script.")
                                           .Property(
                                               "value",
                                               [](const Sealed &sealed) {
                                                   return sealed.value;
                                               },
                                               "Its value.")
                                           .Static(
                                               "make",
                                               [] {
                                                   return Sealed {};
                                               },
                                               Doc("Makes a sealed value."))
                                           .Static(
                                               "counter",
                                               [] {
                                                   return Counter(1);
                                               },
                                               Doc("Makes a counter."));
        return definition;
    }

    // Materialises `definition` and publishes it as a global.
    template <typename T>
    void Publish(lua_State *L, const Bind::Class<T> &definition, Bind::Side side) {
        definition.MaterialiseLua(L, side);
        lua_pushglobaltable(L);
        definition.PublishLua(L, -1);
        lua_pop(L, 1);
    }

    inline void Open(lua_State *L, Bind::Side side = Bind::Side::Server) {
        Publish(L, CounterDefinition(), side);
        Publish(L, SubCounterDefinition(), side);
        Publish(L, TrackedDefinition(), side);
        Publish(L, TokenDefinition(), side);
        Publish(L, SealedDefinition(), side);
    }

    inline const Bind::FunctionDesc *FindFunction(const Bind::ClassDesc &desc, const std::string &name) {
        for (const Bind::FunctionDesc &function : desc.functions) {
            if (function.name == name) {
                return &function;
            }
        }
        return nullptr;
    }

    inline const Bind::PropertyDesc *FindProperty(const Bind::ClassDesc &desc, const std::string &name) {
        for (const Bind::PropertyDesc &property : desc.properties) {
            if (property.name == name) {
                return &property;
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

    // Whether the metatable of the value on top of the stack has the field `name`.
    inline bool HasMetafield(lua_State *L, const char *name) {
        const bool has = luaL_getmetafield(L, -1, name) != LUA_TNIL;
        if (has) {
            lua_pop(L, 1);
        }
        return has;
    }
} // namespace BindClassLuaTest

MODULE(bind_class_lua, {
    using namespace Framework::Scripting::Bind;
    using BindClassLuaTest::Counter;
    using BindClassLuaTest::CounterDefinition;
    using BindClassLuaTest::LogicErrorOf;
    using BindClassLuaTest::Open;
    using BindClassLuaTest::SubCounter;
    using BindClassLuaTest::SubCounterDefinition;
    using BindClassLuaTest::Tracked;
    using LuaTest::Eval;

    // --- Construction ---

    IT("constructs with new and by calling the class", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "Counter.new(2):add(3):get()"), "5");
        EQUALS(Eval(state.L, "Counter(2).value"), "2");
        EQUALS(Eval(state.L, "type(Counter.new(1))"), "userdata");
        EQUALS(Eval(state.L, "type(Counter)"), "table");
    });

    IT("checks the arguments of a constructor however it is called", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "Counter.new('2')"), "<error> eval:1: bad argument #1 to 'Counter.new' (integer expected, got string)");
        EQUALS(Eval(state.L, "Counter('2')"), "<error> eval:1: bad argument #1 to 'Counter.new' (integer expected, got string)");
        EQUALS(Eval(state.L, "Counter.new()"), "<error> eval:1: wrong number of arguments to 'Counter.new' (1..1 expected, got 0)");
        EQUALS(Eval(state.L, "Counter(1, 2)"), "<error> eval:1: wrong number of arguments to 'Counter.new' (1..1 expected, got 2)");
    });

    IT("refuses to construct a class that declares no constructor", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "Sealed()"), "<error> eval:1: Sealed cannot be constructed from script");
        EQUALS(Eval(state.L, "Sealed.new"), "nil");
        EQUALS(Eval(state.L, "Sealed.make().value"), "3");
    });

    IT("turns an exception from a constructor into an error and destroys nothing", {
        LuaTest::State state;
        Open(state.L);

        Tracked::alive = 0;
        EQUALS(Eval(state.L, "Tracked.new(-1)"), "<error> eval:1: Tracked: id must not be negative");
        EQUALS(Eval(state.L, "collectgarbage() return 1 + 1"), "2");
        EQUALS(Tracked::alive, 0);
    });

    // --- Methods ---

    IT("returns the receiver itself from a method that returns a reference to it", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(1)"), "nil");
        EQUALS(Eval(state.L, "rawequal(c, c:add(1))"), "true");
        EQUALS(Eval(state.L, "c:get()"), "2");
        EQUALS(Eval(state.L, "rawequal(c, c:addTwice(2))"), "true");
        EQUALS(Eval(state.L, "c:get()"), "6");
        EQUALS(Eval(state.L, "rawequal(c, c:view())"), "true");
        EQUALS(Eval(state.L, "rawequal(c, c:self())"), "true");
        EQUALS(Eval(state.L, "rawequal(c, c:pick(Counter.new(9), false))"), "true");
    });

    IT("copies a returned value or a reference to another object into a new userdata", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(1) other = Counter.new(9)"), "nil");
        EQUALS(Eval(state.L, "rawequal(c, c:clone())"), "false");
        EQUALS(Eval(state.L, "c:clone():get()"), "1");
        EQUALS(Eval(state.L, "picked = c:pick(other, true) return picked:get()"), "9");
        EQUALS(Eval(state.L, "rawequal(picked, other)"), "false");
        EQUALS(Eval(state.L, "picked:add(1) return other:get()"), "9");
    });

    IT("moves an object returned by value into its userdata", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "Token.make(4):value()"), "4");
        EQUALS(Eval(state.L, "Token.new(2):value()"), "2");
        EQUALS(Eval(state.L, "Token(3):value()"), "3");
    });

    IT("calls a function or a lambda that takes the receiver first as a method", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(4) c:reset() return c:get()"), "0");
        EQUALS(Eval(state.L, "c:addTwice(3):get()"), "6");
        EQUALS(Eval(state.L, "c:addTwice('3')"), "<error> eval:1: bad argument #1 to 'Counter:addTwice' (integer expected, got string)");
        EQUALS(Eval(state.L, "c.reset(1)"), "<error> eval:1: bad receiver for 'Counter:reset' (Counter expected, got number)");
    });

    IT("lets trailing optional arguments be omitted", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(3)"), "nil");
        EQUALS(Eval(state.L, "c:scaled(2)"), "6");
        EQUALS(Eval(state.L, "c:scaled(2, nil)"), "6");
        EQUALS(Eval(state.L, "c:scaled(2, 1)"), "7");
        EQUALS(Eval(state.L, "c:scaled()"), "<error> eval:1: wrong number of arguments to 'Counter:scaled' (1..2 expected, got 0)");
        EQUALS(Eval(state.L, "c:scaled(2, 1, 0)"), "<error> eval:1: wrong number of arguments to 'Counter:scaled' (1..2 expected, got 3)");
        EQUALS(Eval(state.L, "c:scaled(2, 'x')"), "<error> eval:1: bad argument #2 to 'Counter:scaled' (integer expected, got string)");
    });

    IT("raises on a wrong type, a missing or an extra argument", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(1)"), "nil");
        EQUALS(Eval(state.L, "c:add('x')"), "<error> eval:1: bad argument #1 to 'Counter:add' (integer expected, got string)");
        EQUALS(Eval(state.L, "c:add(1.5)"), "<error> eval:1: bad argument #1 to 'Counter:add' (integer expected, got number)");
        EQUALS(Eval(state.L, "c:add(nil)"), "<error> eval:1: bad argument #1 to 'Counter:add' (integer expected, got nil)");
        EQUALS(Eval(state.L, "c:add()"), "<error> eval:1: wrong number of arguments to 'Counter:add' (1..1 expected, got 0)");
        EQUALS(Eval(state.L, "c:add(1, 2)"), "<error> eval:1: wrong number of arguments to 'Counter:add' (1..1 expected, got 2)");
        EQUALS(Eval(state.L, "c:get(1)"), "<error> eval:1: wrong number of arguments to 'Counter:get' (0..0 expected, got 1)");
        EQUALS(Eval(state.L, "c:plus(5)"), "<error> eval:1: bad argument #1 to 'Counter:plus' (Counter expected, got number)");
        EQUALS(Eval(state.L, "c:plus(Tracked.new(1))"), "<error> eval:1: bad argument #1 to 'Counter:plus' (Counter expected, got Tracked)");
        EQUALS(Eval(state.L, "c:get()"), "1");
    });

    IT("raises a receiver error when a method is called without its receiver", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(1)"), "nil");
        EQUALS(Eval(state.L, "c.add(5, 1)"), "<error> eval:1: bad receiver for 'Counter:add' (Counter expected, got number)");
        EQUALS(Eval(state.L, "c.add()"), "<error> eval:1: bad receiver for 'Counter:add' (Counter expected, got no value)");
        EQUALS(Eval(state.L, "c.add(Tracked.new(1), 1)"), "<error> eval:1: bad receiver for 'Counter:add' (Counter expected, got Tracked)");
        EQUALS(Eval(state.L, "c.add(c, 1):get()"), "2");
    });

    IT("turns a C++ exception into an error and leaves the state usable", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(1)"), "nil");
        EQUALS(Eval(state.L, "c:throws()"), "<error> eval:1: boom");
        EQUALS(Eval(state.L, "pcall(c.throws, c)"), "false");
        EQUALS(Eval(state.L, "select(2, pcall(c.throws, c))"), "boom");
        EQUALS(Eval(state.L, "1 + 1"), "2");
        EQUALS(Eval(state.L, "c:add(1):get()"), "2");
        EQUALS(lua_gettop(state.L), 0);
    });

    IT("turns an exception from a property, a static or an operator into an error too", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(1)"), "nil");
        EQUALS(Eval(state.L, "c.fragile"), "<error> eval:1: getter failed");
        EQUALS(Eval(state.L, "c.fragile = 1"), "<error> eval:1: setter failed");
        EQUALS(Eval(state.L, "Counter.fail()"), "<error> eval:1: static failed");
        EQUALS(Eval(state.L, "c / c"), "<error> eval:1: operator failed");
        EQUALS(Eval(state.L, "c:get()"), "1");
    });

    // --- Properties ---

    IT("reads and writes a property", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(4)"), "nil");
        EQUALS(Eval(state.L, "c.value"), "4");
        EQUALS(Eval(state.L, "c.value = 9 return c:get()"), "9");
        EQUALS(Eval(state.L, "c.doubled"), "18");
        EQUALS(Eval(state.L, "c.negated"), "-9");
        EQUALS(Eval(state.L, "c.negated = 5 return c.value"), "-5");
        EQUALS(Eval(state.L, "c.value = 'x'"), "<error> eval:1: bad argument #1 to 'Counter.value' (integer expected, got string)");
        EQUALS(Eval(state.L, "c.value"), "-5");
    });

    IT("raises on a write to a read-only property or an unknown key and reads an unknown key as nil", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(4)"), "nil");
        EQUALS(Eval(state.L, "c.doubled = 1"), "<error> eval:1: attempt to set 'doubled' on Counter");
        EQUALS(Eval(state.L, "c.nope"), "nil");
        EQUALS(Eval(state.L, "c[1]"), "nil");
        EQUALS(Eval(state.L, "c.nope = 1"), "<error> eval:1: attempt to set 'nope' on Counter");
        EQUALS(Eval(state.L, "c[1] = 1"), "<error> eval:1: attempt to set '1' on Counter");
        EQUALS(Eval(state.L, "c.add = 1"), "<error> eval:1: attempt to set 'add' on Counter");
        EQUALS(Eval(state.L, "c.doubled"), "8");
    });

    IT("keeps the metatable and its metamethods out of reach", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(4)"), "nil");
        EQUALS(Eval(state.L, "getmetatable(c) == false"), "true");
        EQUALS(Eval(state.L, "c.__index"), "nil");
        EQUALS(Eval(state.L, "c.__gc"), "nil");
        EQUALS(Eval(state.L, "c.__name"), "nil");
        EQUALS(Eval(state.L, "pcall(setmetatable, c, {})"), "false");
    });

    // --- Statics and the class table ---

    IT("calls a static through the class table", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "Counter.make(4):get()"), "4");
        EQUALS(Eval(state.L, "Counter.make('4')"), "<error> eval:1: bad argument #1 to 'Counter.make' (integer expected, got string)");
        EQUALS(Eval(state.L, "Counter.make()"), "<error> eval:1: wrong number of arguments to 'Counter.make' (1..1 expected, got 0)");
        EQUALS(Eval(state.L, "Counter:make(4)"), "<error> eval:1: wrong number of arguments to 'Counter.make' (1..1 expected, got 2)");
        EQUALS(Eval(state.L, "Counter.new(1).make"), "nil");
    });

    IT("keeps the class table read-only", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "Counter.new = nil"), "<error> eval:1: attempt to set 'new' on class Counter");
        EQUALS(Eval(state.L, "Counter.extra = 1"), "<error> eval:1: attempt to set 'extra' on class Counter");
        EQUALS(Eval(state.L, "rawget(Counter, 'new')"), "nil");
        EQUALS(Eval(state.L, "getmetatable(Counter) == false"), "true");
        EQUALS(Eval(state.L, "Counter.nope"), "nil");
        EQUALS(Eval(state.L, "Counter.new(2):get()"), "2");
    });

    IT("uses toString for tostring and names the class otherwise", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "tostring(Counter.new(5))"), "Counter(5)");
        EQUALS(Eval(state.L, "Counter.new(5):toString()"), "Counter(5)");
        EQUALS(Eval(state.L, "tostring(Tracked.new(1)):match('^Tracked: ') ~= nil"), "true");
    });

    // --- Conversions of a bound class ---

    IT("takes and returns bound objects inside vectors and optionals", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "Counter.sum({ Counter.new(1), Counter.new(2), SubCounter.new(3) })"), "6");
        EQUALS(Eval(state.L, "Counter.sum({})"), "0");
        EQUALS(Eval(state.L, "Counter.sum({ Counter.new(1), 2 })"), "<error> eval:1: bad argument #1 to 'Counter.sum' (table expected, got table)");
        EQUALS(Eval(state.L, "Counter.maybe()"), "nil");
        EQUALS(Eval(state.L, "Counter.maybe(nil)"), "nil");
        EQUALS(Eval(state.L, "Counter.maybe(Counter.new(8)):get()"), "8");
        EQUALS(Eval(state.L, "Counter.maybe(8)"), "<error> eval:1: bad argument #1 to 'Counter.maybe' (Counter expected, got number)");
    });

    IT("passes a pointer or a reference into the userdata and refuses nil for a pointer", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(1) Counter.bump(c) return c:get()"), "2");
        EQUALS(Eval(state.L, "Counter.bump(nil)"), "<error> eval:1: bad argument #1 to 'Counter.bump' (Counter expected, got nil)");
    });

    IT("converts a bound object on the stack", {
        LuaTest::State state;
        lua_State *L = state.L;
        Open(L);

        LuaConvert<Counter>::Push(L, Counter(3));
        EQUALS(lua_gettop(L), 1);
        EQUALS(lua_type(L, 1), LUA_TUSERDATA);
        STREQUALS(LuaTypeNameAt(L, 1), "Counter");
        STREQUALS(LuaConvert<Counter>::Expected(), "Counter");
        STREQUALS(LuaConvert<const Counter &>::Expected(), "Counter");
        STREQUALS(LuaConvert<Counter *>::Expected(), "Counter");

        EQUALS(LuaConvert<Counter>::Is(L, 1), true);
        EQUALS(LuaConvert<Counter>::Is(L, -1), true);
        EQUALS(LuaConvert<SubCounter>::Is(L, 1), false);
        EQUALS(LuaConvert<Tracked>::Is(L, 1), false);
        EQUALS(LuaConvert<Counter>::Is(L, 2), false);
        EQUALS(LuaConvert<int>::Is(L, 1), false);

        // A copy leaves the userdata alone, a reference or a pointer reaches into it.
        Counter copy = LuaConvert<Counter>::Get(L, 1);
        copy.add(1);
        EQUALS(LuaConvert<const Counter &>::Get(L, 1).get(), 3);
        LuaConvert<Counter &>::Get(L, 1).add(1);
        EQUALS(LuaConvert<const Counter &>::Get(L, 1).get(), 4);
        LuaConvert<Counter *>::Get(L, 1)->add(1);
        EQUALS(LuaConvert<const Counter *>::Get(L, 1)->get(), 5);
        EQUALS(lua_gettop(L), 1);

        lua_pushnil(L);
        EQUALS(LuaConvert<Counter *>::Is(L, 2), false);
        EQUALS(LuaConvert<std::optional<Counter>>::Is(L, 2), true);
        LuaConvert<const Counter *>::Push(L, nullptr);
        EQUALS(lua_type(L, 3), LUA_TNIL);

        lua_newuserdatauv(L, 4, 0);
        EQUALS(LuaConvert<Counter>::Is(L, 4), false);
        STREQUALS(LuaTypeNameAt(L, 4), "userdata");
        lua_pushlightuserdata(L, &copy);
        EQUALS(LuaConvert<Counter>::Is(L, 5), false);
        EQUALS(lua_gettop(L), 5);
    });

    IT("raises when a class that was not materialised in the state is pushed", {
        LuaTest::State state;
        BindClassLuaTest::Publish(state.L, BindClassLuaTest::SealedDefinition(), Side::Server);

        EQUALS(Eval(state.L, "Sealed.counter()"), "<error> eval:1: Counter is not available in this Lua state");
        EQUALS(Eval(state.L, "Sealed.make().value"), "3");
    });

    // --- Operators ---

    IT("gives the class the operators it declares with LuaMeta", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "a = Counter.new(2) b = Counter.new(3)"), "nil");
        EQUALS(Eval(state.L, "(a + b):get()"), "5");
        EQUALS(Eval(state.L, "a:get()"), "2");
        EQUALS(Eval(state.L, "(-a):get()"), "-2");
        EQUALS(Eval(state.L, "#b"), "3");
        EQUALS(Eval(state.L, "a < b"), "true");
        EQUALS(Eval(state.L, "b < a"), "false");
        EQUALS(Eval(state.L, "a + 1"), "<error> eval:1: bad argument #2 to 'Counter.__add' (Counter expected, got number)");
        EQUALS(Eval(state.L, "1 + a"), "<error> eval:1: bad argument #1 to 'Counter.__add' (Counter expected, got number)");
        EQUALS(Eval(state.L, "a - b"), "<error> eval:1: attempt to perform arithmetic on a Counter value (global 'a')");
    });

    IT("compares with Eq and never raises from a comparison", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "a = Counter.new(2) b = Counter.new(3)"), "nil");
        EQUALS(Eval(state.L, "a == b"), "false");
        EQUALS(Eval(state.L, "a == Counter.new(2)"), "true");
        EQUALS(Eval(state.L, "a ~= Counter.new(2)"), "false");
        EQUALS(Eval(state.L, "a == a"), "true");
        EQUALS(Eval(state.L, "a == Tracked.new(2)"), "false");
        EQUALS(Eval(state.L, "a == 2"), "false");
    });

    IT("tries the functions declared for one event in order", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "a = Counter.new(2)"), "nil");
        EQUALS(Eval(state.L, "(a * 3):get()"), "6");
        EQUALS(Eval(state.L, "(4 * a):get()"), "8");
        EQUALS(Eval(state.L, "a * 'x'"), "<error> eval:1: bad argument #2 to 'Counter.__mul' (integer expected, got string)");
    });

    // --- Inheritance ---

    IT("gives a derived class the methods, properties and operators of its base", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "s = SubCounter.new(2)"), "nil");
        EQUALS(Eval(state.L, "s:add(3):get()"), "5");
        EQUALS(Eval(state.L, "rawequal(s, s:add(0))"), "true");
        EQUALS(Eval(state.L, "s:twice()"), "10");
        EQUALS(Eval(state.L, "s.value"), "5");
        EQUALS(Eval(state.L, "s.value = 7 return s.doubled"), "14");
        EQUALS(Eval(state.L, "tostring(s)"), "Counter(7)");
        EQUALS(Eval(state.L, "(s + s):get()"), "14");
        EQUALS(Eval(state.L, "s == Counter.new(7)"), "true");
        EQUALS(Eval(state.L, "s.nope"), "nil");
        EQUALS(Eval(state.L, "s.doubled = 1"), "<error> eval:1: attempt to set 'doubled' on SubCounter");
    });

    IT("lets a derived class replace a method of its base", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "Counter.new(1):kind()"), "counter");
        EQUALS(Eval(state.L, "SubCounter.new(1):kind()"), "sub");
    });

    IT("accepts a derived instance where its base is expected and not the reverse", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "c = Counter.new(1) s = SubCounter.new(2)"), "nil");
        EQUALS(Eval(state.L, "c:plus(s)"), "3");
        EQUALS(Eval(state.L, "s:plus(c)"), "3");
        EQUALS(Eval(state.L, "s:absorb(SubCounter.new(4))"), "6");
        EQUALS(Eval(state.L, "s:absorb(c)"), "<error> eval:1: bad argument #1 to 'SubCounter:absorb' (SubCounter expected, got Counter)");
        EQUALS(Eval(state.L, "s.twice(c)"), "<error> eval:1: bad receiver for 'SubCounter:twice' (SubCounter expected, got Counter)");
        EQUALS(Eval(state.L, "c.add(s, 1):get()"), "7");
        EQUALS(Eval(state.L, "Counter.bump(s) return s:get()"), "8");
    });

    IT("does not give a derived class the statics or the constructor of its base", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "SubCounter.make"), "nil");
        EQUALS(Eval(state.L, "type(SubCounter.new)"), "function");
        EQUALS(Eval(state.L, "SubCounter.LIMIT"), "nil");
    });

    IT("reaches the base part of a derived object wherever it lies in it", {
        LuaTest::State state;
        lua_State *L = state.L;
        Open(L);

        LuaConvert<SubCounter>::Push(L, SubCounter(6));
        EQUALS(LuaConvert<Counter>::Is(L, 1), true);
        EQUALS(LuaConvert<const Counter &>::Get(L, 1).get(), 6);
        const SubCounter &derived = LuaConvert<const SubCounter &>::Get(L, 1);
        EQUALS(static_cast<const void *>(&LuaConvert<const Counter &>::Get(L, 1)) == static_cast<const void *>(static_cast<const Counter *>(&derived)), true);
        EQUALS(static_cast<const void *>(&LuaConvert<const Counter &>::Get(L, 1)) == static_cast<const void *>(&derived), false);
        STREQUALS(LuaTypeNameAt(L, 1), "SubCounter");
    });

    // --- Sides ---

    IT("materialises a sided member only in a runtime of its side", {
        LuaTest::State server;
        Open(server.L, Side::Server);
        EQUALS(Eval(server.L, "Counter.new(1):kick('reason')"), "true");
        EQUALS(Eval(server.L, "Counter.restricted()"), "1");
        EQUALS(Eval(server.L, "Counter.new(1).client"), "nil");
        EQUALS(Eval(server.L, "SubCounter.new(1):kick('reason')"), "true");

        LuaTest::State client;
        Open(client.L, Side::Client);
        EQUALS(Eval(client.L, "Counter.new(1).kick"), "nil");
        EQUALS(Eval(client.L, "Counter.restricted"), "nil");
        EQUALS(Eval(client.L, "Counter.new(1).client"), "1");
        EQUALS(Eval(client.L, "SubCounter.new(1).kick"), "nil");
        EQUALS(Eval(client.L, "Counter.new(1):add(1):get()"), "2");
    });

    // --- Lifetime ---

    IT("destroys a non-trivially-destructible object when its userdata is collected", {
        Tracked::alive = 0;
        {
            LuaTest::State state;
            Open(state.L);

            EQUALS(Eval(state.L, "kept = Tracked.new(1) return kept:id()"), "1");
            EQUALS(Tracked::alive, 1);
            EQUALS(Eval(state.L, "kept = nil collectgarbage() return 1"), "1");
            EQUALS(Tracked::alive, 0);

            EQUALS(Eval(state.L, "kept = Tracked.new(2)"), "nil");
            EQUALS(Tracked::alive, 1);
        }
        EQUALS(Tracked::alive, 0);
    });

    IT("gives __gc only to a class with a non-trivial destructor", {
        LuaTest::State state;
        lua_State *L = state.L;
        Open(L);

        LuaConvert<Tracked>::Push(L, Tracked(1));
        EQUALS(BindClassLuaTest::HasMetafield(L, "__gc"), true);
        LuaConvert<Counter>::Push(L, Counter(1));
        EQUALS(BindClassLuaTest::HasMetafield(L, "__gc"), false);
        EQUALS(BindClassLuaTest::HasMetafield(L, "__index"), true);
    });

    IT("stops treating an object a finalizer brought back as a bound object", {
        Tracked::alive = 0;
        LuaTest::State state;
        Open(state.L);

        // The table is finalized first and saves the object, which is destroyed right after.
        EQUALS(Eval(state.L, "do local t = Tracked.new(5) setmetatable({}, { __gc = function() saved = t end }) end collectgarbage() return type(saved)"), "userdata");
        EQUALS(Tracked::alive, 0);
        EQUALS(Eval(state.L, "saved:id()"), "<error> eval:1: attempt to index a userdata value (global 'saved')");
        EQUALS(Eval(state.L, "Tracked.idOf(saved)"), "<error> eval:1: bad argument #1 to 'Tracked.idOf' (Tracked expected, got userdata)");
        EQUALS(Eval(state.L, "saved = nil collectgarbage() return 1"), "1");
        EQUALS(Tracked::alive, 0);
    });

    // --- Materialisation ---

    IT("materialises once per state and leaves the stack as it found it", {
        LuaTest::State state;
        lua_State *L   = state.L;
        const int runs = BindClassLuaTest::hookRuns;

        lua_pushinteger(L, 42);
        Open(L);
        EQUALS(lua_gettop(L), 1);
        EQUALS(BindClassLuaTest::hookRuns, runs + 1);
        EQUALS(Eval(L, "first = Counter"), "nil");

        Open(L, Side::Client);
        EQUALS(lua_gettop(L), 1);
        EQUALS(BindClassLuaTest::hookRuns, runs + 1);
        EQUALS(Eval(L, "rawequal(first, Counter)"), "true");
        // The side of the first materialisation stays.
        EQUALS(Eval(L, "Counter.new(1):kick('reason')"), "true");

        LuaTest::State other;
        Open(other.L);
        EQUALS(BindClassLuaTest::hookRuns, runs + 2);
        EQUALS(Eval(other.L, "Counter.new(2):get()"), "2");
    });

    IT("materialises the bases of a class before the class", {
        LuaTest::State state;
        BindClassLuaTest::Publish(state.L, SubCounterDefinition(), Side::Server);

        EQUALS(Eval(state.L, "Counter"), "nil");
        EQUALS(Eval(state.L, "SubCounter.new(2):add(1):get()"), "3");
        // The base is materialised, so a method that returns one can push it.
        EQUALS(Eval(state.L, "SubCounter.new(2):clone():get()"), "2");
    });

    IT("shares a class between the coroutines of a state", {
        LuaTest::State state;
        Open(state.L);

        EQUALS(Eval(state.L, "coroutine.wrap(function() return Counter.new(2):add(1):get() end)()"), "3");
        EQUALS(Eval(state.L, "local c = Counter.new(1) return coroutine.wrap(function() return c:add(1):get() end)()"), "2");
    });

    IT("refuses to publish a class that was not materialised", {
        LuaTest::State state;
        lua_pushglobaltable(state.L);
        const std::string message = LogicErrorOf([&state] {
            CounterDefinition().PublishLua(state.L, -1);
        });
        EQUALS(message.find("'Counter'") != std::string::npos, true);
        EQUALS(lua_gettop(state.L), 1);
    });

    IT("runs a LuaHook on the metatable and the class table", {
        LuaTest::State state;
        lua_State *L = state.L;
        Open(L);

        EQUALS(Eval(L, "Counter.LIMIT"), "99");
        LuaConvert<Counter>::Push(L, Counter(1));
        EQUALS(luaL_getmetafield(L, -1, "hook"), LUA_TSTRING);
        STREQUALS(lua_tostring(L, -1), "hooked");
    });

    // --- The definition ---

    IT("describes the class it binds", {
        const ClassDesc &desc = CounterDefinition().Describe();
        EQUALS(desc.name, "Counter");
        EQUALS(desc.description, "Counts up and down.");
        EQUALS(desc.isNamespace, false);
        EQUALS(desc.bases.empty(), true);

        EQUALS(desc.constructor.has_value(), true);
        EQUALS(desc.constructor->name, "constructor");
        EQUALS(desc.constructor->params.size(), 1u);
        EQUALS(desc.constructor->params[0].name, "value");
        EQUALS(desc.constructor->params[0].type.kind, TypeKind::Integer);

        const FunctionDesc *add = BindClassLuaTest::FindFunction(desc, "add");
        EQUALS(add != nullptr, true);
        EQUALS(add->description, "Adds to the counter.");
        EQUALS(add->params.size(), 1u);
        EQUALS(add->params[0].name, "amount");
        EQUALS(add->returns.kind, TypeKind::This);
        EQUALS(add->returnsDescription, "This counter.");
        EQUALS(add->isStatic, false);
        EQUALS(add->side, Side::Shared);

        EQUALS(BindClassLuaTest::FindFunction(desc, "kick")->side, Side::Server);
        EQUALS(BindClassLuaTest::FindFunction(desc, "scaled")->params[1].optional, true);

        const FunctionDesc *make = BindClassLuaTest::FindFunction(desc, "make");
        EQUALS(make->isStatic, true);
        EQUALS(make->params.size(), 1u);
        EQUALS(make->returns.kind, TypeKind::Class);
        EQUALS(make->returns.name, "Counter");

        const PropertyDesc *value = BindClassLuaTest::FindProperty(desc, "value");
        EQUALS(value != nullptr, true);
        EQUALS(value->type.kind, TypeKind::Integer);
        EQUALS(value->description, "Current value.");
        EQUALS(value->readonly, false);
        EQUALS(value->isStatic, false);
        EQUALS(BindClassLuaTest::FindProperty(desc, "doubled")->readonly, true);
        EQUALS(BindClassLuaTest::FindProperty(desc, "negated")->readonly, false);
        EQUALS(BindClassLuaTest::FindProperty(desc, "client")->side, Side::Client);

        EQUALS(SubCounterDefinition().Describe().bases, (std::vector<std::string> {"Counter"}));
    });

    IT("does not describe the receiver a function or a lambda takes first", {
        const ClassDesc &desc = CounterDefinition().Describe();

        const FunctionDesc *reset = BindClassLuaTest::FindFunction(desc, "reset");
        EQUALS(reset->params.empty(), true);
        EQUALS(reset->returns.kind, TypeKind::Void);
        EQUALS(reset->isStatic, false);

        const FunctionDesc *addTwice = BindClassLuaTest::FindFunction(desc, "addTwice");
        EQUALS(addTwice->params.size(), 1u);
        EQUALS(addTwice->params[0].name, "amount");
        EQUALS(addTwice->returns.kind, TypeKind::This);
    });

    IT("describes nothing for LuaMeta and LuaHook", {
        const ClassDesc &desc = CounterDefinition().Describe();
        for (const FunctionDesc &function : desc.functions) {
            EQUALS(function.name.rfind("__", 0) == 0, false);
        }
        EQUALS(BindClassLuaTest::FindFunction(desc, "LIMIT") == nullptr, true);
    });

    IT("records the definition in the catalog and finds it by its C++ type", {
        const ClassDesc &desc = CounterDefinition().Describe();
        EQUALS(ApiCatalog::Get().Find("Counter") == &desc, true);

        const Class<Counter> *found = Class<Counter>::Find();
        EQUALS(found != nullptr, true);
        EQUALS(&found->Describe() == &desc, true);
        EQUALS(Class<BindClassLuaTest::Orphan>::Find() == nullptr, true);

        // A copy is the same definition.
        const Class<Counter> copy = CounterDefinition();
        EQUALS(&copy.Describe() == &desc, true);
        EQUALS(&Class<Counter>::Find()->Describe() == &desc, true);
    });

    IT("refuses a second definition of a C++ type", {
        const std::string message = LogicErrorOf([] {
            Class<Counter>("Counter", "Again.");
        });
        EQUALS(message.find("'Counter'") != std::string::npos, true);
        EQUALS(&Class<Counter>::Find()->Describe() == &CounterDefinition().Describe(), true);

        EQUALS(LogicErrorOf([] {
            Class<BindClassLuaTest::Twice>("Twice", "First.");
        }),
            "");
        EQUALS(LogicErrorOf([] {
            Class<BindClassLuaTest::Twice>("Twice", "Second.");
        }).empty(),
            false);
    });

    IT("refuses a name other than the one FW_SCRIPT_CLASS gave the type", {
        const std::string message = LogicErrorOf([] {
            Class<BindClassLuaTest::Misnamed>("Other", "Named differently.");
        });
        EQUALS(message.find("'Other'") != std::string::npos, true);
        EQUALS(message.find("'Misnamed'") != std::string::npos, true);
        EQUALS(Class<BindClassLuaTest::Misnamed>::Find() == nullptr, true);
        EQUALS(ApiCatalog::Get().Find("Other") == nullptr, true);
    });

    IT("refuses to inherit a base that has no definition", {
        Class<BindClassLuaTest::OrphanChild> definition("OrphanChild", "Its base is not bound.");
        const std::string message = LogicErrorOf([&definition] {
            definition.Inherit<BindClassLuaTest::Orphan>();
        });
        EQUALS(message.find("'OrphanChild'") != std::string::npos, true);
        EQUALS(definition.Describe().bases.empty(), true);
    });

    IT("names the class and the member when a Doc and a signature disagree", {
        Class<BindClassLuaTest::Mismatch> definition("Mismatch", "Its documentation is wrong.");

        const std::string method = LogicErrorOf([&definition] {
            definition.Method("grow", &BindClassLuaTest::Mismatch::grow, Doc("Grows."));
        });
        EQUALS(method.find("'Mismatch.grow'") != std::string::npos, true);

        const std::string lambda = LogicErrorOf([&definition] {
            definition.Method("shrink", [](BindClassLuaTest::Mismatch &) {}, Doc("Shrinks.").Param("amount", "Not a parameter."));
        });
        EQUALS(lambda.find("'Mismatch.shrink'") != std::string::npos, true);

        const std::string function = LogicErrorOf([&definition] {
            definition.Static("make", [](int) {}, Doc("Makes."));
        });
        EQUALS(function.find("'Mismatch.make'") != std::string::npos, true);

        const std::string constructor = LogicErrorOf([&definition] {
            definition.Constructor<int>(Doc("Creates."));
        });
        EQUALS(constructor.find("'Mismatch.constructor'") != std::string::npos, true);

        EQUALS(definition.Describe().functions.empty(), true);
        EQUALS(definition.Describe().constructor.has_value(), false);
    });

    IT("refuses a second constructor and an operator function of the wrong arity", {
        Class<BindClassLuaTest::Arity> definition("Arity", "Declares things twice.");

        definition.Constructor<int>(Doc("Creates.").Param("size", "Initial size."));
        const std::string constructor = LogicErrorOf([&definition] {
            definition.Constructor<int>(Doc("Creates.").Param("size", "Initial size."));
        });
        EQUALS(constructor.find("'Arity'") != std::string::npos, true);

        const std::string unary = LogicErrorOf([&definition] {
            definition.LuaMeta(LuaMetaEvent::Unm, [](const BindClassLuaTest::Arity &, const BindClassLuaTest::Arity &) {
                return 0;
            });
        });
        EQUALS(unary.find("'Arity.__unm'") != std::string::npos, true);

        const std::string binary = LogicErrorOf([&definition] {
            definition.LuaMeta(LuaMetaEvent::Add, [](const BindClassLuaTest::Arity &) {
                return 0;
            });
        });
        EQUALS(binary.find("'Arity.__add'") != std::string::npos, true);
    });
});
