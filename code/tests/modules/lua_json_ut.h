/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_transfer_ut.h"

#include "scripting/lua/lua_json.h"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace LuaJsonTest {
    // Written to JSON as the properties it names, in the order it names them.
    class Shape {
      public:
        explicit Shape(int size): _size(size) {}

        int GetSize() const {
            return _size;
        }

        // A getter that fails for a shape that makes no sense.
        std::string GetLabel() const {
            if (_size < 0) {
                throw std::runtime_error("a shape cannot be smaller than nothing");
            }
            return "shape " + std::to_string(_size);
        }

        Framework::Scripting::Builtins::Vector3 GetOrigin() const {
            return {1.0f, 2.0f, 3.0f};
        }

      private:
        int _size;
    };

    // Names no property for JSON itself.
    class Square final: public Shape {
      public:
        explicit Square(int size): Shape(size) {}
    };

    // Each one holds the next: written to JSON, it never ends.
    struct Chain final {
        Chain GetNext() const {
            return {};
        }
    };

    // For what a definition refuses.
    struct Refused final {
        int GetValue() const {
            return 0;
        }
    };
} // namespace LuaJsonTest

FW_SCRIPT_CLASS(LuaJsonTest::Shape, "JsonShape");
FW_SCRIPT_CLASS(LuaJsonTest::Square, "JsonSquare");
FW_SCRIPT_CLASS(LuaJsonTest::Chain, "JsonChain");
FW_SCRIPT_CLASS(LuaJsonTest::Refused, "JsonRefused");

namespace LuaJsonTest {
    namespace Bind = Framework::Scripting::Bind;

    inline const Bind::Class<Shape> &ShapeDefinition() {
        using Bind::Doc;
        static const auto definition = Bind::Class<Shape>("JsonShape", "A shape.")
                                           .Constructor<int>(Doc("Creates a shape.").Param("size", "Its size."))
                                           .Property("size", &Shape::GetSize, "Its size.")
                                           .Property("label", &Shape::GetLabel, "Its label.")
                                           .Property("origin", &Shape::GetOrigin, "Where it is.")
                                           .JsonProperties({"label", "origin", "size"});
        return definition;
    }

    inline const Bind::Class<Square> &SquareDefinition() {
        using Bind::Doc;
        ShapeDefinition();
        static const auto definition = Bind::Class<Square>("JsonSquare", "A square.").Inherit<Shape>().Constructor<int>(Doc("Creates a square.").Param("size", "Its size."));
        return definition;
    }

    inline const Bind::Class<Chain> &ChainDefinition() {
        using Bind::Doc;
        static const auto definition = Bind::Class<Chain>("JsonChain", "A chain.").Constructor<>(Doc("Creates a chain.")).Property("next", &Chain::GetNext, "The next link.").JsonProperties({"next"});
        return definition;
    }

    using Framework::Scripting::LuaRuntime;
    using LuaTransferTest::Worlds;
    namespace LuaJson = Framework::Scripting::LuaJson;

    // The JSON of what `expression` gives in a Lua state, or "<failed> " and why it has none.
    inline std::string Encode(LuaRuntime &runtime, const std::string &expression) {
        lua_State *L            = runtime.State();
        const std::string chunk = "return " + expression;
        std::string error;
        if (luaL_loadbufferx(L, chunk.data(), chunk.size(), "=eval", "t") != LUA_OK) {
            error = lua_tostring(L, -1);
            lua_pop(L, 1);
            return "<lua> " + error;
        }
        if (!runtime.Call(0, 1, error)) {
            return "<lua> " + error;
        }
        const auto json = LuaJson::Encode(L, -1);
        lua_pop(L, 1);
        return json ? json.GetValue() : "<failed> " + json.GetError();
    }

    inline std::string Encode(Worlds &worlds, const std::string &expression) {
        return Encode(worlds.lua, expression);
    }

