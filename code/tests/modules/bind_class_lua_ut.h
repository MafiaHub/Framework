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

        Token &touch() {
            ++*_value;
            return *this;
        }

        // A reference to another token, which a script cannot receive: there is no copy to give it.
        Token &pass(Token &other) {
            return other;
        }

        Token *maybe(bool keep) {
            return keep ? this : nullptr;
        }

      private:
        std::unique_ptr<int> _value;
    };

    // Has no script constructor.
    struct Sealed final {
        int value = 3;
    };

    struct Orphan {};
    // A class of the server, and classes that would inherit it.
    struct ServerOnly {};
    struct ServerChild final: ServerOnly {};
    struct ClientChild final: ServerOnly {};
    struct SharedChild final: ServerOnly {};
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

    struct Repeated final {
        int size = 0;

        void go() {}

        int getSize() const {
            return size;
        }

        void setSize(int value) {
            size = value;
        }
    };

} // namespace BindClassLuaTest

FW_SCRIPT_CLASS(BindClassLuaTest::Counter, "Counter");
FW_SCRIPT_CLASS(BindClassLuaTest::SubCounter, "SubCounter");
FW_SCRIPT_CLASS(BindClassLuaTest::Tracked, "Tracked");
FW_SCRIPT_CLASS(BindClassLuaTest::Token, "Token");
FW_SCRIPT_CLASS(BindClassLuaTest::Sealed, "Sealed");
FW_SCRIPT_CLASS(BindClassLuaTest::Orphan, "Orphan");
FW_SCRIPT_CLASS(BindClassLuaTest::ServerOnly, "ServerOnly");
FW_SCRIPT_CLASS(BindClassLuaTest::ServerChild, "ServerChild");
FW_SCRIPT_CLASS(BindClassLuaTest::ClientChild, "ClientChild");
FW_SCRIPT_CLASS(BindClassLuaTest::SharedChild, "SharedChild");
FW_SCRIPT_CLASS(BindClassLuaTest::OrphanChild, "OrphanChild");
FW_SCRIPT_CLASS(BindClassLuaTest::Misnamed, "Misnamed");
FW_SCRIPT_CLASS(BindClassLuaTest::Mismatch, "Mismatch");
FW_SCRIPT_CLASS(BindClassLuaTest::Twice, "Twice");
FW_SCRIPT_CLASS(BindClassLuaTest::Arity, "Arity");
FW_SCRIPT_CLASS(BindClassLuaTest::Repeated, "Repeated");

namespace BindClassLuaTest {
    inline int hookRuns = 0;

