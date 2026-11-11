/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_test_helpers.h"

#include "scripting/bind/lua_convert.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LuaConvertTest {
    namespace Bind = Framework::Scripting::Bind;

    enum class Mode : uint8_t {
        Off,
        On
    };

    // What `take` last read and what `give` pushes, one per C++ type.
    template <typename T>
    T &Slot() {
        static T value {};
        return value;
    }

    // take(value): whether the argument is a T. When it is, the test finds it in Slot<T>().
    template <typename T>
    int Take(lua_State *L) {
        const bool is = Bind::LuaConvert<T>::Is(L, 1);
        if (is) {
            Slot<T>() = Bind::LuaConvert<T>::Get(L, 1);
        }
        lua_pushboolean(L, is);
        return 1;
    }

    // give(): pushes Slot<T>().
    template <typename T>
    int Give(lua_State *L) {
        Bind::LuaConvert<T>::Push(L, Slot<T>());
        return 1;
    }

    // echo(value): reads the argument as a T and pushes it back, refusing anything else the way a thunk does.
    // luaL_error puts the position of the calling Lua code in front of the message.
    template <typename T>
    int Echo(lua_State *L) {
        if (!Bind::LuaConvert<T>::Is(L, 1)) {
            return luaL_error(L, "bad argument #1 to 'echo' (%s expected, got %s)", Bind::LuaConvert<T>::Expected(), Bind::LuaTypeNameAt(L, 1));
        }
        Bind::LuaConvert<T>::Push(L, Bind::LuaConvert<T>::Get(L, 1));
        return 1;
    }

    // Publishes take, give and echo for T, replacing those of another type.
    template <typename T>
    void Expose(lua_State *L) {
        lua_register(L, "take", Take<T>);
        lua_register(L, "give", Give<T>);
        lua_register(L, "echo", Echo<T>);
    }

    // The length C++ sees through a view of the argument, read while the string is on the stack.
    inline int ViewLength(lua_State *L) {
        lua_pushinteger(L, static_cast<lua_Integer>(Bind::LuaConvert<std::string_view>::Get(L, 1).size()));
        return 1;
    }

    inline int CStringLength(lua_State *L) {
        lua_pushinteger(L, static_cast<lua_Integer>(std::strlen(Bind::LuaConvert<const char *>::Get(L, 1))));
        return 1;
    }
} // namespace LuaConvertTest