    // Reads `text` into the global `received` of a Lua state. Empty, or why it could not.
    inline std::string Decode(LuaRuntime &runtime, std::string_view text) {
        lua_State *L = runtime.State();
        std::string error;
        if (!LuaJson::Decode(L, text, error)) {
            return error.empty() ? "decode failed without saying why" : error;
        }
        LuaTransferTest::SetGlobal(L, "received");
        return {};
    }

    inline std::string Decode(Worlds &worlds, std::string_view text) {
        return Decode(worlds.lua, text);
    }

    // `depth` arrays, each the only element of the one around it.
    inline std::string NestedJson(int depth, char open, char close) {
        return std::string(static_cast<size_t>(depth), open) + std::string(static_cast<size_t>(depth), close);
    }
} // namespace LuaJsonTest

MODULE(lua_json, {
    using namespace LuaJsonTest;
    using LuaTransferTest::NestedLua;

    // --- Encoding ---

    IT("writes nil, booleans, integers and strings", {
        Worlds worlds;
        EQUALS(Encode(worlds, "nil"), "null");
        EQUALS(Encode(worlds, "true"), "true");
        EQUALS(Encode(worlds, "false"), "false");
        EQUALS(Encode(worlds, "42"), "42");
        EQUALS(Encode(worlds, "-7"), "-7");
        EQUALS(Encode(worlds, "math.maxinteger"), "9223372036854775807");
        EQUALS(Encode(worlds, "math.mininteger"), "-9223372036854775808");
        EQUALS(Encode(worlds, "'text'"), "\"text\"");
        EQUALS(Encode(worlds, "''"), "\"\"");
    });

    IT("writes a float as JSON.stringify writes the same number", {
        Worlds worlds;
        // Each is a literal of both languages.
        for (const char *number :
            {"0.1", "1.5", "-2.5", "100.0", "1e21", "1e20", "123456789012345680000.0", "1e-7", "0.000001", "0.00000123", "5e-324", "1.7976931348623157e308", "-0.0", "0.0", "9007199254740992.0", "0.1 + 0.2", "1 / 3", "-1e-10", "4.35", "1.0e100", "123456.789"}) {
            EQUALS(Encode(worlds, number), worlds.Js(std::string("JSON.stringify(") + number + ")"));
        }
        EQUALS(Encode(worlds, "100.0"), "100");
        EQUALS(Encode(worlds, "1e21"), "1e+21");
        EQUALS(Encode(worlds, "1e-7"), "1e-7");
        EQUALS(Encode(worlds, "0.1 + 0.2"), "0.30000000000000004");
    });

    IT("writes null for a NaN and an infinity, as JSON.stringify does", {
        Worlds worlds;
        EQUALS(Encode(worlds, "{ 0/0, 1/0, -1/0 }"), "[null,null,null]");
        EQUALS(worlds.Js("JSON.stringify([NaN, Infinity, -Infinity])"), "[null,null,null]");
    });

    IT("escapes a string as JSON.stringify escapes the same string", {
        Worlds worlds;
        // A literal of both languages.
        const std::string literal = R"src('q"b\\s/\n\r\t\b\f\0\x01\x1f\x7f \u{e9} \u{1f600} \u{2028}')src";
        EQUALS(Encode(worlds, literal), worlds.Js("JSON.stringify(" + literal + ")"));
        EQUALS(Encode(worlds, literal), "\"q\\\"b\\\\s/\\n\\r\\t\\b\\f\\u0000\\u0001\\u001f\x7f \xC3\xA9 \xF0\x9F\x98\x80 \xE2\x80\xA8\"");
    });

    IT("writes a sequence as an array, the empty table as [] and any other table as an object with its keys in order", {
        Worlds worlds;
        EQUALS(Encode(worlds, "{ 1, 'two', { 3 }, true }"), "[1,\"two\",[3],true]");
        EQUALS(Encode(worlds, "{}"), "[]");
        EQUALS(Encode(worlds, "{ list = {} }"), "{\"list\":[]}");
        EQUALS(Encode(worlds, "{ b = 1, a = { c = {} }, [2] = 'x', [-1] = 'y', [10] = 'z', B = 2 }"), "{\"-1\":\"y\",\"2\":\"x\",\"10\":\"z\",\"B\":2,\"a\":{\"c\":[]},\"b\":1}");
        EQUALS(Encode(worlds, "{ 'a', nil, 'c' }"), "{\"1\":\"a\",\"3\":\"c\"}");
        EQUALS(Encode(worlds, "{ [5] = 'integer', ['5'] = 'string' }"), "{\"5\":\"string\"}");
        EQUALS(Encode(worlds, "{ ['k\"\\n'] = 1 }"), "{\"k\\\"\\n\":1}");
    });

    IT("writes a table by what it holds itself, without running a metamethod", {
        Worlds worlds;
        LuaRuntimeTest::Run(worlds.lua, R"lua(
            ran = 0
            local function count() ran = ran + 1 end
            proxy = setmetatable({ own = 1 }, {
                __index = function() count() return 'computed' end,
                __pairs = function() count() return next, { fake = true } end,
                __len = function() count() return 99 end,
                __tostring = function() count() return 'proxy' end,
            })
        )lua");
        EQUALS(Encode(worlds, "{ proxy }"), "[{\"own\":1}]");
        EQUALS(worlds.Lua("ran"), "0");
    });

    IT("writes a value type as the object its toJSON gives in JavaScript", {
        Worlds worlds;
        EQUALS(Encode(worlds, "Vector3(1, 2, 3)"), "{\"x\":1,\"y\":2,\"z\":3}");
        const std::vector<std::pair<std::string, std::string>> values = {
            {"Vector2(0.1, -2.5)", "new Vector2(0.1, -2.5)"},
            {"Vector3(0.1, -2.5, 1e10)", "new Vector3(0.1, -2.5, 1e10)"},
            {"Vector4(1, 2, 3, 4.5)", "new Vector4(1, 2, 3, 4.5)"},
            {"Quaternion(0.5, 0.25, 0.125, 1)", "new Quaternion(0.5, 0.25, 0.125, 1)"},
            {"Color(0.2, 0.4, 0.6, 1)", "new Color(0.2, 0.4, 0.6, 1)"},
        };
        for (const auto &[lua, js] : values) {
            EQUALS(Encode(worlds, lua), worlds.Js("JSON.stringify(" + js + ")"));
        }
        EQUALS(Encode(worlds, "Quaternion(0.5, 0.25, 0.125, 1)"), "{\"w\":0.5,\"x\":0.25,\"y\":0.125,\"z\":1}");
        EQUALS(Encode(worlds, "Color(0.5, 0.25, 0.125, 1)"), "{\"r\":0.5,\"g\":0.25,\"b\":0.125,\"a\":1}");
        EQUALS(Encode(worlds, "Vector4(1, 2, 3, 4)"), "{\"x\":1,\"y\":2,\"z\":3,\"w\":4}");
        EQUALS(Encode(worlds, "{ at = Vector3(1, 2, 3), path = { Vector2(1, 2) } }"), "{\"at\":{\"x\":1,\"y\":2,\"z\":3},\"path\":[{\"x\":1,\"y\":2}]}");
    });

    IT("writes an object of a bound class as the properties its class names, in the order it names them", {
        Worlds worlds;
        lua_State *L = worlds.lua.State();
        ShapeDefinition().RegisterLuaGlobal(L, Bind::Side::Server);
        SquareDefinition().RegisterLuaGlobal(L, Bind::Side::Server);
        ChainDefinition().RegisterLuaGlobal(L, Bind::Side::Server);
        EQUALS(Encode(worlds, "JsonShape(4)"), "{\"label\":\"shape 4\",\"origin\":{\"x\":1,\"y\":2,\"z\":3},\"size\":4}");
        // A class that names none is written as the base that does.
        EQUALS(Encode(worlds, "{ JsonSquare(2) }"), "[{\"label\":\"shape 2\",\"origin\":{\"x\":1,\"y\":2,\"z\":3},\"size\":2}]");
        // What a getter throws fails the encoding, where it was.
        EQUALS(Encode(worlds, "{ shapes = { JsonShape(1), JsonShape(-1) } }"), "<failed> Cannot encode value.shapes[2].label: a shape cannot be smaller than nothing");
        const std::string endless = Encode(worlds, "JsonChain()");
        EQUALS(endless.rfind("<failed> Cannot encode value.next.next", 0) == 0, true);
        EQUALS(endless.ends_with(".next: it is nested too deeply"), true);
        EQUALS(lua_gettop(L), 0);
    });

    IT("refuses to name for JSON what is not a property, nothing at all, or anything twice", {
        const auto message = [](auto action) -> std::string {
            try {
                action();
            }
            catch (const std::logic_error &error) {
                return error.what();
            }
            return {};
        };
        auto definition = Bind::Class<Refused>("JsonRefused", "Refuses.").Property("value", &Refused::GetValue, "A value.");
        EQUALS(message([&] {
            definition.JsonProperties({"value", "missing"});
        }),
            "Bind: 'JsonRefused.missing' is named for JSON and is not a property of the class");
        EQUALS(message([&] {
            definition.JsonProperties({});
        }),
            "Bind: 'JsonRefused' names no JSON property");
        EQUALS(message([&] {
            definition.JsonProperties({"value"});
        }),
            "");
        EQUALS(message([&] {
            definition.JsonProperties({"value"});
        }),
            "Bind: 'JsonRefused' names its JSON properties twice");
    });

    IT("refuses what has no JSON form and says where", {
        Worlds worlds;
        EQUALS(Encode(worlds, "{ items = { 1, { name = function() end } } }"), "<failed> Cannot encode value.items[2].name: a function cannot be encoded as JSON");
        EQUALS(Encode(worlds, "{ job = coroutine.create(function() end) }"), "<failed> Cannot encode value.job: a coroutine cannot be encoded as JSON");
        EQUALS(Encode(worlds, "{ list = { TransferPlain() } }"), "<failed> Cannot encode value.list[1]: TransferPlain cannot be encoded as JSON");
        EQUALS(Encode(worlds, "TransferTicket(1)"), "<failed> Cannot encode value: TransferTicket cannot be encoded as JSON");
        EQUALS(Encode(worlds, "(function() local loop = { inner = {} } loop.inner.back = loop return loop end)()"), "<failed> Cannot encode value.inner.back: it refers back to itself");
        EQUALS(Encode(worlds, "{ a = { [true] = 1 } }"), "<failed> Cannot encode value.a: it has a key that is neither a string nor an integer");
        EQUALS(Encode(worlds, "{ [1.5] = 1 }"), "<failed> Cannot encode value: it has a key that is neither a string nor an integer");
        EQUALS(Encode(worlds, "{ name = 'caf\\xe9' }"), "<failed> Cannot encode value.name: string is not valid UTF-8");
        EQUALS(Encode(worlds, "{ { ['\\xff'] = 1 } }"), "<failed> Cannot encode value[1]: a key is not valid UTF-8");
        EQUALS(lua_gettop(worlds.lua.State()), 0);

        lua_State *L = worlds.lua.State();
        lua_newuserdatauv(L, 8, 0);
        EQUALS(LuaJson::Encode(L, -1).GetError(), "Cannot encode value: a userdata cannot be encoded as JSON");
        lua_pop(L, 1);
    });

    IT("writes 128 tables nested in one another and refuses one more", {
        Worlds worlds;
        EQUALS(Encode(worlds, NestedLua(128)), NestedJson(128, '[', ']'));
        const std::string refusal = Encode(worlds, NestedLua(129));
        EQUALS(refusal.rfind("<failed> Cannot encode value[1][1]", 0) == 0, true);
        EQUALS(refusal.ends_with("[1]: it is nested too deeply"), true);
    });

    IT("refuses a value too large to write instead of expanding it", {
        Worlds worlds;
        LuaRuntimeTest::Run(worlds.lua, "tree = { 1 } for _ = 1, 40 do tree = { tree, tree } end");
        const std::string doubled = Encode(worlds, "tree");
        EQUALS(doubled.rfind("<failed> Cannot encode value[", 0) == 0, true);
        EQUALS(doubled.ends_with("it is too large to encode"), true);
    });

    IT("leaves the stack as it found it and takes any index", {
        Worlds worlds;
        lua_State *L = worlds.lua.State();
        lua_pushinteger(L, 1);
        lua_pushstring(L, "two");
        lua_pushboolean(L, 1);
        EQUALS(LuaJson::Encode(L, 2).GetValue(), "\"two\"");
        EQUALS(LuaJson::Encode(L, -3).GetValue(), "1");
        EQUALS(lua_gettop(L), 3);
        // Not converted in place, as lua_tolstring would a number.
        EQUALS(lua_type(L, 1), LUA_TNUMBER);
        lua_settop(L, 0);
    });

    // --- Decoding ---

    IT("reads objects as tables, arrays as sequences and null as nil", {
        Worlds worlds;
        EQUALS(Decode(worlds, R"({"a":1,"b":[true,false,null,"x"],"c":{"d":1.5,"gone":null},"e":[],"f":{}})"), "");
        EQUALS(worlds.Lua("show(received)"), "{a=1,b={[1]=true,[2]=false,[4]=\"x\"},c={d=1.5},e={},f={}}");
        EQUALS(Decode(worlds, " [ 1 , [ 2 , [ 3 ] ] ] "), "");
        EQUALS(worlds.Lua("show(received) .. ' ' .. #received"), "{[1]=1,[2]={[1]=2,[2]={[1]=3}}} 2");
        EQUALS(lua_gettop(worlds.lua.State()), 0);
    });

    IT("reads a scalar on its own", {
        Worlds worlds;
        EQUALS(Decode(worlds, "5"), "");
        EQUALS(worlds.Lua("math.type(received) .. ' ' .. received"), "integer 5");
        EQUALS(Decode(worlds, "\"x\""), "");
        EQUALS(worlds.Lua("received"), "x");
        EQUALS(Decode(worlds, "true"), "");
        EQUALS(worlds.Lua("received"), "true");
        EQUALS(Decode(worlds, "null"), "");
        EQUALS(worlds.Lua("received"), "nil");
    });

    IT("reads an integral number within 2^53 as an integer and any other as a float", {
        Worlds worlds;
        EQUALS(Decode(worlds, "[1, 1.0, 1e3, -4, 0, 9007199254740992, -9007199254740992, 1.5, 9007199254740993, -9007199254740993, 1e300, 18446744073709551615, 0.5e1, -0.0]"), "");
        EQUALS(worlds.Lua("(function() local types = {} for index, value in ipairs(received) do types[index] = math.type(value) == 'integer' and 'i' or 'f' end return table.concat(types) end)()"), "iiiiiiifffffif");
        EQUALS(worlds.Lua("show(received)"), "{[1]=1,[2]=1,[3]=1000,[4]=-4,[5]=0,[6]=9007199254740992,[7]=-9007199254740992,[8]=1.5,[9]=9007199254740992.0,[10]=-9007199254740992.0,[11]=1e+300,[12]=1.8446744073709552e+19,[13]=5,[14]=-0.0}");
    });

    IT("reads the escapes of a string and keeps the key of an object a string", {
        Worlds worlds;
        EQUALS(Decode(worlds, R"(["caf\u00e9 \ud83d\ude00 \n\t\" \\ \/ \u0000|", "caf)"
                              "\xC3\xA9"
                              R"("])"),
            "");
        EQUALS(worlds.Lua("received[1] == 'caf\\u{e9} \\u{1f600} \\n\\t\" \\\\ / \\0|' and received[2] == 'caf\\u{e9}'"), "true");
        EQUALS(Decode(worlds, R"({"10":"ten","2":"two","":"empty"})"), "");
        EQUALS(worlds.Lua("show(received)"), "{=\"empty\",10=\"ten\",2=\"two\"}");
        EQUALS(worlds.Lua("tostring(received[10])"), "nil");
    });

    IT("keeps the last of two members with one key, as JSON.parse does", {
        Worlds worlds;
        EQUALS(Decode(worlds, R"({"a":1,"a":2,"b":1,"b":null})"), "");
        EQUALS(worlds.Lua("show(received)"), "{a=2}");
        EQUALS(worlds.Js(R"(JSON.stringify(JSON.parse('{"a":1,"a":2,"b":1,"b":null}')))"), R"({"a":2,"b":null})");
    });

    IT("says what is wrong with text that is not JSON, and pushes nothing", {
        Worlds worlds;
        for (const char *text : {"", " ", "{", "[1,", "{\"a\":}", "[1,]", "nope", "[1] x", "{'a':1}", "[NaN]", "[01]", "\"open", "{\"a\" 1}", "[\"\\x\"]"}) {
            const std::string error = Decode(worlds, text);
            EQUALS(error.rfind("Invalid JSON: ", 0) == 0 && error.size() > 14, true);
        }
        EQUALS(lua_gettop(worlds.lua.State()), 0);
        EQUALS(Decode(worlds, "[1,]").find("line 1, column 4") != std::string::npos, true);
    });

    IT("reads 128 arrays or objects nested in one another and refuses one more", {
        Worlds worlds;
        EQUALS(Decode(worlds, NestedJson(128, '[', ']')), "");
        const std::string arrays = Decode(worlds, NestedJson(129, '[', ']'));
        EQUALS(arrays.rfind("Invalid JSON: value[0][0]", 0) == 0, true);
        EQUALS(arrays.ends_with("[0]: it is nested too deeply"), true);
        std::string objects;
        for (int i = 0; i < 128; ++i) {
            objects += "{\"k\":";
        }
        const std::string tail(128, '}');
        EQUALS(Decode(worlds, objects + "1" + tail), "");
        const std::string members = Decode(worlds, "[7," + objects + "1" + tail + "]");
        EQUALS(members.rfind("Invalid JSON: value[1].k.k", 0) == 0, true);
        EQUALS(members.ends_with(".k: it is nested too deeply"), true);
        EQUALS(lua_gettop(worlds.lua.State()), 0);
    });

    IT("fails to read into a state that is out of memory without raising, and leaves the state usable", {
        Worlds worlds(4u << 20);
        std::string text = "[";
        for (int i = 0; i < 128; ++i) {
            text += (i == 0 ? "\"" : ",\"") + std::string(64 * 1024, static_cast<char>('a' + i % 26)) + "\"";
        }
        text += "]";
        const int top           = lua_gettop(worlds.other.State());
        const std::string error = Decode(worlds.other, text);
        EQUALS(error, "Cannot read JSON: not enough memory");
        EQUALS(lua_gettop(worlds.other.State()), top);
        EQUALS(Decode(worlds.other, "[1,2]"), "");
        EQUALS(worlds.Other("show(received)"), "{[1]=1,[2]=2}");
    });

    // --- Both ways, and both languages ---

    IT("reads back what it wrote", {
        Worlds worlds;
        LuaRuntimeTest::Run(worlds.lua, "original = { name = 'caf\\u{e9}', list = { 1, 2.5, 'x', true, { deep = { 1 } } }, count = 3, ratio = 0.1, big = math.maxinteger, empty = {} }");
        const std::string json = Encode(worlds, "original");
        EQUALS(json, "{\"big\":9223372036854775807,\"count\":3,\"empty\":[],\"list\":[1,2.5,\"x\",true,{\"deep\":[1]}],\"name\":\"caf\xC3\xA9\",\"ratio\":0.1}");
        EQUALS(Decode(worlds, json), "");
        // An integer beyond 2^53 comes back as the float JavaScript reads from the same text.
        EQUALS(worlds.Lua("show(received)"), "{big=9.2233720368547758e+18,count=3,empty={},list={[1]=1,[2]=2.5,[3]=\"x\",[4]=true,[5]={deep={[1]=1}}},name=\"caf\xC3\xA9\",ratio=0.1}");

        const std::string canonical = R"({"a":[1,2,{"b":"c"}],"n":-1.5,"s":"\u0001\"","t":true})";
        EQUALS(Decode(worlds, canonical), "");
        EQUALS(Encode(worlds, "received"), canonical);
    });

    IT("writes the JSON JavaScript writes for the same value", {
        Worlds worlds;
        // Written with the keys in the order both languages give them: integers ascending, then strings sorted.
        const std::string lua = "{ [2] = 'two', [10] = 'ten', alpha = { 1, 2.5, 'x', true, { 0.1 } }, at = Vector3(1, 2, 3), nested = { a = { 'deep' }, b = -0.5 }, text = 'q\"\\n\\u{e9}', zeta = 1e21 }";
        const std::string js  = "({ 2: 'two', 10: 'ten', alpha: [1, 2.5, 'x', true, [0.1]], at: new Vector3(1, 2, 3), nested: { a: ['deep'], b: -0.5 }, text: 'q\"\\n\\u{e9}', zeta: 1e21 })";
        EQUALS(Encode(worlds, lua), worlds.Js("JSON.stringify" + js));

        // In any order, the same JSON once the keys are sorted.
        const std::string shuffled = "({ zeta: 1, text: 'x', nested: { b: [], a: 1 }, alpha: [{ y: 1, x: 2 }], 10: 'ten', 2: 'two', tint: new Color(1, 0.5, 0.25, 1) })";
        const std::string fromLua  = Encode(worlds, "{ zeta = 1, text = 'x', nested = { b = {}, a = 1 }, alpha = { { y = 1, x = 2 } }, [10] = 'ten', [2] = 'two', tint = Color(1, 0.5, 0.25, 1) }");
        EQUALS(nlohmann::json::parse(fromLua).dump(), nlohmann::json::parse(worlds.Js("JSON.stringify" + shuffled)).dump());
    });

    IT("reads the value JavaScript reads from the same JSON", {
        Worlds worlds;
        const std::string text = R"({"list":[1,2.5,"x",true,[{"k":"v"}]],"n":-3,"big":9007199254740993,"s":"caf\u00e9 \ud83d\ude00","o":{"10":1,"b":0.1}})";
        EQUALS(Decode(worlds, text), "");
        const std::string fromJs = worlds.Js("JSON.stringify(JSON.parse('" + std::string(R"({"list":[1,2.5,"x",true,[{"k":"v"}]],"n":-3,"big":9007199254740993,"s":"caf\\u00e9 \\ud83d\\ude00","o":{"10":1,"b":0.1}})") + "'))");
        EQUALS(nlohmann::json::parse(Encode(worlds, "received")).dump(), nlohmann::json::parse(fromJs).dump());
    });

    // --- Through the dynamic interface ---

    IT("writes JSON through Value::ToJson the same in both languages", {
        Worlds worlds;
        EQUALS(worlds.Lua("Codec.json({ list = { 1, 'two', 2.5 }, at = Vector3(1, 2, 3) })"), "{\"at\":{\"x\":1,\"y\":2,\"z\":3},\"list\":[1,\"two\",2.5]}");
        EQUALS(worlds.Js("Codec.json({ at: new Vector3(1, 2, 3), list: [1, 'two', 2.5] })"), "{\"at\":{\"x\":1,\"y\":2,\"z\":3},\"list\":[1,\"two\",2.5]}");
        EQUALS(worlds.Lua("Codec.json(nil)"), "null");
        EQUALS(worlds.Js("Codec.json(undefined) + ' ' + Codec.json(null)"), "null null");
        EQUALS(worlds.Lua("Codec.json('text') .. Codec.json(5) .. Codec.json(true)"), "\"text\"5true");
        EQUALS(worlds.Js("Codec.json('text') + Codec.json(5) + Codec.json(true)"), "\"text\"5true");
        // What JSON.stringify asks a value for is asked for.
        EQUALS(worlds.Js("Codec.json({ toJSON() { return { shaped: 1 }; } })"), "{\"shaped\":1}");
        EQUALS(worlds.Js("Codec.json(new Date(0))"), "\"1970-01-01T00:00:00.000Z\"");
    });

    IT("hands the function why a value has no JSON, and JavaScript its own exception too", {
        Worlds worlds;
        EQUALS(worlds.Lua("Codec.json(print)"), "<failed> Cannot encode value: a function cannot be encoded as JSON");
        EQUALS(worlds.Js("Codec.json(() => 1)"), "<failed> a value of this kind has no JSON form");
        EQUALS(worlds.Js("Codec.json(Symbol('x'))"), "<failed> a value of this kind has no JSON form");

        EQUALS(worlds.Lua("(function() local loop = { inner = {} } loop.inner.back = loop return select(2, pcall(Codec.jsonOrThrow, loop)) end)()"), "Codec.jsonOrThrow: Cannot encode value.inner.back: it refers back to itself");
        // The TypeError of JSON.stringify is the one the script catches: the first throw wins.
        EQUALS(worlds.Js("(() => { const loop = {}; loop.back = loop; try { Codec.jsonOrThrow(loop); return 'no throw'; } catch (error) { return (error instanceof TypeError) + ' ' + error.message.includes('circular'); } })()"), "true true");
        EQUALS(worlds.Js("(() => { try { Codec.jsonOrThrow({ toJSON() { throw new RangeError('mine'); } }); return 'no throw'; } catch (error) { return error.name + ' ' + error.message; } })()"), "RangeError mine");
        EQUALS(worlds.Js("(() => { try { Codec.jsonOrThrow(10n); return 'no throw'; } catch (error) { return error.name; } })()"), "TypeError");
    });

    IT("stops a script that is terminated while its value is written or copied", {
        Worlds worlds;
        worlds.Enter([](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            const v8::Local<v8::Function> stop = Bind::MakeV8Function(isolate, context, [isolate](Bind::Call &) {
                isolate->TerminateExecution();
            });
            context->Global()->Set(context, v8pp::to_v8(isolate, "stop"), stop).Check();
        });
        for (const char *source : {
                 "Codec.json({ toJSON() { stop(); for (;;) {} } }); globalThis.survived = true",
                 // Terminated while what its toJSON threw is spelled for the function.
                 "Codec.json({ toJSON() { throw { toString() { stop(); for (;;) {} } }; } }); globalThis.survived = true",
                 "Codec.copy({ get a() { stop(); for (;;) {} } }); globalThis.survived = true",
             }) {
            // What a script is terminated with is no exception of its own: V8 has it as null.
            EQUALS(worlds.Js(source), "<threw> null");
            worlds.Enter([](v8::Isolate *isolate, v8::Local<v8::Context>) {
                isolate->CancelTerminateExecution();
            });
            EQUALS(worlds.Js("typeof survived"), "undefined");
        }
        EQUALS(worlds.Js("Codec.json([1])"), "[1]");
    });

    IT("reads JSON through Call::ParseJson in both languages, and hands the function what is wrong with it", {
        Worlds worlds;
        EQUALS(worlds.Lua("show(Codec.parse('{\"a\":[1,2,null,4],\"b\":{\"c\":true}}'))"), "{a={[1]=1,[2]=2,[4]=4},b={c=true}}");
        EQUALS(worlds.Js("JSON.stringify(Codec.parse('{\"a\":[1,2,null,4],\"b\":{\"c\":true}}'))"), "{\"a\":[1,2,null,4],\"b\":{\"c\":true}}");
        EQUALS(worlds.Lua("tostring(Codec.parse('null'))"), "nil");
        EQUALS(worlds.Js("String(Codec.parse('null'))"), "null");
        EQUALS(worlds.Lua("Codec.parse('{\"a\":}'):sub(1, 23)"), "<failed> Invalid JSON: ");
        EQUALS(worlds.Js("Codec.parse('{\"a\":}').slice(0, 22)"), "<failed> SyntaxError: ");
        // The function was told, so the script is not thrown at.
        EQUALS(worlds.Js("(() => { try { Codec.parse('nope'); return 'no throw'; } catch (error) { return 'threw'; } })()"), "no throw");
    });
});