    inline const Framework::Scripting::Bind::Class<Counter> &CounterDefinition() {
        static const auto definition = Framework::Scripting::Bind::Class<Counter>("Counter", "Counts up and down.")
                                           .Constructor<int>(Framework::Scripting::Bind::Doc("Creates a counter.").Param("value", "Initial value."))
                                           .Method("add", &Counter::add, Framework::Scripting::Bind::Doc("Adds to the counter.").Param("amount", "Amount to add.").Returns("This counter."))
                                           .Method("get", &Counter::get, Framework::Scripting::Bind::Doc("Reads the counter.").Returns("Current value."))
                                           .Method("toString", &Counter::toString, Framework::Scripting::Bind::Doc("Formats the counter."))
                                           .Method("throws", &Counter::throws, Framework::Scripting::Bind::Doc("Always fails."))
                                           .Method("kick", &Counter::kick, Framework::Scripting::Bind::Doc("Kicks the counter.").Param("reason", "Why."), Framework::Scripting::Bind::Side::Server)
                                           .Method("kind", &Counter::kind, Framework::Scripting::Bind::Doc("Names the class."))
                                           .Method("plus", &Counter::plus, Framework::Scripting::Bind::Doc("Adds two counters.").Param("other", "Counter to add."))
                                           .Method("scaled", &Counter::scaled, Framework::Scripting::Bind::Doc("Scales the counter.").Param("factor", "Multiplier.").Param("offset", "Added after scaling."))
                                           .Method("clone", &Counter::clone, Framework::Scripting::Bind::Doc("Copies the counter."))
                                           .Method("pick", &Counter::pick, Framework::Scripting::Bind::Doc("Picks a counter.").Param("other", "Another counter.").Param("takeOther", "Whether to return it."))
                                           .Method("view", &Counter::view, Framework::Scripting::Bind::Doc("Returns the counter."))
                                           .Method("self", &Counter::self, Framework::Scripting::Bind::Doc("Returns the counter."))
                                           .Method(
                                               "reset",
                                               [](Counter &counter) {
                                                   counter.set(0);
                                               },
                                               Framework::Scripting::Bind::Doc("Zeroes the counter."))
                                           .Method("addTwice", &AddTwice, Framework::Scripting::Bind::Doc("Adds an amount twice.").Param("amount", "Amount to add."))
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
                                           .Property("client", &Counter::get, "Only on the client.", Framework::Scripting::Bind::Side::Client)
                                           .Property(
                                               "fragile",
                                               [](const Counter &) -> int {
                                                   throw std::runtime_error("getter failed");
                                               },
                                               [](Counter &, int) {
                                                   throw std::runtime_error("setter failed");
                                               },
                                               "Always fails.")
                                           .Static("make", &Counter::make, Framework::Scripting::Bind::Doc("Makes a counter.").Param("value", "Initial value."))
                                           .Static(
                                               "sum",
                                               [](const std::vector<Counter> &counters) {
                                                   int total = 0;
                                                   for (const Counter &counter : counters) {
                                                       total += counter.get();
                                                   }
                                                   return total;
                                               },
                                               Framework::Scripting::Bind::Doc("Adds counters up.").Param("counters", "Counters to add."))
                                           .Static(
                                               "maybe",
                                               [](std::optional<Counter> counter) {
                                                   return counter;
                                               },
                                               Framework::Scripting::Bind::Doc("Returns what it was given.").Param("counter", "A counter, or nothing."))
                                           .Static(
                                               "bump",
                                               [](Counter *counter) {
                                                   counter->add(1);
                                               },
                                               Framework::Scripting::Bind::Doc("Adds one to a counter.").Param("counter", "Counter to change."))
                                           .Static(
                                               "fail",
                                               []() -> int {
                                                   throw std::out_of_range("static failed");
                                               },
                                               Framework::Scripting::Bind::Doc("Always fails."))
                                           .Static(
                                               "restricted",
                                               [] {
                                                   return 1;
                                               },
                                               Framework::Scripting::Bind::Doc("Only on the server."), Framework::Scripting::Bind::Side::Server)
                                           .LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Add, &AddCounters)
                                           .LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Eq,
                                               [](const Counter &a, const Counter &b) {
                                                   return a.get() == b.get();
                                               })
                                           .LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Lt,
                                               [](const Counter &a, const Counter &b) {
                                                   return a.get() < b.get();
                                               })
                                           .LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Unm,
                                               [](const Counter &a) {
                                                   return Counter(-a.get());
                                               })
                                           .LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Len,
                                               [](const Counter &a) {
                                                   return a.get();
                                               })
                                           .LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Div,
                                               [](const Counter &, const Counter &) -> Counter {
                                                   throw std::domain_error("operator failed");
                                               })
                                           .LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Mul,
                                               [](const Counter &a, int factor) {
                                                   return Counter(a.get() * factor);
                                               })
                                           .LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Mul,
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

    inline const Framework::Scripting::Bind::Class<SubCounter> &SubCounterDefinition() {
        CounterDefinition();
        static const auto definition = Framework::Scripting::Bind::Class<SubCounter>("SubCounter", "A counter that does more.")
                                           .Inherit<Counter>()
                                           .Constructor<int>(Framework::Scripting::Bind::Doc("Creates a sub counter.").Param("value", "Initial value."))
                                           .Method("twice", &SubCounter::twice, Framework::Scripting::Bind::Doc("Doubles the value."))
                                           .Method("absorb", &SubCounter::absorb, Framework::Scripting::Bind::Doc("Takes the value of another.").Param("other", "Sub counter to take from."))
                                           .Method("kind", &SubCounter::kind, Framework::Scripting::Bind::Doc("Names the class."));
        return definition;
    }

    inline const Framework::Scripting::Bind::Class<Tracked> &TrackedDefinition() {
        static const auto definition = Framework::Scripting::Bind::Class<Tracked>("Tracked", "Counts its instances.")
                                           .Constructor<int>(Framework::Scripting::Bind::Doc("Creates a tracked value.").Param("id", "Identifier, not negative."))
                                           .Method("id", &Tracked::id, Framework::Scripting::Bind::Doc("Reads the identifier."))
                                           .Static(
                                               "idOf",
                                               [](const Tracked &tracked) {
                                                   return tracked.id();
                                               },
                                               Framework::Scripting::Bind::Doc("Reads the identifier of a value.").Param("tracked", "Value to read."));
        return definition;
    }

    inline const Framework::Scripting::Bind::Class<Token> &TokenDefinition() {
        static const auto definition = Framework::Scripting::Bind::Class<Token>("Token", "Cannot be copied.")
                                           .Constructor<int>(Framework::Scripting::Bind::Doc("Creates a token.").Param("value", "Its value."))
                                           .Method("value", &Token::value, Framework::Scripting::Bind::Doc("Reads the value."))
                                           .Static(
                                               "make",
                                               [](int value) {
                                                   return Token(value);
                                               },
                                               Framework::Scripting::Bind::Doc("Makes a token.").Param("value", "Its value."))
                                           .Method("touch", &Token::touch, Framework::Scripting::Bind::Doc("Adds one to the value.").Returns("This token."))
                                           .Method("pass", &Token::pass, Framework::Scripting::Bind::Doc("Returns a token.").Param("other", "Token to return."))
                                           .Method("maybe", &Token::maybe, Framework::Scripting::Bind::Doc("Returns this token or nothing.").Param("keep", "Whether to return it."))
                                           .Static(
                                               "same",
                                               [](Token &token) -> Token & {
                                                   return token;
                                               },
                                               Framework::Scripting::Bind::Doc("Returns a token.").Param("token", "Token to return."));
        return definition;
    }

    inline const Framework::Scripting::Bind::Class<Sealed> &SealedDefinition() {
        static const auto definition = Framework::Scripting::Bind::Class<Sealed>("Sealed", "Cannot be built by a script.")
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
                                               Framework::Scripting::Bind::Doc("Makes a sealed value."))
                                           .Static(
                                               "counter",
                                               [] {
                                                   return Counter(1);
                                               },
                                               Framework::Scripting::Bind::Doc("Makes a counter."));
        return definition;
    }

    // Materialises `definition` and publishes it as a global.
    template <typename T>
    void Publish(lua_State *L, const Framework::Scripting::Bind::Class<T> &definition, Framework::Scripting::Bind::Side side) {
        definition.MaterialiseLua(L, side);
        lua_pushglobaltable(L);
        definition.PublishLua(L, -1);
        lua_pop(L, 1);
    }

    inline void Open(lua_State *L, Framework::Scripting::Bind::Side side = Framework::Scripting::Bind::Side::Server) {
        Publish(L, CounterDefinition(), side);
        Publish(L, SubCounterDefinition(), side);
        Publish(L, TrackedDefinition(), side);
        Publish(L, TokenDefinition(), side);
        Publish(L, SealedDefinition(), side);
    }

    inline const Framework::Scripting::Bind::FunctionDesc *FindFunction(const Framework::Scripting::Bind::ClassDesc &desc, const std::string &name) {
        for (const Framework::Scripting::Bind::FunctionDesc &function : desc.functions) {
            if (function.name == name) {
                return &function;
            }
        }
        return nullptr;
    }

    inline const Framework::Scripting::Bind::PropertyDesc *FindProperty(const Framework::Scripting::Bind::ClassDesc &desc, const std::string &name) {
        for (const Framework::Scripting::Bind::PropertyDesc &property : desc.properties) {
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

    // --- Construction ---

    IT("constructs with new and by calling the class", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "Counter.new(2):add(3):get()"), "5");
        EQUALS(LuaTest::Eval(state.L, "Counter(2).value"), "2");
        EQUALS(LuaTest::Eval(state.L, "type(Counter.new(1))"), "userdata");
        EQUALS(LuaTest::Eval(state.L, "type(Counter)"), "table");
    });

    IT("checks the arguments of a constructor however it is called", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "Counter.new('2')"), "<error> eval:1: bad argument #1 to 'Counter.new' (integer expected, got string)");
        EQUALS(LuaTest::Eval(state.L, "Counter('2')"), "<error> eval:1: bad argument #1 to 'Counter.new' (integer expected, got string)");
        EQUALS(LuaTest::Eval(state.L, "Counter.new()"), "<error> eval:1: wrong number of arguments to 'Counter.new' (1..1 expected, got 0)");
        EQUALS(LuaTest::Eval(state.L, "Counter(1, 2)"), "<error> eval:1: wrong number of arguments to 'Counter.new' (1..1 expected, got 2)");
    });

    IT("refuses to construct a class that declares no constructor", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "Sealed()"), "<error> eval:1: Sealed cannot be constructed from script");
        EQUALS(LuaTest::Eval(state.L, "Sealed.new()"), "<error> eval:1: Sealed cannot be constructed from script");
        EQUALS(LuaTest::Eval(state.L, "Sealed.new(1, 2)"), "<error> eval:1: Sealed cannot be constructed from script");
        EQUALS(LuaTest::Eval(state.L, "type(Sealed.new)"), "function");
        EQUALS(LuaTest::Eval(state.L, "Sealed.make().value"), "3");
    });

    IT("turns an exception from a constructor into an error and destroys nothing", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        BindClassLuaTest::Tracked::alive = 0;
        EQUALS(LuaTest::Eval(state.L, "Tracked.new(-1)"), "<error> eval:1: Tracked: id must not be negative");
        EQUALS(LuaTest::Eval(state.L, "collectgarbage() return 1 + 1"), "2");
        EQUALS(BindClassLuaTest::Tracked::alive, 0);
    });

    // --- Methods ---

    IT("returns the receiver itself from a method that returns a reference to it", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(1)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "rawequal(c, c:add(1))"), "true");
        EQUALS(LuaTest::Eval(state.L, "c:get()"), "2");
        EQUALS(LuaTest::Eval(state.L, "rawequal(c, c:addTwice(2))"), "true");
        EQUALS(LuaTest::Eval(state.L, "c:get()"), "6");
        EQUALS(LuaTest::Eval(state.L, "rawequal(c, c:view())"), "true");
        EQUALS(LuaTest::Eval(state.L, "rawequal(c, c:self())"), "true");
        EQUALS(LuaTest::Eval(state.L, "rawequal(c, c:pick(Counter.new(9), false))"), "true");
    });

    IT("copies a returned value or a reference to another object into a new userdata", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(1) other = Counter.new(9)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "rawequal(c, c:clone())"), "false");
        EQUALS(LuaTest::Eval(state.L, "c:clone():get()"), "1");
        EQUALS(LuaTest::Eval(state.L, "picked = c:pick(other, true) return picked:get()"), "9");
        EQUALS(LuaTest::Eval(state.L, "rawequal(picked, other)"), "false");
        EQUALS(LuaTest::Eval(state.L, "picked:add(1) return other:get()"), "9");
    });

    IT("moves an object returned by value into its userdata", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "Token.make(4):value()"), "4");
        EQUALS(LuaTest::Eval(state.L, "Token.new(2):value()"), "2");
        EQUALS(LuaTest::Eval(state.L, "Token(3):value()"), "3");
    });

    IT("returns a reference to the receiver from a class that cannot be copied and refuses any other", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "t = Token.new(1)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "rawequal(t, t:touch())"), "true");
        EQUALS(LuaTest::Eval(state.L, "t:touch():touch():value()"), "4");
        EQUALS(LuaTest::Eval(state.L, "rawequal(t, t:pass(t))"), "true");
        EQUALS(LuaTest::Eval(state.L, "rawequal(t, t:maybe(true))"), "true");
        EQUALS(LuaTest::Eval(state.L, "t:maybe(false)"), "nil");

        EQUALS(LuaTest::Eval(state.L, "t:pass(Token.new(5))"), "<error> eval:1: 'Token:pass' cannot return a Token by reference: the class cannot be copied");
        EQUALS(LuaTest::Eval(state.L, "Token.same(t)"), "<error> eval:1: 'Token.same' cannot return a Token by reference: the class cannot be copied");
        EQUALS(LuaTest::Eval(state.L, "t:value()"), "4");
    });

    IT("calls a function or a lambda that takes the receiver first as a method", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(4) c:reset() return c:get()"), "0");
        EQUALS(LuaTest::Eval(state.L, "c:addTwice(3):get()"), "6");
        EQUALS(LuaTest::Eval(state.L, "c:addTwice('3')"), "<error> eval:1: bad argument #1 to 'Counter:addTwice' (integer expected, got string)");
        EQUALS(LuaTest::Eval(state.L, "c.reset(1)"), "<error> eval:1: bad receiver for 'Counter:reset' (Counter expected, got number)");
    });

    IT("lets trailing optional arguments be omitted", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(3)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "c:scaled(2)"), "6");
        EQUALS(LuaTest::Eval(state.L, "c:scaled(2, nil)"), "6");
        EQUALS(LuaTest::Eval(state.L, "c:scaled(2, 1)"), "7");
        EQUALS(LuaTest::Eval(state.L, "c:scaled()"), "<error> eval:1: wrong number of arguments to 'Counter:scaled' (1..2 expected, got 0)");
        EQUALS(LuaTest::Eval(state.L, "c:scaled(2, 1, 0)"), "<error> eval:1: wrong number of arguments to 'Counter:scaled' (1..2 expected, got 3)");
        EQUALS(LuaTest::Eval(state.L, "c:scaled(2, 'x')"), "<error> eval:1: bad argument #2 to 'Counter:scaled' (integer expected, got string)");
    });

    IT("raises on a wrong type, a missing or an extra argument", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(1)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "c:add('x')"), "<error> eval:1: bad argument #1 to 'Counter:add' (integer expected, got string)");
        EQUALS(LuaTest::Eval(state.L, "c:add(1.5)"), "<error> eval:1: bad argument #1 to 'Counter:add' (integer expected, got number)");
        EQUALS(LuaTest::Eval(state.L, "c:add(nil)"), "<error> eval:1: bad argument #1 to 'Counter:add' (integer expected, got nil)");
        EQUALS(LuaTest::Eval(state.L, "c:add()"), "<error> eval:1: wrong number of arguments to 'Counter:add' (1..1 expected, got 0)");
        EQUALS(LuaTest::Eval(state.L, "c:add(1, 2)"), "<error> eval:1: wrong number of arguments to 'Counter:add' (1..1 expected, got 2)");
        EQUALS(LuaTest::Eval(state.L, "c:get(1)"), "<error> eval:1: wrong number of arguments to 'Counter:get' (0..0 expected, got 1)");
        EQUALS(LuaTest::Eval(state.L, "c:plus(5)"), "<error> eval:1: bad argument #1 to 'Counter:plus' (Counter expected, got number)");
        EQUALS(LuaTest::Eval(state.L, "c:plus(Tracked.new(1))"), "<error> eval:1: bad argument #1 to 'Counter:plus' (Counter expected, got Tracked)");
        EQUALS(LuaTest::Eval(state.L, "c:get()"), "1");
    });

    IT("raises a receiver error when a method is called without its receiver", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(1)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "c.add(5, 1)"), "<error> eval:1: bad receiver for 'Counter:add' (Counter expected, got number)");
        EQUALS(LuaTest::Eval(state.L, "c.add()"), "<error> eval:1: bad receiver for 'Counter:add' (Counter expected, got no value)");
        EQUALS(LuaTest::Eval(state.L, "c.add(Tracked.new(1), 1)"), "<error> eval:1: bad receiver for 'Counter:add' (Counter expected, got Tracked)");
        EQUALS(LuaTest::Eval(state.L, "c.add(c, 1):get()"), "2");
    });

    IT("turns a C++ exception into an error and leaves the state usable", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(1)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "c:throws()"), "<error> eval:1: boom");
        EQUALS(LuaTest::Eval(state.L, "pcall(c.throws, c)"), "false");
        EQUALS(LuaTest::Eval(state.L, "select(2, pcall(c.throws, c))"), "boom");
        EQUALS(LuaTest::Eval(state.L, "1 + 1"), "2");
        EQUALS(LuaTest::Eval(state.L, "c:add(1):get()"), "2");
        EQUALS(lua_gettop(state.L), 0);
    });

    IT("turns an exception from a property, a static or an operator into an error too", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(1)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "c.fragile"), "<error> eval:1: getter failed");
        EQUALS(LuaTest::Eval(state.L, "c.fragile = 1"), "<error> eval:1: setter failed");
        EQUALS(LuaTest::Eval(state.L, "Counter.fail()"), "<error> eval:1: static failed");
        EQUALS(LuaTest::Eval(state.L, "c / c"), "<error> eval:1: operator failed");
        EQUALS(LuaTest::Eval(state.L, "c:get()"), "1");
    });

    // --- Properties ---

    IT("reads and writes a property", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(4)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "c.value"), "4");
        EQUALS(LuaTest::Eval(state.L, "c.value = 9 return c:get()"), "9");
        EQUALS(LuaTest::Eval(state.L, "c.doubled"), "18");
        EQUALS(LuaTest::Eval(state.L, "c.negated"), "-9");
        EQUALS(LuaTest::Eval(state.L, "c.negated = 5 return c.value"), "-5");
        EQUALS(LuaTest::Eval(state.L, "c.value = 'x'"), "<error> eval:1: bad argument #1 to 'Counter.value' (integer expected, got string)");
        EQUALS(LuaTest::Eval(state.L, "c.value"), "-5");
    });

    IT("raises on a write to a read-only property or an unknown key and reads an unknown key as nil", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(4)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "c.doubled = 1"), "<error> eval:1: attempt to set 'doubled' on Counter");
        EQUALS(LuaTest::Eval(state.L, "c.nope"), "nil");
        EQUALS(LuaTest::Eval(state.L, "c[1]"), "nil");
        EQUALS(LuaTest::Eval(state.L, "c.nope = 1"), "<error> eval:1: attempt to set 'nope' on Counter");
        EQUALS(LuaTest::Eval(state.L, "c[1] = 1"), "<error> eval:1: attempt to set '1' on Counter");
        EQUALS(LuaTest::Eval(state.L, "c.add = 1"), "<error> eval:1: attempt to set 'add' on Counter");
        EQUALS(LuaTest::Eval(state.L, "c.doubled"), "8");
    });

    IT("keeps the metatable and its metamethods out of reach", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(4)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "getmetatable(c) == false"), "true");
        EQUALS(LuaTest::Eval(state.L, "c.__index"), "nil");
        EQUALS(LuaTest::Eval(state.L, "c.__gc"), "nil");
        EQUALS(LuaTest::Eval(state.L, "c.__name"), "nil");
        EQUALS(LuaTest::Eval(state.L, "pcall(setmetatable, c, {})"), "false");
    });

    // --- Statics and the class table ---

    IT("calls a static through the class table", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "Counter.make(4):get()"), "4");
        EQUALS(LuaTest::Eval(state.L, "Counter.make('4')"), "<error> eval:1: bad argument #1 to 'Counter.make' (integer expected, got string)");
        EQUALS(LuaTest::Eval(state.L, "Counter.make()"), "<error> eval:1: wrong number of arguments to 'Counter.make' (1..1 expected, got 0)");
        EQUALS(LuaTest::Eval(state.L, "Counter:make(4)"), "<error> eval:1: wrong number of arguments to 'Counter.make' (1..1 expected, got 2)");
        EQUALS(LuaTest::Eval(state.L, "Counter.new(1).make"), "nil");
    });

    IT("keeps the class table read-only", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "Counter.new = nil"), "<error> eval:1: attempt to set 'new' on class Counter");
        EQUALS(LuaTest::Eval(state.L, "Counter.extra = 1"), "<error> eval:1: attempt to set 'extra' on class Counter");
        EQUALS(LuaTest::Eval(state.L, "rawget(Counter, 'new')"), "nil");
        EQUALS(LuaTest::Eval(state.L, "getmetatable(Counter) == false"), "true");
        EQUALS(LuaTest::Eval(state.L, "Counter.nope"), "nil");
        EQUALS(LuaTest::Eval(state.L, "Counter.new(2):get()"), "2");
    });

    IT("uses toString for tostring and names the class otherwise", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "tostring(Counter.new(5))"), "Counter(5)");
        EQUALS(LuaTest::Eval(state.L, "Counter.new(5):toString()"), "Counter(5)");
        EQUALS(LuaTest::Eval(state.L, "tostring(Tracked.new(1)):match('^Tracked: ') ~= nil"), "true");
    });

    // --- Conversions of a bound class ---

    IT("takes and returns bound objects inside vectors and optionals", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "Counter.sum({ Counter.new(1), Counter.new(2), SubCounter.new(3) })"), "6");
        EQUALS(LuaTest::Eval(state.L, "Counter.sum({})"), "0");
        EQUALS(LuaTest::Eval(state.L, "Counter.sum({ Counter.new(1), 2 })"), "<error> eval:1: bad argument #1 to 'Counter.sum' (table expected, got table)");
        EQUALS(LuaTest::Eval(state.L, "Counter.maybe()"), "nil");
        EQUALS(LuaTest::Eval(state.L, "Counter.maybe(nil)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "Counter.maybe(Counter.new(8)):get()"), "8");
        EQUALS(LuaTest::Eval(state.L, "Counter.maybe(8)"), "<error> eval:1: bad argument #1 to 'Counter.maybe' (Counter expected, got number)");
    });

    IT("passes a pointer or a reference into the userdata and refuses nil for a pointer", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(1) Counter.bump(c) return c:get()"), "2");
        EQUALS(LuaTest::Eval(state.L, "Counter.bump(nil)"), "<error> eval:1: bad argument #1 to 'Counter.bump' (Counter expected, got nil)");
    });

    IT("converts a bound object on the stack", {
        LuaTest::State state;
        lua_State *L = state.L;
        BindClassLuaTest::Open(L);

        Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter>::Push(L, BindClassLuaTest::Counter(3));
        EQUALS(lua_gettop(L), 1);
        EQUALS(lua_type(L, 1), LUA_TUSERDATA);
        STREQUALS(Framework::Scripting::Bind::LuaTypeNameAt(L, 1), "Counter");
        STREQUALS(Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter>::Expected(), "Counter");
        STREQUALS(Framework::Scripting::Bind::LuaConvert<const BindClassLuaTest::Counter &>::Expected(), "Counter");
        STREQUALS(Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter *>::Expected(), "Counter");

        EQUALS(Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter>::Is(L, 1), true);
        EQUALS(Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter>::Is(L, -1), true);
        EQUALS(Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::SubCounter>::Is(L, 1), false);
        EQUALS(Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Tracked>::Is(L, 1), false);
        EQUALS(Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter>::Is(L, 2), false);
        EQUALS(Framework::Scripting::Bind::LuaConvert<int>::Is(L, 1), false);

        // A copy leaves the userdata alone, a reference or a pointer reaches into it.
        BindClassLuaTest::Counter copy = Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter>::Get(L, 1);
        copy.add(1);
        EQUALS(Framework::Scripting::Bind::LuaConvert<const BindClassLuaTest::Counter &>::Get(L, 1).get(), 3);
        Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter &>::Get(L, 1).add(1);
        EQUALS(Framework::Scripting::Bind::LuaConvert<const BindClassLuaTest::Counter &>::Get(L, 1).get(), 4);
        Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter *>::Get(L, 1)->add(1);
        EQUALS(Framework::Scripting::Bind::LuaConvert<const BindClassLuaTest::Counter *>::Get(L, 1)->get(), 5);
        EQUALS(lua_gettop(L), 1);

        lua_pushnil(L);
        EQUALS(Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter *>::Is(L, 2), false);
        EQUALS(Framework::Scripting::Bind::LuaConvert<std::optional<BindClassLuaTest::Counter>>::Is(L, 2), true);
        Framework::Scripting::Bind::LuaConvert<const BindClassLuaTest::Counter *>::Push(L, nullptr);
        EQUALS(lua_type(L, 3), LUA_TNIL);

        lua_newuserdatauv(L, 4, 0);
        EQUALS(Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter>::Is(L, 4), false);
        STREQUALS(Framework::Scripting::Bind::LuaTypeNameAt(L, 4), "userdata");
        lua_pushlightuserdata(L, &copy);
        EQUALS(Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter>::Is(L, 5), false);
        EQUALS(lua_gettop(L), 5);
    });

    IT("raises when a class that was not materialised in the state is pushed", {
        LuaTest::State state;
        BindClassLuaTest::Publish(state.L, BindClassLuaTest::SealedDefinition(), Framework::Scripting::Bind::Side::Server);

        EQUALS(LuaTest::Eval(state.L, "Sealed.counter()"), "<error> eval:1: Counter is not available in this Lua state");
        EQUALS(LuaTest::Eval(state.L, "Sealed.make().value"), "3");
    });

    // --- Operators ---

    IT("gives the class the operators it declares with LuaMeta", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "a = Counter.new(2) b = Counter.new(3)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "(a + b):get()"), "5");
        EQUALS(LuaTest::Eval(state.L, "a:get()"), "2");
        EQUALS(LuaTest::Eval(state.L, "(-a):get()"), "-2");
        EQUALS(LuaTest::Eval(state.L, "#b"), "3");
        EQUALS(LuaTest::Eval(state.L, "a < b"), "true");
        EQUALS(LuaTest::Eval(state.L, "b < a"), "false");
        EQUALS(LuaTest::Eval(state.L, "a + 1"), "<error> eval:1: bad argument #2 to 'Counter.__add' (Counter expected, got number)");
        EQUALS(LuaTest::Eval(state.L, "1 + a"), "<error> eval:1: bad argument #1 to 'Counter.__add' (Counter expected, got number)");
        EQUALS(LuaTest::Eval(state.L, "a - b"), "<error> eval:1: attempt to perform arithmetic on a Counter value (global 'a')");
    });

    IT("compares with Eq and never raises from a comparison", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "a = Counter.new(2) b = Counter.new(3)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "a == b"), "false");
        EQUALS(LuaTest::Eval(state.L, "a == Counter.new(2)"), "true");
        EQUALS(LuaTest::Eval(state.L, "a ~= Counter.new(2)"), "false");
        EQUALS(LuaTest::Eval(state.L, "a == a"), "true");
        EQUALS(LuaTest::Eval(state.L, "a == Tracked.new(2)"), "false");
        EQUALS(LuaTest::Eval(state.L, "a == 2"), "false");
    });

    IT("tries the functions declared for one event in order", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "a = Counter.new(2)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "(a * 3):get()"), "6");
        EQUALS(LuaTest::Eval(state.L, "(4 * a):get()"), "8");
        EQUALS(LuaTest::Eval(state.L, "a * 'x'"), "<error> eval:1: bad argument #2 to 'Counter.__mul' (integer expected, got string)");
    });

    // --- Inheritance ---

    IT("gives a derived class the methods, properties and operators of its base", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "s = SubCounter.new(2)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "s:add(3):get()"), "5");
        EQUALS(LuaTest::Eval(state.L, "rawequal(s, s:add(0))"), "true");
        EQUALS(LuaTest::Eval(state.L, "s:twice()"), "10");
        EQUALS(LuaTest::Eval(state.L, "s.value"), "5");
        EQUALS(LuaTest::Eval(state.L, "s.value = 7 return s.doubled"), "14");
        EQUALS(LuaTest::Eval(state.L, "tostring(s)"), "Counter(7)");
        EQUALS(LuaTest::Eval(state.L, "(s + s):get()"), "14");
        EQUALS(LuaTest::Eval(state.L, "s == Counter.new(7)"), "true");
        EQUALS(LuaTest::Eval(state.L, "s.nope"), "nil");
        EQUALS(LuaTest::Eval(state.L, "s.doubled = 1"), "<error> eval:1: attempt to set 'doubled' on SubCounter");
    });

    IT("lets a derived class replace a method of its base", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "Counter.new(1):kind()"), "counter");
        EQUALS(LuaTest::Eval(state.L, "SubCounter.new(1):kind()"), "sub");
    });

    IT("accepts a derived instance where its base is expected and not the reverse", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "c = Counter.new(1) s = SubCounter.new(2)"), "nil");
        EQUALS(LuaTest::Eval(state.L, "c:plus(s)"), "3");
        EQUALS(LuaTest::Eval(state.L, "s:plus(c)"), "3");
        EQUALS(LuaTest::Eval(state.L, "s:absorb(SubCounter.new(4))"), "6");
        EQUALS(LuaTest::Eval(state.L, "s:absorb(c)"), "<error> eval:1: bad argument #1 to 'SubCounter:absorb' (SubCounter expected, got Counter)");
        EQUALS(LuaTest::Eval(state.L, "s.twice(c)"), "<error> eval:1: bad receiver for 'SubCounter:twice' (SubCounter expected, got Counter)");
        EQUALS(LuaTest::Eval(state.L, "c.add(s, 1):get()"), "7");
        EQUALS(LuaTest::Eval(state.L, "Counter.bump(s) return s:get()"), "8");
    });

    IT("does not give a derived class the statics or the constructor of its base", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "SubCounter.make"), "nil");
        EQUALS(LuaTest::Eval(state.L, "type(SubCounter.new)"), "function");
        EQUALS(LuaTest::Eval(state.L, "SubCounter.LIMIT"), "nil");
    });

    IT("reaches the base part of a derived object wherever it lies in it", {
        LuaTest::State state;
        lua_State *L = state.L;
        BindClassLuaTest::Open(L);

        Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::SubCounter>::Push(L, BindClassLuaTest::SubCounter(6));
        EQUALS(Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter>::Is(L, 1), true);
        EQUALS(Framework::Scripting::Bind::LuaConvert<const BindClassLuaTest::Counter &>::Get(L, 1).get(), 6);
        const BindClassLuaTest::SubCounter &derived = Framework::Scripting::Bind::LuaConvert<const BindClassLuaTest::SubCounter &>::Get(L, 1);
        EQUALS(static_cast<const void *>(&Framework::Scripting::Bind::LuaConvert<const BindClassLuaTest::Counter &>::Get(L, 1)) == static_cast<const void *>(static_cast<const BindClassLuaTest::Counter *>(&derived)), true);
        EQUALS(static_cast<const void *>(&Framework::Scripting::Bind::LuaConvert<const BindClassLuaTest::Counter &>::Get(L, 1)) == static_cast<const void *>(&derived), false);
        STREQUALS(Framework::Scripting::Bind::LuaTypeNameAt(L, 1), "SubCounter");
    });

    // --- Sides ---

    IT("materialises a sided member only in a runtime of its side", {
        LuaTest::State server;
        BindClassLuaTest::Open(server.L, Framework::Scripting::Bind::Side::Server);
        EQUALS(LuaTest::Eval(server.L, "Counter.new(1):kick('reason')"), "true");
        EQUALS(LuaTest::Eval(server.L, "Counter.restricted()"), "1");
        EQUALS(LuaTest::Eval(server.L, "Counter.new(1).client"), "nil");
        EQUALS(LuaTest::Eval(server.L, "SubCounter.new(1):kick('reason')"), "true");

        LuaTest::State client;
        BindClassLuaTest::Open(client.L, Framework::Scripting::Bind::Side::Client);
        EQUALS(LuaTest::Eval(client.L, "Counter.new(1).kick"), "nil");
        EQUALS(LuaTest::Eval(client.L, "Counter.restricted"), "nil");
        EQUALS(LuaTest::Eval(client.L, "Counter.new(1).client"), "1");
        EQUALS(LuaTest::Eval(client.L, "SubCounter.new(1).kick"), "nil");
        EQUALS(LuaTest::Eval(client.L, "Counter.new(1):add(1):get()"), "2");
    });

    // --- Lifetime ---

    IT("destroys a non-trivially-destructible object when its userdata is collected", {
        BindClassLuaTest::Tracked::alive = 0;
        {
            LuaTest::State state;
            BindClassLuaTest::Open(state.L);

            EQUALS(LuaTest::Eval(state.L, "kept = Tracked.new(1) return kept:id()"), "1");
            EQUALS(BindClassLuaTest::Tracked::alive, 1);
            EQUALS(LuaTest::Eval(state.L, "kept = nil collectgarbage() return 1"), "1");
            EQUALS(BindClassLuaTest::Tracked::alive, 0);

            EQUALS(LuaTest::Eval(state.L, "kept = Tracked.new(2)"), "nil");
            EQUALS(BindClassLuaTest::Tracked::alive, 1);
        }
        EQUALS(BindClassLuaTest::Tracked::alive, 0);
    });

    IT("gives __gc only to a class with a non-trivial destructor", {
        LuaTest::State state;
        lua_State *L = state.L;
        BindClassLuaTest::Open(L);

        Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Tracked>::Push(L, BindClassLuaTest::Tracked(1));
        EQUALS(BindClassLuaTest::HasMetafield(L, "__gc"), true);
        Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter>::Push(L, BindClassLuaTest::Counter(1));
        EQUALS(BindClassLuaTest::HasMetafield(L, "__gc"), false);
        EQUALS(BindClassLuaTest::HasMetafield(L, "__index"), true);
    });

    IT("stops treating an object a finalizer brought back as a bound object", {
        BindClassLuaTest::Tracked::alive = 0;
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        // The table is finalized first and saves the object, which is destroyed right after.
        EQUALS(LuaTest::Eval(state.L, "do local t = Tracked.new(5) setmetatable({}, { __gc = function() saved = t end }) end collectgarbage() return type(saved)"), "userdata");
        EQUALS(BindClassLuaTest::Tracked::alive, 0);
        EQUALS(LuaTest::Eval(state.L, "saved:id()"), "<error> eval:1: attempt to index a userdata value (global 'saved')");
        EQUALS(LuaTest::Eval(state.L, "Tracked.idOf(saved)"), "<error> eval:1: bad argument #1 to 'Tracked.idOf' (Tracked expected, got userdata)");
        EQUALS(LuaTest::Eval(state.L, "saved = nil collectgarbage() return 1"), "1");
        EQUALS(BindClassLuaTest::Tracked::alive, 0);
    });

    // --- Materialisation ---

    IT("materialises once per state and leaves the stack as it found it", {
        LuaTest::State state;
        lua_State *L   = state.L;
        const int runs = BindClassLuaTest::hookRuns;

        lua_pushinteger(L, 42);
        BindClassLuaTest::Open(L);
        EQUALS(lua_gettop(L), 1);
        EQUALS(BindClassLuaTest::hookRuns, runs + 1);
        EQUALS(LuaTest::Eval(L, "first = Counter"), "nil");

        BindClassLuaTest::Open(L, Framework::Scripting::Bind::Side::Client);
        EQUALS(lua_gettop(L), 1);
        EQUALS(BindClassLuaTest::hookRuns, runs + 1);
        EQUALS(LuaTest::Eval(L, "rawequal(first, Counter)"), "true");
        // The side of the first materialisation stays.
        EQUALS(LuaTest::Eval(L, "Counter.new(1):kick('reason')"), "true");

        LuaTest::State other;
        BindClassLuaTest::Open(other.L);
        EQUALS(BindClassLuaTest::hookRuns, runs + 2);
        EQUALS(LuaTest::Eval(other.L, "Counter.new(2):get()"), "2");
    });

    IT("materialises the bases of a class before the class", {
        LuaTest::State state;
        BindClassLuaTest::Publish(state.L, BindClassLuaTest::SubCounterDefinition(), Framework::Scripting::Bind::Side::Server);

        EQUALS(LuaTest::Eval(state.L, "Counter"), "nil");
        EQUALS(LuaTest::Eval(state.L, "SubCounter.new(2):add(1):get()"), "3");
        // The base is materialised, so a method that returns one can push it.
        EQUALS(LuaTest::Eval(state.L, "SubCounter.new(2):clone():get()"), "2");
    });

    IT("shares a class between the coroutines of a state", {
        LuaTest::State state;
        BindClassLuaTest::Open(state.L);

        EQUALS(LuaTest::Eval(state.L, "coroutine.wrap(function() return Counter.new(2):add(1):get() end)()"), "3");
        EQUALS(LuaTest::Eval(state.L, "local c = Counter.new(1) return coroutine.wrap(function() return c:add(1):get() end)()"), "2");
    });

    IT("refuses to publish a class that was not materialised", {
        LuaTest::State state;
        lua_pushglobaltable(state.L);
        const std::string message = BindClassLuaTest::LogicErrorOf([&state] {
            BindClassLuaTest::CounterDefinition().PublishLua(state.L, -1);
        });
        EQUALS(message.find("'Counter'") != std::string::npos, true);
        EQUALS(lua_gettop(state.L), 1);
    });

    IT("registers a class as a global of the state in one call", {
        LuaTest::State state;
        lua_State *L = state.L;

        lua_pushinteger(L, 42);
        BindClassLuaTest::SubCounterDefinition().RegisterLuaGlobal(L, Framework::Scripting::Bind::Side::Client);
        EQUALS(lua_gettop(L), 1);

        // The class is a global under its name, for the side it was given. Its base is materialised and is no global.
        EQUALS(LuaTest::Eval(L, "SubCounter.new(2):add(1):get()"), "3");
        EQUALS(LuaTest::Eval(L, "SubCounter.new(1).kick"), "nil");
        EQUALS(LuaTest::Eval(L, "Counter"), "nil");

        // Again, the class the state already has goes back under its name, whatever the side.
        EQUALS(LuaTest::Eval(L, "first = SubCounter SubCounter = nil"), "nil");
        BindClassLuaTest::SubCounterDefinition().RegisterLuaGlobal(L, Framework::Scripting::Bind::Side::Server);
        EQUALS(lua_gettop(L), 1);
        EQUALS(LuaTest::Eval(L, "rawequal(first, SubCounter)"), "true");
        EQUALS(LuaTest::Eval(L, "SubCounter.new(1).kick"), "nil");

        // It is published raw: globals that refuse a new name do not refuse the class.
        EQUALS(LuaTest::Eval(L, "setmetatable(_G, { __newindex = function() error('sealed') end }) Tracked = 1"), "<error> eval:1: sealed");
        BindClassLuaTest::TrackedDefinition().RegisterLuaGlobal(L, Framework::Scripting::Bind::Side::Server);
        EQUALS(lua_gettop(L), 1);
        EQUALS(LuaTest::Eval(L, "Tracked.new(1):id()"), "1");
    });

    IT("runs a LuaHook on the metatable and the class table", {
        LuaTest::State state;
        lua_State *L = state.L;
        BindClassLuaTest::Open(L);

        EQUALS(LuaTest::Eval(L, "Counter.LIMIT"), "99");
        Framework::Scripting::Bind::LuaConvert<BindClassLuaTest::Counter>::Push(L, BindClassLuaTest::Counter(1));
        EQUALS(luaL_getmetafield(L, -1, "hook"), LUA_TSTRING);
        STREQUALS(lua_tostring(L, -1), "hooked");
    });

    // --- The definition ---

    IT("describes the class it binds", {
        const Framework::Scripting::Bind::ClassDesc &desc = BindClassLuaTest::CounterDefinition().Describe();
        EQUALS(desc.name, "Counter");
        EQUALS(desc.description, "Counts up and down.");
        EQUALS(desc.isNamespace, false);
        EQUALS(desc.bases.empty(), true);

        EQUALS(desc.constructor.has_value(), true);
        EQUALS(desc.constructor->name, "constructor");
        EQUALS(desc.constructor->params.size(), 1u);
        EQUALS(desc.constructor->params[0].name, "value");
        EQUALS(desc.constructor->params[0].type.kind, Framework::Scripting::Bind::TypeKind::Integer);

        const Framework::Scripting::Bind::FunctionDesc *add = BindClassLuaTest::FindFunction(desc, "add");
        EQUALS(add != nullptr, true);
        EQUALS(add->description, "Adds to the counter.");
        EQUALS(add->params.size(), 1u);
        EQUALS(add->params[0].name, "amount");
        EQUALS(add->returns.kind, Framework::Scripting::Bind::TypeKind::This);
        EQUALS(add->returnsDescription, "This counter.");
        EQUALS(add->isStatic, false);
        EQUALS(add->side, Framework::Scripting::Bind::Side::Shared);

        EQUALS(BindClassLuaTest::FindFunction(desc, "kick")->side, Framework::Scripting::Bind::Side::Server);
        EQUALS(BindClassLuaTest::FindFunction(desc, "scaled")->params[1].optional, true);

        const Framework::Scripting::Bind::FunctionDesc *make = BindClassLuaTest::FindFunction(desc, "make");
        EQUALS(make->isStatic, true);
        EQUALS(make->params.size(), 1u);
        EQUALS(make->returns.kind, Framework::Scripting::Bind::TypeKind::Class);
        EQUALS(make->returns.name, "Counter");

        const Framework::Scripting::Bind::PropertyDesc *value = BindClassLuaTest::FindProperty(desc, "value");
        EQUALS(value != nullptr, true);
        EQUALS(value->type.kind, Framework::Scripting::Bind::TypeKind::Integer);
        EQUALS(value->description, "Current value.");
        EQUALS(value->readonly, false);
        EQUALS(value->isStatic, false);
        EQUALS(BindClassLuaTest::FindProperty(desc, "doubled")->readonly, true);
        EQUALS(BindClassLuaTest::FindProperty(desc, "negated")->readonly, false);
        EQUALS(BindClassLuaTest::FindProperty(desc, "client")->side, Framework::Scripting::Bind::Side::Client);

        EQUALS(BindClassLuaTest::SubCounterDefinition().Describe().bases, (std::vector<std::string> {"Counter"}));
    });

    IT("does not describe the receiver a function or a lambda takes first", {
        const Framework::Scripting::Bind::ClassDesc &desc = BindClassLuaTest::CounterDefinition().Describe();

        const Framework::Scripting::Bind::FunctionDesc *reset = BindClassLuaTest::FindFunction(desc, "reset");
        EQUALS(reset->params.empty(), true);
        EQUALS(reset->returns.kind, Framework::Scripting::Bind::TypeKind::Void);
        EQUALS(reset->isStatic, false);

        const Framework::Scripting::Bind::FunctionDesc *addTwice = BindClassLuaTest::FindFunction(desc, "addTwice");
        EQUALS(addTwice->params.size(), 1u);
        EQUALS(addTwice->params[0].name, "amount");
        EQUALS(addTwice->returns.kind, Framework::Scripting::Bind::TypeKind::This);
    });

    IT("describes nothing for LuaMeta and LuaHook", {
        const Framework::Scripting::Bind::ClassDesc &desc = BindClassLuaTest::CounterDefinition().Describe();
        for (const Framework::Scripting::Bind::FunctionDesc &function : desc.functions) {
            EQUALS(function.name.rfind("__", 0) == 0, false);
        }
        EQUALS(BindClassLuaTest::FindFunction(desc, "LIMIT") == nullptr, true);
    });

    IT("records the definition in the catalog and finds it by its C++ type", {
        const Framework::Scripting::Bind::ClassDesc &desc = BindClassLuaTest::CounterDefinition().Describe();
        EQUALS(Framework::Scripting::Bind::ApiCatalog::Get().Find("Counter") == &desc, true);

        const Framework::Scripting::Bind::Class<BindClassLuaTest::Counter> *found = Framework::Scripting::Bind::Class<BindClassLuaTest::Counter>::Find();
        EQUALS(found != nullptr, true);
        EQUALS(&found->Describe() == &desc, true);
        EQUALS(Framework::Scripting::Bind::Class<BindClassLuaTest::Orphan>::Find() == nullptr, true);

        // A copy is the same definition.
        const Framework::Scripting::Bind::Class<BindClassLuaTest::Counter> copy = BindClassLuaTest::CounterDefinition();
        EQUALS(&copy.Describe() == &desc, true);
        EQUALS(&Framework::Scripting::Bind::Class<BindClassLuaTest::Counter>::Find()->Describe() == &desc, true);
    });

    IT("refuses a second definition of a C++ type", {
        const std::string message = BindClassLuaTest::LogicErrorOf([] {
            Framework::Scripting::Bind::Class<BindClassLuaTest::Counter>("Counter", "Again.");
        });
        EQUALS(message.find("'Counter'") != std::string::npos, true);
        EQUALS(&Framework::Scripting::Bind::Class<BindClassLuaTest::Counter>::Find()->Describe() == &BindClassLuaTest::CounterDefinition().Describe(), true);

        EQUALS(BindClassLuaTest::LogicErrorOf([] {
            Framework::Scripting::Bind::Class<BindClassLuaTest::Twice>("Twice", "First.");
        }),
            "");
        EQUALS(BindClassLuaTest::LogicErrorOf([] {
            Framework::Scripting::Bind::Class<BindClassLuaTest::Twice>("Twice", "Second.");
        }).empty(),
            false);
    });

    IT("refuses a name other than the one FW_SCRIPT_CLASS gave the type", {
        const std::string message = BindClassLuaTest::LogicErrorOf([] {
            Framework::Scripting::Bind::Class<BindClassLuaTest::Misnamed>("Other", "Named differently.");
        });
        EQUALS(message.find("'Other'") != std::string::npos, true);
        EQUALS(message.find("'Misnamed'") != std::string::npos, true);
        EQUALS(Framework::Scripting::Bind::Class<BindClassLuaTest::Misnamed>::Find() == nullptr, true);
        EQUALS(Framework::Scripting::Bind::ApiCatalog::Get().Find("Other") == nullptr, true);
    });

    IT("refuses to inherit a base that has no definition", {
        Framework::Scripting::Bind::Class<BindClassLuaTest::OrphanChild> definition("OrphanChild", "Its base is not bound.");
        const std::string message = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Inherit<BindClassLuaTest::Orphan>();
        });
        EQUALS(message.find("'OrphanChild'") != std::string::npos, true);
        EQUALS(definition.Describe().bases.empty(), true);
    });

    // A runtime that has a class has its bases. bind_class_v8 goes on with these definitions.
    IT("lets a class of one side inherit a shared base or one of its side, and no other", {
        Framework::Scripting::Bind::Class<BindClassLuaTest::ServerOnly> base("ServerOnly", "Exists on the server.", Framework::Scripting::Bind::Side::Server);
        EQUALS(base.Describe().side == Framework::Scripting::Bind::Side::Server, true);
        EQUALS(BindClassLuaTest::CounterDefinition().Describe().side == Framework::Scripting::Bind::Side::Shared, true);

        Framework::Scripting::Bind::Class<BindClassLuaTest::ServerChild> same("ServerChild", "Of the server too.", Framework::Scripting::Bind::Side::Server);
        EQUALS(BindClassLuaTest::LogicErrorOf([&same] {
            same.Inherit<BindClassLuaTest::ServerOnly>();
        }),
            "");
        EQUALS(same.Describe().bases.size(), 1u);

        Framework::Scripting::Bind::Class<BindClassLuaTest::ClientChild> other("ClientChild", "Of the client.", Framework::Scripting::Bind::Side::Client);
        EQUALS(BindClassLuaTest::LogicErrorOf([&other] {
            other.Inherit<BindClassLuaTest::ServerOnly>();
        }),
            "Bind: 'ClientChild' inherits 'ServerOnly', which exists in a server runtime only, and declares no such side itself");
        EQUALS(other.Describe().bases.empty(), true);

        Framework::Scripting::Bind::Class<BindClassLuaTest::SharedChild> everywhere("SharedChild", "Everywhere.");
        EQUALS(BindClassLuaTest::LogicErrorOf([&everywhere] {
            everywhere.Inherit<BindClassLuaTest::ServerOnly>();
        }).find("'SharedChild' inherits 'ServerOnly'")
                   != std::string::npos,
            true);
        EQUALS(everywhere.Describe().bases.empty(), true);

        // In a state of the other side the class is not there, and registering it leaves no global.
        LuaTest::State client;
        same.RegisterLuaGlobal(client.L, Framework::Scripting::Bind::Side::Client);
        base.MaterialiseLua(client.L, Framework::Scripting::Bind::Side::Client);
        EQUALS(LuaTest::Eval(client.L, "type(ServerChild) .. type(ServerOnly)"), "nilnil");
        EQUALS(lua_gettop(client.L), 0);
        LuaTest::State server;
        same.RegisterLuaGlobal(server.L, Framework::Scripting::Bind::Side::Server);
        EQUALS(LuaTest::Eval(server.L, "type(ServerChild)"), "table");
        EQUALS(Framework::Scripting::Bind::ApiCatalog::Get().Find("ServerChild", Framework::Scripting::Bind::Side::Client) == nullptr, true);
        EQUALS(Framework::Scripting::Bind::ApiCatalog::Get().Find("ServerChild", Framework::Scripting::Bind::Side::Server) == &same.Describe(), true);
    });

    IT("names the class and the member when a Doc and a signature disagree", {
        Framework::Scripting::Bind::Class<BindClassLuaTest::Mismatch> definition("Mismatch", "Its documentation is wrong.");

        const std::string method = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Method("grow", &BindClassLuaTest::Mismatch::grow, Framework::Scripting::Bind::Doc("Grows."));
        });
        EQUALS(method.find("'Mismatch.grow'") != std::string::npos, true);

        const std::string lambda = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Method("shrink", [](BindClassLuaTest::Mismatch &) {}, Framework::Scripting::Bind::Doc("Shrinks.").Param("amount", "Not a parameter."));
        });
        EQUALS(lambda.find("'Mismatch.shrink'") != std::string::npos, true);

        const std::string function = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Static("make", [](int) {}, Framework::Scripting::Bind::Doc("Makes."));
        });
        EQUALS(function.find("'Mismatch.make'") != std::string::npos, true);

        const std::string constructor = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Constructor<int>(Framework::Scripting::Bind::Doc("Creates."));
        });
        EQUALS(constructor.find("'Mismatch.constructor'") != std::string::npos, true);

        EQUALS(definition.Describe().functions.empty(), true);
        EQUALS(definition.Describe().constructor.has_value(), false);
    });

    IT("refuses a second constructor and an operator function of the wrong arity", {
        Framework::Scripting::Bind::Class<BindClassLuaTest::Arity> definition("Arity", "Declares things twice.");

        definition.Constructor<int>(Framework::Scripting::Bind::Doc("Creates.").Param("size", "Initial size."));
        const std::string constructor = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Constructor<int>(Framework::Scripting::Bind::Doc("Creates.").Param("size", "Initial size."));
        });
        EQUALS(constructor.find("'Arity'") != std::string::npos, true);

        const std::string unary = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Unm, [](const BindClassLuaTest::Arity &, const BindClassLuaTest::Arity &) {
                return 0;
            });
        });
        EQUALS(unary.find("'Arity.__unm'") != std::string::npos, true);

        const std::string binary = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Add, [](const BindClassLuaTest::Arity &) {
                return 0;
            });
        });
        EQUALS(binary.find("'Arity.__add'") != std::string::npos, true);
    });

    IT("refuses a member declared twice, since a member cannot be overloaded", {
        Framework::Scripting::Bind::Class<BindClassLuaTest::Repeated> definition("Repeated", "Declares members twice.");
        definition.Method("go", &BindClassLuaTest::Repeated::go, Framework::Scripting::Bind::Doc("Goes."));
        definition.Property("size", &BindClassLuaTest::Repeated::getSize, &BindClassLuaTest::Repeated::setSize, "Its size.");
        definition.Static(
            "make",
            [] {
                return BindClassLuaTest::Repeated {};
            },
            Framework::Scripting::Bind::Doc("Makes one."));

        const std::string method = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Method("go", &BindClassLuaTest::Repeated::go, Framework::Scripting::Bind::Doc("Goes again."));
        });
        EQUALS(method.find("'Repeated.go'") != std::string::npos, true);

        const std::string property = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Property("size", &BindClassLuaTest::Repeated::getSize, &BindClassLuaTest::Repeated::setSize, "Its size again.");
        });
        EQUALS(property.find("'Repeated.size'") != std::string::npos, true);

        const std::string readOnly = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Property("size", &BindClassLuaTest::Repeated::getSize, "Its size, read-only.");
        });
        EQUALS(readOnly.find("'Repeated.size'") != std::string::npos, true);

        const std::string function = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Static(
                "make",
                [] {
                    return 1;
                },
                Framework::Scripting::Bind::Doc("Makes another."));
        });
        EQUALS(function.find("'Repeated.make'") != std::string::npos, true);

        // A script finds a method and a property under the same key.
        const std::string methodOverProperty = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Method("size", &BindClassLuaTest::Repeated::getSize, Framework::Scripting::Bind::Doc("Reads the size."));
        });
        EQUALS(methodOverProperty.find("'Repeated.size'") != std::string::npos, true);

        const std::string propertyOverMethod = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Property("go", &BindClassLuaTest::Repeated::getSize, "Its size.");
        });
        EQUALS(propertyOverMethod.find("'Repeated.go'") != std::string::npos, true);

        // `new` is the constructor, declared or not.
        const std::string constructor = BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Static(
                "new",
                [] {
                    return BindClassLuaTest::Repeated {};
                },
                Framework::Scripting::Bind::Doc("Makes one."));
        });
        EQUALS(constructor.find("'Repeated.new'") != std::string::npos, true);

        EQUALS(definition.Describe().functions.size(), 2u);
        EQUALS(definition.Describe().properties.size(), 1u);

        // A static and a method are found in different places, and an operator may be declared again.
        EQUALS(BindClassLuaTest::LogicErrorOf([&definition] {
            definition.Static(
                "go",
                [] {
                    return 1;
                },
                Framework::Scripting::Bind::Doc("Goes, as a static."));
            definition.LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Len, [](const BindClassLuaTest::Repeated &repeated) {
                return repeated.size;
            });
            definition.LuaMeta(Framework::Scripting::Bind::LuaMetaEvent::Len, [](const BindClassLuaTest::Repeated &repeated) {
                return repeated.size;
            });
        }),
            "");
        EQUALS(definition.Describe().functions.size(), 3u);

        LuaTest::State state;
        BindClassLuaTest::Publish(state.L, definition, Framework::Scripting::Bind::Side::Server);
        EQUALS(LuaTest::Eval(state.L, "Repeated.go()"), "1");
        EQUALS(LuaTest::Eval(state.L, "local r = Repeated.make() r.size = 4 r:go() return r.size"), "4");
    });
});