MODULE(lua_convert, {
    using namespace Framework::Scripting::Bind;
    using LuaConvertTest::Expose;
    using LuaConvertTest::Mode;
    using LuaConvertTest::Slot;
    using LuaTest::Eval;
    using LuaTest::EvalNumber;

    // --- The shared Lua test helper ---

    IT("opens the base, coroutine, table, string, math and utf8 libraries and no other", {
        LuaTest::State state;
        EQUALS(Eval(state.L, "type(tostring)"), "function");
        EQUALS(Eval(state.L, "type(coroutine)"), "table");
        EQUALS(Eval(state.L, "type(table)"), "table");
        EQUALS(Eval(state.L, "type(string)"), "table");
        EQUALS(Eval(state.L, "type(math)"), "table");
        EQUALS(Eval(state.L, "type(utf8)"), "table");
        EQUALS(Eval(state.L, "type(io)"), "nil");
        EQUALS(Eval(state.L, "type(os)"), "nil");
        EQUALS(Eval(state.L, "type(debug)"), "nil");
        EQUALS(Eval(state.L, "type(package)"), "nil");
        EQUALS(Eval(state.L, "type(require)"), "nil");
    });

    IT("spells the first result of an expression as tostring would", {
        LuaTest::State state;
        EQUALS(Eval(state.L, "nil"), "nil");
        EQUALS(Eval(state.L, "true"), "true");
        EQUALS(Eval(state.L, "1 == 2"), "false");
        EQUALS(Eval(state.L, "1 + 1"), "2");
        EQUALS(Eval(state.L, "3 / 2"), "1.5");
        EQUALS(Eval(state.L, "4 / 2"), "2.0");
        EQUALS(Eval(state.L, "'some' .. ' text'"), "some text");
        EQUALS(Eval(state.L, "''"), "");
        EQUALS(Eval(state.L, "1, 2"), "1");
        EQUALS(Eval(state.L, "#'a\\0b'"), "3");
        EQUALS(Eval(state.L, "'a\\0b'"), std::string("a\0b", 3));
        EQUALS(Eval(state.L, "setmetatable({}, { __tostring = function() return 'custom' end })"), "custom");
    });

    IT("runs a chunk that is not an expression", {
        LuaTest::State state;
        EQUALS(Eval(state.L, "x = 5"), "nil");
        EQUALS(Eval(state.L, "x"), "5");
        EQUALS(Eval(state.L, "local y = x + 1 return y * 2"), "12");
        EQUALS(Eval(state.L, "return 'a' .. 'b'"), "ab");
        EQUALS(Eval(state.L, "returned = 7 return returned"), "7");
        EQUALS(Eval(state.L, "returned"), "7");
    });

    IT("reports a compile or runtime error and stays usable", {
        LuaTest::State state;
        EQUALS(Eval(state.L, "error('boom')"), "<error> eval:1: boom");
        EQUALS(Eval(state.L, "error('boom', 0)"), "<error> boom");
        EQUALS(Eval(state.L, "local t = nil\nreturn t.field"), "<error> eval:2: attempt to index a nil value (local 't')");
        EQUALS(Eval(state.L, "1 +"), "<error> eval:1: unexpected symbol near '1'");
        EQUALS(Eval(state.L, "error({})"), "<error> (error object is a table value)");
        EQUALS(Eval(state.L, "setmetatable({}, { __tostring = function() error('unprintable', 0) end })"), "<error> unprintable");
        EQUALS(Eval(state.L, "1 + 1"), "2");
    });

    IT("refuses a binary chunk", {
        LuaTest::State state;
        EQUALS(Eval(state.L, "\x1bLua"), "<error> attempt to load a binary chunk (mode is 't')");
    });

    IT("evaluates a number, or NaN when there is none", {
        LuaTest::State state;
        EQUALS(EvalNumber(state.L, "1 + 1"), 2.0);
        EQUALS(EvalNumber(state.L, "3 / 2"), 1.5);
        EQUALS(EvalNumber(state.L, "local half = 0.5 return half * 3"), 1.5);
        EQUALS(std::isnan(EvalNumber(state.L, "'2'")), true);
        EQUALS(std::isnan(EvalNumber(state.L, "nil")), true);
        EQUALS(std::isnan(EvalNumber(state.L, "error('boom')")), true);
        EQUALS(std::isnan(EvalNumber(state.L, "1 +")), true);
    });

    IT("leaves the stack as it found it", {
        LuaTest::State state;
        lua_pushinteger(state.L, 42);
        Eval(state.L, "1 + 1");
        Eval(state.L, "error('boom')");
        Eval(state.L, "1 +");
        EvalNumber(state.L, "1 + 1");
        EvalNumber(state.L, "error('boom')");
        EQUALS(lua_gettop(state.L), 1);
        EQUALS(lua_tointeger(state.L, 1), 42);
    });

    // --- bool ---

    IT("converts a boolean and nothing else to bool", {
        LuaTest::State state;
        Expose<bool>(state.L);

        EQUALS(Eval(state.L, "take(true)"), "true");
        EQUALS(Slot<bool>(), true);
        EQUALS(Eval(state.L, "take(false)"), "true");
        EQUALS(Slot<bool>(), false);

        EQUALS(Eval(state.L, "take(0)"), "false");
        EQUALS(Eval(state.L, "take(1)"), "false");
        EQUALS(Eval(state.L, "take(nil)"), "false");
        EQUALS(Eval(state.L, "take()"), "false");
        EQUALS(Eval(state.L, "take('true')"), "false");

        Slot<bool>() = true;
        EQUALS(Eval(state.L, "give() == true"), "true");
        Slot<bool>() = false;
        EQUALS(Eval(state.L, "give() == false"), "true");
        EQUALS(Eval(state.L, "echo(true)"), "true");
        EQUALS(Eval(state.L, "echo(0)"), "<error> eval:1: bad argument #1 to 'echo' (boolean expected, got number)");
    });

    // --- Integral types ---

    IT("converts an integer, or a float with an integral value, to an integral type", {
        LuaTest::State state;
        Expose<int>(state.L);

        EQUALS(Eval(state.L, "take(5)"), "true");
        EQUALS(Slot<int>(), 5);
        EQUALS(Eval(state.L, "take(-7)"), "true");
        EQUALS(Slot<int>(), -7);
        EQUALS(Eval(state.L, "take(6.0)"), "true");
        EQUALS(Slot<int>(), 6);
        EQUALS(Eval(state.L, "take(2^10)"), "true");
        EQUALS(Slot<int>(), 1024);

        Slot<int>() = -3;
        EQUALS(Eval(state.L, "give()"), "-3");
        EQUALS(Eval(state.L, "math.type(give())"), "integer");
        EQUALS(Eval(state.L, "echo(5)"), "5");
        EQUALS(Eval(state.L, "math.type(echo(5.0))"), "integer");
    });

    IT("refuses what is not a whole number for an integral type", {
        LuaTest::State state;
        Expose<int>(state.L);

        EQUALS(Eval(state.L, "take('5')"), "false");
        EQUALS(Eval(state.L, "take('abc')"), "false");
        EQUALS(Eval(state.L, "take(1.5)"), "false");
        EQUALS(Eval(state.L, "take(0/0)"), "false");
        EQUALS(Eval(state.L, "take(math.huge)"), "false");
        EQUALS(Eval(state.L, "take(true)"), "false");
        EQUALS(Eval(state.L, "take(nil)"), "false");
        EQUALS(Eval(state.L, "take()"), "false");
        EQUALS(Eval(state.L, "take({})"), "false");

        EQUALS(Eval(state.L, "echo('5')"), "<error> eval:1: bad argument #1 to 'echo' (integer expected, got string)");
        EQUALS(Eval(state.L, "echo(1.5)"), "<error> eval:1: bad argument #1 to 'echo' (integer expected, got number)");
    });

    IT("refuses an integer outside the range of the C++ type", {
        LuaTest::State state;

        Expose<uint8_t>(state.L);
        EQUALS(Eval(state.L, "take(0)"), "true");
        EQUALS(Slot<uint8_t>(), 0);
        EQUALS(Eval(state.L, "take(255)"), "true");
        EQUALS(Slot<uint8_t>(), 255);
        EQUALS(Eval(state.L, "take(300)"), "false");
        EQUALS(Eval(state.L, "take(256)"), "false");
        EQUALS(Eval(state.L, "take(-1)"), "false");
        EQUALS(Eval(state.L, "take(300.0)"), "false");

        Expose<int8_t>(state.L);
        EQUALS(Eval(state.L, "take(-128)"), "true");
        EQUALS(Slot<int8_t>(), -128);
        EQUALS(Eval(state.L, "take(127)"), "true");
        EQUALS(Slot<int8_t>(), 127);
        EQUALS(Eval(state.L, "take(-129)"), "false");
        EQUALS(Eval(state.L, "take(128)"), "false");

        Expose<int32_t>(state.L);
        EQUALS(Eval(state.L, "take(2147483647)"), "true");
        EQUALS(Slot<int32_t>(), std::numeric_limits<int32_t>::max());
        EQUALS(Eval(state.L, "take(-2147483648)"), "true");
        EQUALS(Slot<int32_t>(), std::numeric_limits<int32_t>::min());
        EQUALS(Eval(state.L, "take(2147483648)"), "false");
        EQUALS(Eval(state.L, "take(-2147483649)"), "false");

        Expose<uint32_t>(state.L);
        EQUALS(Eval(state.L, "take(4294967295)"), "true");
        EQUALS(Slot<uint32_t>(), std::numeric_limits<uint32_t>::max());
        EQUALS(Eval(state.L, "take(4294967296)"), "false");
        EQUALS(Eval(state.L, "take(-1)"), "false");
        Slot<uint32_t>() = std::numeric_limits<uint32_t>::max();
        EQUALS(Eval(state.L, "give()"), "4294967295");
    });

    IT("converts the whole 64-bit range to int64_t", {
        LuaTest::State state;
        Expose<int64_t>(state.L);

        EQUALS(Eval(state.L, "take(math.maxinteger)"), "true");
        EQUALS(Slot<int64_t>(), std::numeric_limits<int64_t>::max());
        EQUALS(Eval(state.L, "take(math.mininteger)"), "true");
        EQUALS(Slot<int64_t>(), std::numeric_limits<int64_t>::min());
        EQUALS(Eval(state.L, "echo(math.maxinteger) == math.maxinteger"), "true");
        EQUALS(Eval(state.L, "echo(math.mininteger) == math.mininteger"), "true");

        EQUALS(Eval(state.L, "take(2^53)"), "true");
        EQUALS(Slot<int64_t>(), int64_t {1} << 53);
        // 2^63 is a float one past the largest integer.
        EQUALS(Eval(state.L, "take(2^63)"), "false");
    });

    IT("converts uint64_t as the two's complement of a Lua integer", {
        LuaTest::State state;
        Expose<uint64_t>(state.L);

        EQUALS(Eval(state.L, "take(0xFFFFFFFFFFFFFFFF)"), "true");
        EQUALS(Slot<uint64_t>(), std::numeric_limits<uint64_t>::max());
        EQUALS(Eval(state.L, "take(0x8000000000000000)"), "true");
        EQUALS(Slot<uint64_t>(), uint64_t {1} << 63);
        EQUALS(Eval(state.L, "take(42)"), "true");
        EQUALS(Slot<uint64_t>(), uint64_t {42});
        // The same integer, spelled as Lua prints it.
        EQUALS(Eval(state.L, "take(-1)"), "true");
        EQUALS(Slot<uint64_t>(), std::numeric_limits<uint64_t>::max());
        // A float has no two's complement: 2^63 is beyond every Lua integer.
        EQUALS(Eval(state.L, "take(2^63)"), "false");

        Slot<uint64_t>() = std::numeric_limits<uint64_t>::max();
        EQUALS(Eval(state.L, "give() == 0xFFFFFFFFFFFFFFFF"), "true");
        EQUALS(Eval(state.L, "string.format('%x', give())"), "ffffffffffffffff");
        EQUALS(Eval(state.L, "math.type(give())"), "integer");
        EQUALS(Eval(state.L, "string.format('%x', echo(0xFFFFFFFFFFFFFFFF))"), "ffffffffffffffff");
        EQUALS(Eval(state.L, "echo(0xFFFFFFFFFFFFFFFE) == 0xFFFFFFFFFFFFFFFE"), "true");

        EQUALS(Eval(state.L, "take(1.5)"), "false");
        EQUALS(Eval(state.L, "take('1')"), "false");
    });

    IT("converts an enum as its underlying integer", {
        LuaTest::State state;
        Expose<Mode>(state.L);

        EQUALS(Eval(state.L, "take(1)"), "true");
        EQUALS(Slot<Mode>(), Mode::On);
        EQUALS(Eval(state.L, "take(0.0)"), "true");
        EQUALS(Slot<Mode>(), Mode::Off);

        Slot<Mode>() = Mode::On;
        EQUALS(Eval(state.L, "give()"), "1");
        EQUALS(Eval(state.L, "math.type(give())"), "integer");
        EQUALS(Eval(state.L, "echo(1)"), "1");

        EQUALS(Eval(state.L, "take(300)"), "false");
        EQUALS(Eval(state.L, "take(-1)"), "false");
        EQUALS(Eval(state.L, "take(1.5)"), "false");
        EQUALS(Eval(state.L, "take('1')"), "false");
        EQUALS(Eval(state.L, "take(true)"), "false");
        EQUALS(Eval(state.L, "echo('1')"), "<error> eval:1: bad argument #1 to 'echo' (integer expected, got string)");
    });

    // --- float, double ---

    IT("converts an integer or a float to double and pushes a float", {
        LuaTest::State state;
        Expose<double>(state.L);

        EQUALS(Eval(state.L, "take(2.5)"), "true");
        EQUALS(Slot<double>(), 2.5);
        EQUALS(Eval(state.L, "take(2)"), "true");
        EQUALS(Slot<double>(), 2.0);
        EQUALS(Eval(state.L, "take(math.huge)"), "true");
        EQUALS(Slot<double>(), std::numeric_limits<double>::infinity());

        Slot<double>() = 0.25;
        EQUALS(Eval(state.L, "give()"), "0.25");
        Slot<double>() = 3.0;
        EQUALS(Eval(state.L, "give()"), "3.0");
        EQUALS(Eval(state.L, "math.type(give())"), "float");
        EQUALS(Eval(state.L, "math.type(echo(2))"), "float");

        EQUALS(Eval(state.L, "take('1.5')"), "false");
        EQUALS(Eval(state.L, "take('abc')"), "false");
        EQUALS(Eval(state.L, "take(true)"), "false");
        EQUALS(Eval(state.L, "take(nil)"), "false");
        EQUALS(Eval(state.L, "take()"), "false");
        EQUALS(Eval(state.L, "echo('1.5')"), "<error> eval:1: bad argument #1 to 'echo' (number expected, got string)");
    });

    IT("converts an integer or a float to float", {
        LuaTest::State state;
        Expose<float>(state.L);

        EQUALS(Eval(state.L, "take(0.5)"), "true");
        EQUALS(Slot<float>(), 0.5f);
        EQUALS(Eval(state.L, "take(3)"), "true");
        EQUALS(Slot<float>(), 3.0f);
        EQUALS(Eval(state.L, "echo(1.5)"), "1.5");
        EQUALS(Eval(state.L, "math.type(echo(1))"), "float");

        EQUALS(Eval(state.L, "take('0.5')"), "false");
    });

    IT("reads a number beyond the range of float as an infinity", {
        LuaTest::State state;
        Expose<float>(state.L);

        EQUALS(Eval(state.L, "take(1e300)"), "true");
        EQUALS(Slot<float>(), std::numeric_limits<float>::infinity());
        EQUALS(Eval(state.L, "take(-1e300)"), "true");
        EQUALS(Slot<float>(), -std::numeric_limits<float>::infinity());
        EQUALS(Eval(state.L, "echo(-1e300) == -math.huge"), "true");
        // Just past the largest float, where rounding to nearest would still give the largest float.
        EQUALS(Eval(state.L, "take(0x1.fffffe8p+127)"), "true");
        EQUALS(Slot<float>(), std::numeric_limits<float>::infinity());
        EQUALS(Eval(state.L, "take(-0x1.fffffe8p+127)"), "true");
        EQUALS(Slot<float>(), -std::numeric_limits<float>::infinity());

        // The largest float is still itself, and what was not finite stays so.
        EQUALS(Eval(state.L, "take(0x1.fffffep+127)"), "true");
        EQUALS(Slot<float>(), std::numeric_limits<float>::max());
        EQUALS(Eval(state.L, "take(-0x1.fffffep+127)"), "true");
        EQUALS(Slot<float>(), std::numeric_limits<float>::lowest());
        EQUALS(Eval(state.L, "echo(math.huge) == math.huge"), "true");
        EQUALS(Eval(state.L, "echo(-math.huge) == -math.huge"), "true");
        EQUALS(Eval(state.L, "local nan = echo(0/0) return nan ~= nan"), "true");
    });

    // --- Strings ---

    IT("converts a string and nothing else to std::string", {
        LuaTest::State state;
        Expose<std::string>(state.L);

        EQUALS(Eval(state.L, "take('hello')"), "true");
        EQUALS(Slot<std::string>(), "hello");
        EQUALS(Eval(state.L, "take('')"), "true");
        EQUALS(Slot<std::string>(), "");
        EQUALS(Eval(state.L, "take('a\\0b')"), "true");
        EQUALS(Slot<std::string>(), std::string("a\0b", 3));

        Slot<std::string>() = std::string("x\0y", 3);
        EQUALS(Eval(state.L, "#give()"), "3");
        EQUALS(Eval(state.L, "give() == 'x\\0y'"), "true");
        EQUALS(Eval(state.L, "echo('round trip')"), "round trip");

        // No coercion: a number is not a string.
        EQUALS(Eval(state.L, "take(5)"), "false");
        EQUALS(Eval(state.L, "take(1.5)"), "false");
        EQUALS(Eval(state.L, "take(nil)"), "false");
        EQUALS(Eval(state.L, "take()"), "false");
        EQUALS(Eval(state.L, "take(true)"), "false");
        EQUALS(Eval(state.L, "take({})"), "false");
        EQUALS(Eval(state.L, "echo(5)"), "<error> eval:1: bad argument #1 to 'echo' (string expected, got number)");
    });

    IT("converts a string to a std::string_view of the whole string", {
        LuaTest::State state;
        Expose<std::string_view>(state.L);
        lua_register(state.L, "viewLength", LuaConvertTest::ViewLength);

        EQUALS(Eval(state.L, "echo('view')"), "view");
        EQUALS(Eval(state.L, "echo('a\\0b') == 'a\\0b'"), "true");
        EQUALS(Eval(state.L, "viewLength('a\\0b')"), "3");
        EQUALS(Eval(state.L, "viewLength('')"), "0");

        Slot<std::string_view>() = "given";
        EQUALS(Eval(state.L, "give()"), "given");

        EQUALS(Eval(state.L, "take(5)"), "false");
        EQUALS(Eval(state.L, "take(nil)"), "false");
        EQUALS(Eval(state.L, "echo(5)"), "<error> eval:1: bad argument #1 to 'echo' (string expected, got number)");
    });

    IT("converts a string to a zero-terminated const char *", {
        LuaTest::State state;
        Expose<const char *>(state.L);
        lua_register(state.L, "cstringLength", LuaConvertTest::CStringLength);

        EQUALS(Eval(state.L, "echo('pointer')"), "pointer");
        EQUALS(Eval(state.L, "cstringLength('pointer')"), "7");
        EQUALS(Eval(state.L, "cstringLength('a\\0b')"), "1");

        Slot<const char *>() = "given";
        EQUALS(Eval(state.L, "give()"), "given");
        Slot<const char *>() = nullptr;
        EQUALS(Eval(state.L, "give()"), "nil");

        EQUALS(Eval(state.L, "take(5)"), "false");
        EQUALS(Eval(state.L, "take(nil)"), "false");
        EQUALS(Eval(state.L, "echo(5)"), "<error> eval:1: bad argument #1 to 'echo' (string expected, got number)");
    });

    // --- std::optional ---

    IT("converts nil to an empty optional and a value to its value", {
        LuaTest::State state;
        Expose<std::optional<int>>(state.L);

        Slot<std::optional<int>>() = 1;
        EQUALS(Eval(state.L, "take(nil)"), "true");
        EQUALS(Slot<std::optional<int>>().has_value(), false);

        EQUALS(Eval(state.L, "take(5)"), "true");
        EQUALS(Slot<std::optional<int>>().value_or(0), 5);

        Slot<std::optional<int>>() = std::nullopt;
        EQUALS(Eval(state.L, "give()"), "nil");
        EQUALS(Eval(state.L, "select('#', give())"), "1");
        Slot<std::optional<int>>() = 7;
        EQUALS(Eval(state.L, "give()"), "7");
        EQUALS(Eval(state.L, "echo(nil)"), "nil");
        EQUALS(Eval(state.L, "echo(9)"), "9");
    });

    IT("converts an absent argument to an empty optional", {
        LuaTest::State state;
        Expose<std::optional<int>>(state.L);

        Slot<std::optional<int>>() = 1;
        EQUALS(Eval(state.L, "take()"), "true");
        EQUALS(Slot<std::optional<int>>().has_value(), false);
        EQUALS(Eval(state.L, "echo()"), "nil");
    });

    IT("refuses for an optional what its value type refuses", {
        LuaTest::State state;
        Expose<std::optional<int>>(state.L);
        EQUALS(Eval(state.L, "take('5')"), "false");
        EQUALS(Eval(state.L, "take(1.5)"), "false");
        EQUALS(Eval(state.L, "take(false)"), "false");
        EQUALS(Eval(state.L, "echo('5')"), "<error> eval:1: bad argument #1 to 'echo' (integer expected, got string)");

        Expose<std::optional<std::string>>(state.L);
        EQUALS(Eval(state.L, "take('text')"), "true");
        EQUALS(Slot<std::optional<std::string>>().value_or(""), "text");
        EQUALS(Eval(state.L, "take(5)"), "false");
        EQUALS(Eval(state.L, "echo(5)"), "<error> eval:1: bad argument #1 to 'echo' (string expected, got number)");
    });

    // --- std::vector ---

    IT("converts a sequence to a vector", {
        LuaTest::State state;
        Expose<std::vector<int>>(state.L);

        EQUALS(Eval(state.L, "take({ 1, 2, 3 })"), "true");
        EQUALS(Slot<std::vector<int>>(), (std::vector<int> {1, 2, 3}));
        EQUALS(Eval(state.L, "take({ 4.0 })"), "true");
        EQUALS(Slot<std::vector<int>>(), (std::vector<int> {4}));
        EQUALS(Eval(state.L, "take({})"), "true");
        EQUALS(Slot<std::vector<int>>().empty(), true);
        EQUALS(Eval(state.L, "local t = {} for i = 1, 1000 do t[i] = i end return take(t)"), "true");
        EQUALS(Slot<std::vector<int>>().size(), size_t {1000});
        EQUALS(Slot<std::vector<int>>().back(), 1000);
    });

    IT("pushes a vector as a sequence starting at 1", {
        LuaTest::State state;
        Expose<std::vector<int>>(state.L);

        Slot<std::vector<int>>() = {4, 5, 6};
        EQUALS(Eval(state.L, "type(give())"), "table");
        EQUALS(Eval(state.L, "#give()"), "3");
        EQUALS(Eval(state.L, "give()[1]"), "4");
        EQUALS(Eval(state.L, "give()[3]"), "6");
        EQUALS(Eval(state.L, "give()[0]"), "nil");
        EQUALS(Eval(state.L, "give()[4]"), "nil");
        EQUALS(Eval(state.L, "math.type(give()[1])"), "integer");
        EQUALS(Eval(state.L, "table.concat(echo({ 7, 8, 9 }), ',')"), "7,8,9");

        Slot<std::vector<int>>().clear();
        EQUALS(Eval(state.L, "type(give())"), "table");
        EQUALS(Eval(state.L, "next(give())"), "nil");
    });

    IT("refuses a table that is not a sequence of the element type", {
        LuaTest::State state;
        Expose<std::vector<int>>(state.L);

        EQUALS(Eval(state.L, "take({ 1, nil, 3 })"), "false");
        EQUALS(Eval(state.L, "take({ a = 1 })"), "false");
        EQUALS(Eval(state.L, "take({ 1, 2, a = 3 })"), "false");
        EQUALS(Eval(state.L, "take({ [0] = 1 })"), "false");
        EQUALS(Eval(state.L, "take({ [2] = 1 })"), "false");
        EQUALS(Eval(state.L, "take({ [1.5] = 1 })"), "false");
        EQUALS(Eval(state.L, "take({ 1, 'x', 3 })"), "false");
        EQUALS(Eval(state.L, "take({ 1, 2.5 })"), "false");
        EQUALS(Eval(state.L, "take({ {} })"), "false");

        EQUALS(Eval(state.L, "take(5)"), "false");
        EQUALS(Eval(state.L, "take('abc')"), "false");
        EQUALS(Eval(state.L, "take(nil)"), "false");
        EQUALS(Eval(state.L, "take()"), "false");
        EQUALS(Eval(state.L, "echo({ a = 1 })"), "<error> eval:1: bad argument #1 to 'echo' (table expected, got table)");
        EQUALS(Eval(state.L, "echo(5)"), "<error> eval:1: bad argument #1 to 'echo' (table expected, got number)");
    });

    IT("converts vectors of every element type, nested too", {
        LuaTest::State state;

        Expose<std::vector<std::string>>(state.L);
        EQUALS(Eval(state.L, "take({ 'a', 'bc' })"), "true");
        EQUALS(Slot<std::vector<std::string>>(), (std::vector<std::string> {"a", "bc"}));
        EQUALS(Eval(state.L, "take({ 'a', 2 })"), "false");

        Expose<std::vector<double>>(state.L);
        EQUALS(Eval(state.L, "table.concat(echo({ 1, 2.5 }), ' ')"), "1.0 2.5");

        Expose<std::vector<bool>>(state.L);
        EQUALS(Eval(state.L, "take({ true, false, true })"), "true");
        EQUALS(Slot<std::vector<bool>>(), (std::vector<bool> {true, false, true}));
        EQUALS(Eval(state.L, "echo({ false, true })[2]"), "true");

        Expose<std::vector<std::vector<int>>>(state.L);
        EQUALS(Eval(state.L, "take({ { 1, 2 }, {}, { 3 } })"), "true");
        EQUALS(Slot<std::vector<std::vector<int>>>(), (std::vector<std::vector<int>> {{1, 2}, {}, {3}}));
        EQUALS(Eval(state.L, "echo({ { 1, 2 }, {}, { 3 } })[1][2]"), "2");
        EQUALS(Eval(state.L, "#echo({ { 1, 2 }, {}, { 3 } })[2]"), "0");
        EQUALS(Eval(state.L, "take({ { 1 }, { a = 1 } })"), "false");
        EQUALS(Eval(state.L, "take({ { 1 }, 2 })"), "false");

        Expose<std::optional<std::vector<int>>>(state.L);
        EQUALS(Eval(state.L, "echo(nil)"), "nil");
        EQUALS(Eval(state.L, "#echo({ 1, 2 })"), "2");
        EQUALS(Eval(state.L, "echo({ a = 1 })"), "<error> eval:1: bad argument #1 to 'echo' (table expected, got table)");
    });

    IT("reads a table without running its metamethods", {
        LuaTest::State state;
        Expose<std::vector<int>>(state.L);

        const std::string proxy  = "setmetatable({}, {"
                                   " __index = function() error('__index ran') end,"
                                   " __len = function() error('__len ran') end,"
                                   " __pairs = function() error('__pairs ran') end })";
        Slot<std::vector<int>>() = {1};
        EQUALS(Eval(state.L, "take(" + proxy + ")"), "true");
        EQUALS(Slot<std::vector<int>>().empty(), true);
        EQUALS(Eval(state.L, "#echo(setmetatable({ 1, 2 }, { __index = function() return 9 end, __len = function() return 5 end }))"), "2");
    });

    // --- The stack contract thunks rely on ---

    IT("accepts a relative index and leaves the stack as it found it", {
        LuaTest::State state;
        lua_State *L = state.L;

        lua_pushinteger(L, 11);
        lua_pushliteral(L, "5");
        EQUALS(luaL_dostring(L, "return { { 1, 2 }, { 3 } }"), LUA_OK);
        EQUALS(lua_gettop(L), 3);

        EQUALS(LuaConvert<std::vector<std::vector<int>>>::Is(L, -1), true);
        EQUALS(lua_gettop(L), 3);
        EQUALS(LuaConvert<std::vector<std::vector<int>>>::Get(L, -1), (std::vector<std::vector<int>> {{1, 2}, {3}}));
        EQUALS(lua_gettop(L), 3);
        EQUALS(LuaConvert<std::vector<std::string>>::Is(L, -1), false);
        EQUALS(lua_gettop(L), 3);

        EQUALS(LuaConvert<std::string>::Is(L, -2), true);
        EQUALS(LuaConvert<std::string>::Get(L, -2), "5");
        EQUALS(LuaConvert<int>::Is(L, -3), true);
        EQUALS(LuaConvert<int>::Get(L, -3), 11);
        EQUALS(LuaConvert<std::optional<int>>::Get(L, -3).value_or(0), 11);
        EQUALS(LuaConvert<std::optional<int>>::Is(L, 4), true);
        EQUALS(LuaConvert<std::optional<int>>::Get(L, 4).has_value(), false);

        // A refused value is not converted in place either.
        EQUALS(LuaConvert<int>::Is(L, -2), false);
        EQUALS(LuaConvert<double>::Is(L, -2), false);
        EQUALS(lua_type(L, -2), LUA_TSTRING);
        EQUALS(LuaConvert<std::string>::Is(L, -3), false);
        EQUALS(lua_type(L, -3), LUA_TNUMBER);
        EQUALS(lua_gettop(L), 3);
    });

    IT("pushes exactly one value", {
        LuaTest::State state;
        lua_State *L = state.L;

        LuaConvert<bool>::Push(L, true);
        EQUALS(lua_gettop(L), 1);
        LuaConvert<int>::Push(L, 1);
        EQUALS(lua_gettop(L), 2);
        LuaConvert<double>::Push(L, 1.5);
        EQUALS(lua_gettop(L), 3);
        LuaConvert<std::string>::Push(L, "text");
        EQUALS(lua_gettop(L), 4);
        LuaConvert<std::optional<int>>::Push(L, std::nullopt);
        EQUALS(lua_gettop(L), 5);
        EQUALS(lua_type(L, -1), LUA_TNIL);
        LuaConvert<std::vector<std::vector<int>>>::Push(L, {{1, 2}, {3}});
        EQUALS(lua_gettop(L), 6);
        EQUALS(lua_type(L, -1), LUA_TTABLE);
        LuaConvert<std::vector<int>>::Push(L, {});
        EQUALS(lua_gettop(L), 7);
        LuaConvert<Mode>::Push(L, Mode::On);
        EQUALS(lua_gettop(L), 8);
    });

    // --- What an argument error names ---

    IT("names the expected type of each C++ type", {
        STREQUALS(LuaConvert<bool>::Expected(), "boolean");
        STREQUALS(LuaConvert<int>::Expected(), "integer");
        STREQUALS(LuaConvert<uint8_t>::Expected(), "integer");
        STREQUALS(LuaConvert<uint64_t>::Expected(), "integer");
        STREQUALS(LuaConvert<Mode>::Expected(), "integer");
        STREQUALS(LuaConvert<float>::Expected(), "number");
        STREQUALS(LuaConvert<double>::Expected(), "number");
        STREQUALS(LuaConvert<std::string>::Expected(), "string");
        STREQUALS(LuaConvert<std::string_view>::Expected(), "string");
        STREQUALS(LuaConvert<const char *>::Expected(), "string");
        STREQUALS(LuaConvert<std::vector<int>>::Expected(), "table");
        STREQUALS(LuaConvert<std::optional<double>>::Expected(), "number");
        STREQUALS(LuaConvert<std::optional<std::vector<int>>>::Expected(), "table");
    });

    IT("names the type of the value it was given", {
        LuaTest::State state;
        lua_State *L = state.L;

        lua_pushnil(L);
        lua_pushboolean(L, 1);
        lua_pushinteger(L, 1);
        lua_pushliteral(L, "text");
        lua_newtable(L);
        lua_pushcfunction(L, LuaConvertTest::ViewLength);
        STREQUALS(LuaTypeNameAt(L, 1), "nil");
        STREQUALS(LuaTypeNameAt(L, 2), "boolean");
        STREQUALS(LuaTypeNameAt(L, 3), "number");
        STREQUALS(LuaTypeNameAt(L, 4), "string");
        STREQUALS(LuaTypeNameAt(L, 5), "table");
        STREQUALS(LuaTypeNameAt(L, -1), "function");
        STREQUALS(LuaTypeNameAt(L, 7), "no value");

        Expose<int>(L);
        EQUALS(Eval(L, "echo()"), "<error> eval:1: bad argument #1 to 'echo' (integer expected, got no value)");
        EQUALS(Eval(L, "echo(nil)"), "<error> eval:1: bad argument #1 to 'echo' (integer expected, got nil)");
        EQUALS(Eval(L, "echo({})"), "<error> eval:1: bad argument #1 to 'echo' (integer expected, got table)");
        EQUALS(Eval(L, "echo(print)"), "<error> eval:1: bad argument #1 to 'echo' (integer expected, got function)");
    });
});
