/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_json.h"

#include "lua_value_walk.h"

#include "lua_transfer.h"

#include "../bind/lua_class.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace Framework::Scripting::LuaJson {
    namespace {
        // Between quotes, escaped as JSON.stringify escapes: the quote, the backslash and the control characters,
        // those with a short form in that form. Everything else is written as it is.
        //
        // False, with `out` cut short, when the text would grow beyond `limit` bytes. An escape takes up to six
        // bytes for one, so the room is looked at before each one and not from the length of `text` alone.
        bool AppendString(std::string &out, std::string_view text, size_t limit) {
            static constexpr char kHex[] = "0123456789abcdef";
            // What is left of `text` from `from` on takes a byte each at the least, then the closing quote.
            const auto fits = [&](size_t from, size_t more) {
                const size_t needed = (text.size() - from) + more + 1;
                return out.size() <= limit && needed <= limit - out.size();
            };
            if (!fits(0, 1)) {
                return false;
            }
            out += '"';
            size_t plain = 0; // where the run of characters that need no escape starts
            for (size_t i = 0; i < text.size(); ++i) {
                const auto byte = static_cast<unsigned char>(text[i]);
                if (byte >= 0x20 && byte != '"' && byte != '\\') {
                    continue;
                }
                const bool brief = byte == '"' || byte == '\\' || byte == '\b' || byte == '\f' || byte == '\n' || byte == '\r' || byte == '\t';
                if (!fits(plain, brief ? 1 : 5)) {
                    return false;
                }
                out.append(text, plain, i - plain);
                plain = i + 1;
                switch (byte) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    out += "\\u00";
                    out += kHex[byte >> 4];
                    out += kHex[byte & 0x0F];
                    break;
                }
            }
            out.append(text, plain, text.size() - plain);
            out += '"';
            return true;
        }

        void AppendInteger(std::string &out, lua_Integer integer) {
            char buffer[24];
            const auto written = std::to_chars(buffer, buffer + sizeof(buffer), integer);
            out.append(buffer, written.ptr);
        }

        // A float as JavaScript spells the number (Number::toString in ECMA-262): the fewest digits that read back
        // as the same double, laid out plainly from 1e-6 up to 1e21 and with an exponent beyond. A NaN and an
        // infinity have no JSON form and are null, and a negative zero is 0, as JSON.stringify writes them.
        void AppendFloat(std::string &out, double number) {
            if (!std::isfinite(number)) {
                out += "null";
                return;
            }
            if (number == 0.0) {
                out += '0';
                return;
            }

            // "d.ddde+xx", with the fewest digits.
            char buffer[32];
            const auto written = std::to_chars(buffer, buffer + sizeof(buffer), number, std::chars_format::scientific);
            const char *cursor = buffer;
            if (*cursor == '-') {
                out += '-';
                ++cursor;
            }
            char digits[24];
            int count = 0;
            for (; cursor != written.ptr && *cursor != 'e'; ++cursor) {
                if (*cursor != '.') {
                    digits[count++] = *cursor;
                }
            }
            // The exponent always has its sign, which std::from_chars only reads when it is a minus.
            ++cursor;
            if (cursor != written.ptr && *cursor == '+') {
                ++cursor;
            }
            int exponent = 0;
            std::from_chars(cursor, written.ptr, exponent);

            // How many of the digits stand before the decimal point.
            const int point = exponent + 1;
            if (count <= point && point <= 21) {
                out.append(digits, static_cast<size_t>(count));
                out.append(static_cast<size_t>(point - count), '0');
            }
            else if (0 < point && point <= 21) {
                out.append(digits, static_cast<size_t>(point));
                out += '.';
                out.append(digits + point, static_cast<size_t>(count - point));
            }
            else if (-6 < point && point <= 0) {
                out += "0.";
                out.append(static_cast<size_t>(-point), '0');
                out.append(digits, static_cast<size_t>(count));
            }
            else {
                out += digits[0];
                if (count > 1) {
                    out += '.';
                    out.append(digits + 1, static_cast<size_t>(count - 1));
                }
                out += 'e';
                out += exponent < 0 ? '-' : '+';
                AppendInteger(out, std::abs(exponent));
            }
        }

        class Encoder final {
          public:
            Encoder(lua_State *L, const LuaValueLimits &limits): _L(L), _limits(limits), _valuesLeft(limits.values) {}

            // Writes the value at the absolute index `index`. The stack is not left as it was when it fails.
            bool Encode(int index) {
                if (_valuesLeft == 0) {
                    return Fail(kTooLarge);
                }
                --_valuesLeft;
                return EncodeCounted(index);
            }

            std::string Take() {
                return std::move(_out);
            }

            const std::string &GetError() const {
                return _error;
            }

            std::string Describe(std::string_view why) const {
                return "Cannot encode " + _path.ToString() + ": " + std::string(why);
            }

          private:
            static constexpr const char *kTooLarge = "it is too large to encode";

            // Writes a value that was counted against the bound already: the root by Encode, anything else with
            // the table or the object that holds it.
            bool EncodeCounted(int index) {
                switch (lua_type(_L, index)) {
                case LUA_TNIL: _out += "null"; break;
                case LUA_TBOOLEAN: _out += lua_toboolean(_L, index) != 0 ? "true" : "false"; break;
                case LUA_TNUMBER:
                    if (lua_isinteger(_L, index) != 0) {
                        AppendInteger(_out, lua_tointegerx(_L, index, nullptr));
                    }
                    else {
                        AppendFloat(_out, static_cast<double>(lua_tonumberx(_L, index, nullptr)));
                    }
                    break;
                case LUA_TSTRING: {
                    // The string is on the stack, which keeps it where `text` points while it is written.
                    size_t length    = 0;
                    const char *text = lua_tolstring(_L, index, &length);
                    if (length > Room()) {
                        return Fail(kTooLarge);
                    }
                    if (!LuaValues::IsValidUtf8({text, length})) {
                        return Fail("string is not valid UTF-8");
                    }
                    return Append({text, length});
                }
                case LUA_TTABLE: return EncodeTable(index);
                case LUA_TUSERDATA: return EncodeObject(index);
                case LUA_TFUNCTION: return Fail("a function cannot be encoded as JSON");
                case LUA_TTHREAD: return Fail("a coroutine cannot be encoded as JSON");
                default: return Fail("a userdata cannot be encoded as JSON");
                }
                return _out.size() <= _limits.bytes || Fail(kTooLarge);
            }

            bool EncodeTable(int index) {
                // Compared and never read through. An ancestor is on the stack, so no other table has its address.
                const void *table = lua_topointer(_L, index);
                if (std::find(_ancestors.begin(), _ancestors.end(), table) != _ancestors.end()) {
                    return Fail("it refers back to itself");
                }
                if (!Enter()) {
                    return false;
                }

                LuaValues::TableShape shape;
                LuaValues::ReadShape(_L, index, shape, _valuesLeft, Room());
                if (shape.foreignKey) {
                    return Fail("it has a key that is neither a string nor an integer");
                }
                if (shape.tooLarge) {
                    return Fail(kTooLarge);
                }
                // What the table holds is counted here, with its keys in hand and before any of it is written, so
                // that the keys of the tables open at one time never add up to more than the bounds.
                _valuesLeft -= shape.Count();
                _keyBytesHeld += shape.stringBytes;

                _ancestors.push_back(table);
                const bool encoded = shape.sequence ? EncodeSequence(index, shape.length) : EncodeProperties(index, shape);
                _ancestors.pop_back();
                --_depth;
                return encoded;
            }

            // An element a weak table lost since its keys were read is nil by now, and is written as null.
            bool EncodeSequence(int index, lua_Integer length) {
                _out += '[';
                for (lua_Integer position = 1; position <= length; ++position) {
                    if (position > 1) {
                        _out += ',';
                    }
                    lua_rawgeti(_L, index, position);
                    _path.EnterIndex(static_cast<size_t>(position));
                    if (!EncodeCounted(lua_gettop(_L))) {
                        return false;
                    }
                    _path.Leave();
                    lua_pop(_L, 1);
                }
                return Close(']');
            }

            // An entry a weak table lost since its keys were read is left out.
            bool EncodeProperties(int index, const LuaValues::TableShape &shape) {
                _out += '{';
                bool first = true;
                for (const lua_Integer key : shape.integers) {
                    const std::string name = std::to_string(key);
                    if (LuaValues::HasStringKey(shape, name)) {
                        continue;
                    }
                    if (lua_rawgeti(_L, index, key) == LUA_TNIL) {
                        lua_pop(_L, 1);
                        continue;
                    }
                    if (!BeginMember(name, first) || !EndMember()) {
                        return false;
                    }
                }
                for (const std::string &key : shape.strings) {
                    // Written from here on, or left out: either way no longer held beside the text.
                    _keyBytesHeld -= key.size();
                    if (!LuaValues::IsValidUtf8(key)) {
                        return Fail("a key is not valid UTF-8");
                    }
                    lua_pushlstring(_L, key.data(), key.size());
                    if (lua_rawget(_L, index) == LUA_TNIL) {
                        lua_pop(_L, 1);
                        continue;
                    }
                    if (!BeginMember(key, first) || !EndMember()) {
                        return false;
                    }
                }
                return Close('}');
            }

            // An object of a bound class is the object of the properties its class names for JSON.
            bool EncodeObject(int index) {
                const Framework::Scripting::Bind::Detail::LuaClass *definition = Bind::Detail::LuaClassAt(_L, index);
                if (definition == nullptr) {
                    return Fail("a userdata cannot be encoded as JSON");
                }
                const std::vector<const Framework::Scripting::Bind::Detail::LuaFunction *> *getters = Bind::Detail::LuaJsonProperties(*definition);
                if (getters == nullptr) {
                    return Fail(definition->desc.name + " cannot be encoded as JSON");
                }
                // A property may give an object in its turn.
                if (!Enter()) {
                    return false;
                }
                if (getters->size() > _valuesLeft) {
                    return Fail(kTooLarge);
                }
                _valuesLeft -= getters->size();
                _out += '{';
                bool first = true;
                for (const Framework::Scripting::Bind::Detail::LuaFunction *getter : *getters) {
                    if (!BeginMember(getter->name, first)) {
                        return false;
                    }
                    Bind::Detail::LuaPushProperty(_L, index, *getter);
                    if (!EndMember()) {
                        return false;
                    }
                }
                --_depth;
                return Close('}');
            }

            // Starts the member `name` of the object being written: its key, and its step in the path, so that
            // what fails to read or to write its value is said to have failed there.
            bool BeginMember(std::string_view name, bool &first) {
                if (!first) {
                    _out += ',';
                }
                first = false;
                if (!Append(name)) {
                    return false;
                }
                _out += ':';
                _path.EnterProperty(name);
                return true;
            }

            // Writes the value on top of the stack as the member that was started, and pops it.
            bool EndMember() {
                if (!EncodeCounted(lua_gettop(_L))) {
                    return false;
                }
                _path.Leave();
                lua_pop(_L, 1);
                return true;
            }

            // A table or an object is about to be written: a key and its value while its keys are read, then the
            // value being written and what reading a property takes.
            bool Enter() {
                if (_depth >= Framework::Scripting::LuaValues::kMaxDepth) {
                    return Fail("it is nested too deeply");
                }
                if (lua_checkstack(_L, 6) == 0) {
                    return Fail("the Lua stack cannot grow");
                }
                ++_depth;
                return true;
            }

            // Ends an array or an object, which the brackets and the commas may have taken past the bound.
            bool Close(char bracket) {
                _out += bracket;
                return _out.size() <= _limits.bytes || Fail(kTooLarge);
            }

            // The bytes the text may still grow by, less those of the keys that wait to be written into it.
            size_t Room() const {
                const size_t taken = _out.size() + _keyBytesHeld;
                return taken < _limits.bytes ? _limits.bytes - taken : 0;
            }

            // Writes `text` as a JSON string, unless the text would then be larger than it may.
            bool Append(std::string_view text) {
                return AppendString(_out, text, _limits.bytes) || Fail(kTooLarge);
            }

            // Keeps the innermost reason: an outer level only adds its own when nothing deeper said why.
            bool Fail(std::string_view why) {
                if (_error.empty()) {
                    _error = Describe(why);
                }
                return false;
            }

            lua_State *_L;
            const LuaValueLimits _limits;
            std::string _out;
            LuaValues::Path _path;
            std::vector<const void *> _ancestors;
            size_t _depth = 0;
            size_t _valuesLeft;
            // The bytes of the keys read from the tables being written that are not in the text yet.
            size_t _keyBytesHeld = 0;
            std::string _error;
        };

        // Builds the value on the Lua stack as the parser reads it. An open array or object is a table on the
        // stack, with the key of the member being read above it for an object.
        class Reader final: public nlohmann::json_sax<nlohmann::json> {
          public:
            explicit Reader(lua_State *L): _L(L) {}

            const std::string &GetError() const {
                return _error;
            }

            // nil cannot be stored: an element that is null is skipped, and a member that is null removes what an
            // earlier member of the same key left.
            bool null() override {
                if (!_open.empty() && _open.back().array) {
                    ++_open.back().next;
                    return true;
                }
                lua_pushnil(_L);
                return Store();
            }

            bool boolean(bool value) override {
                lua_pushboolean(_L, value ? 1 : 0);
                return Store();
            }

            bool number_integer(number_integer_t value) override {
                if (value >= -LuaValues::kExactInteger && value <= LuaValues::kExactInteger) {
                    lua_pushinteger(_L, static_cast<lua_Integer>(value));
                }
                else {
                    lua_pushnumber(_L, static_cast<lua_Number>(value));
                }
                return Store();
            }

            bool number_unsigned(number_unsigned_t value) override {
                if (value <= static_cast<number_unsigned_t>(LuaValues::kExactInteger)) {
                    lua_pushinteger(_L, static_cast<lua_Integer>(value));
                }
                else {
                    lua_pushnumber(_L, static_cast<lua_Number>(value));
                }
                return Store();
            }

            bool number_float(number_float_t value, const string_t &) override {
                LuaValues::PushNumber(_L, value);
                return Store();
            }

            bool string(string_t &value) override {
                lua_pushlstring(_L, value.data(), value.size());
                return Store();
            }

            bool binary(binary_t &) override {
                _error = "it holds binary data";
                return false;
            }

            bool start_object(std::size_t) override {
                return Open(false);
            }

            bool key(string_t &value) override {
                lua_pushlstring(_L, value.data(), value.size());
                return true;
            }

            bool end_object() override {
                return Close();
            }

            bool start_array(std::size_t) override {
                return Open(true);
            }

            bool end_array() override {
                return Close();
            }

            // "[json.exception.parse_error.101] parse error at line 1, column 4: syntax error ...", without what
            // only names the library.
            bool parse_error(std::size_t, const std::string &, const nlohmann::json::exception &exception) override {
                std::string_view what = exception.what();
                if (const size_t tag = what.find("] "); what.starts_with('[') && tag != std::string_view::npos) {
                    what.remove_prefix(tag + 2);
                }
                if (constexpr std::string_view prefix = "parse error "; what.starts_with(prefix)) {
                    what.remove_prefix(prefix.size());
                }
                _error = what;
                return false;
            }

          private:
            struct Level final {
                bool array       = false;
                lua_Integer next = 0; // the elements read so far, the null ones included
                int table        = 0; // where its table is on the stack
            };

            // The table, the key of a member and its value, and room for what pushes a scalar.
            bool Open(bool array) {
                if (_open.size() >= Framework::Scripting::LuaValues::kMaxDepth) {
                    _error = Where() + ": it is nested too deeply";
                    return false;
                }
                if (lua_checkstack(_L, 4) == 0) {
                    _error = "the Lua stack cannot grow";
                    return false;
                }
                lua_newtable(_L);
                _open.push_back({array, 0, lua_gettop(_L)});
                return true;
            }

            // Where in the value the text has got to, as the other refusals spell it: an element by its index
            // from 0, a member by its key, which lies above the table of its object while its value is read.
            std::string Where() const {
                std::string where = "value";
                for (const Level &level : _open) {
                    if (level.array) {
                        where += "[" + std::to_string(level.next) + "]";
                    }
                    else {
                        size_t length   = 0;
                        const char *key = lua_tolstring(_L, level.table + 1, &length);
                        where += ".";
                        LuaValues::AppendStepName(where, {key, length});
                    }
                }
                return where;
            }

            bool Close() {
                _open.pop_back();
                return Store();
            }

            // Puts the value on top of the stack where it belongs: in the array or the object being read, or
            // nowhere when it is the whole text.
            bool Store() {
                if (_open.empty()) {
                    return true;
                }
                Level &level = _open.back();
                if (level.array) {
                    lua_rawseti(_L, -2, ++level.next);
                }
                else {
                    lua_rawset(_L, -3);
                }
                return true;
            }

            lua_State *_L;
            std::vector<Level> _open;
            std::string _error;
        };
    } // namespace

    Utils::Result<std::string, std::string> Encode(lua_State *L, int index) {
        return Encode(L, index, LuaValueLimits {});
    }

    Utils::Result<std::string, std::string> Encode(lua_State *L, int index, const LuaValueLimits &limits) {
        using Result = Utils::Result<std::string, std::string>;

        Encoder encoder(L, limits);
        std::string unwound;
        const bool encoded = LuaValues::Protected(
            L,
            [&encoder]() {
                return encoder.Encode(2);
            },
            lua_absindex(L, index), false, unwound);
        if (!encoded) {
            return Result::Err(unwound.empty() ? encoder.GetError() : encoder.Describe(unwound));
        }
        return Result::Ok(encoder.Take());
    }

    bool Decode(lua_State *L, std::string_view text, std::string &error) {
        Reader reader(L);
        std::string unwound;
        const bool read = LuaValues::Protected(
            L,
            [&reader, text]() {
                return nlohmann::json::sax_parse(text.begin(), text.end(), &reader);
            },
            0, true, unwound);
        if (!read) {
            if (!unwound.empty()) {
                error = "Cannot read JSON: " + unwound;
            }
            else {
                error = "Invalid JSON: " + (reader.GetError().empty() ? std::string("it could not be read") : reader.GetError());
            }
        }
        return read;
    }
} // namespace Framework::Scripting::LuaJson
