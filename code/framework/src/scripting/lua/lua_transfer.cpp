/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_transfer.h"

#include "lua_value_walk.h"

#include "../bind/lua_callable.h"
#include "../bind/lua_class.h"

#include <utils/profiler.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <string_view>
#include <utility>

namespace Framework::Scripting {
    namespace {
        using Kind = TransferredValue::Kind;

        static_assert(sizeof(lua_Integer) == sizeof(uint64_t), "a Lua integer is the one word of a BigInt that fits it");

        struct HostType final {
            LuaTransfer::HostCopy copy;
            LuaTransfer::HostRebuild rebuild;
        };

        // Filled while the process starts, before any script runs, and only read afterwards.
        std::map<std::string, HostType, std::less<>> &HostTypes() {
            static std::map<std::string, HostType, std::less<>> types;
            return types;
        }

        bool IsNil(const TransferredValue &value) {
            return value.kind == Kind::Undefined || value.kind == Kind::Null;
        }

        // Both walks stop at the first thing they cannot do and keep why, with the path to it. What unwinds a walk
        // instead, a Lua error or an exception of a host type, is given the path by Describe.
        class Walk {
          public:
            const std::string &GetError() const {
                return _error;
            }

            std::string Describe(std::string_view why) const {
                return "Cannot copy " + _path.ToString() + ": " + std::string(why);
            }

          protected:
            explicit Walk(lua_State *L): _L(L) {}

            bool Fail(std::string_view why) {
                if (_error.empty()) {
                    _error = Describe(why);
                }
                return false;
            }

            lua_State *_L;
            LuaValues::Path _path;

          private:
            std::string _error;
        };

        class Copier final: public Walk {
          public:
            Copier(lua_State *L, const LuaValueLimits &limits): Walk(L), _limits(limits), _valuesLeft(limits.values), _bytesLeft(limits.bytes) {}

            // Copies the value at the absolute index `index`. The stack is not left as it was when it fails.
            bool Copy(int index, TransferredValue &out) {
                if (_valuesLeft == 0) {
                    return Fail(kTooLarge);
                }
                --_valuesLeft;
                return CopyCounted(index, out);
            }

            size_t GetCopiedCount() const {
                return _limits.values - _valuesLeft;
            }

          private:
            static constexpr const char *kTooLarge = "it is too large to copy";

            // Copies a value that was counted against the bound already: the root by Copy, anything else with the
            // table that holds it.
            bool CopyCounted(int index, TransferredValue &out) {
                switch (lua_type(_L, index)) {
                case LUA_TNIL: out.kind = Kind::Undefined; return true;
                case LUA_TBOOLEAN:
                    out.kind    = Kind::Boolean;
                    out.boolean = lua_toboolean(_L, index) != 0;
                    return true;
                case LUA_TNUMBER: return CopyNumber(index, out);
                case LUA_TSTRING: return CopyString(index, out);
                case LUA_TTABLE: return CopyTable(index, out);
                case LUA_TUSERDATA: return CopyObject(index, out);
                case LUA_TFUNCTION:
                    out.kind     = Kind::Function;
                    out.callable = Bind::LuaToCallable(_L, index);
                    return true;
                case LUA_TTHREAD: return Fail("a coroutine cannot be copied");
                default: return Fail("a userdata cannot be copied");
                }
            }

            // An integer JavaScript holds exactly is a Number, as a float is. Beyond that it is a BigInt, which
            // keeps every bit of it.
            bool CopyNumber(int index, TransferredValue &out) {
                if (lua_isinteger(_L, index) == 0) {
                    out.kind   = Kind::Number;
                    out.number = static_cast<double>(lua_tonumberx(_L, index, nullptr));
                    return true;
                }
                const lua_Integer integer = lua_tointegerx(_L, index, nullptr);
                if (integer >= -LuaValues::kExactInteger && integer <= LuaValues::kExactInteger) {
                    out.kind   = Kind::Number;
                    out.number = static_cast<double>(integer);
                    return true;
                }
                const uint64_t magnitude = integer < 0 ? uint64_t {0} - static_cast<uint64_t>(integer) : static_cast<uint64_t>(integer);
                if (!Spend(sizeof(magnitude))) {
                    return false;
                }
                out.kind    = Kind::BigInt;
                out.boolean = integer < 0;
                out.bytes.resize(sizeof(magnitude));
                std::memcpy(out.bytes.data(), &magnitude, sizeof(magnitude));
                return true;
            }

            // The string is on the stack, which keeps it where `text` points until it is copied.
            bool CopyString(int index, TransferredValue &out) {
                size_t length    = 0;
                const char *text = lua_tolstring(_L, index, &length);
                if (!Spend(length)) {
                    return false;
                }
                if (!LuaValues::IsValidUtf8({text, length})) {
                    return Fail("string is not valid UTF-8");
                }
                out.kind = Kind::String;
                out.text.assign(text, length);
                return true;
            }

            bool CopyTable(int index, TransferredValue &out) {
                // Compared and never read through. An ancestor is on the stack, so no other table has its address.
                const void *table = lua_topointer(_L, index);
                if (std::find(_ancestors.begin(), _ancestors.end(), table) != _ancestors.end()) {
                    return Fail("it refers back to itself");
                }
                if (_ancestors.size() >= Framework::Scripting::LuaValues::kMaxDepth) {
                    return Fail("it is nested too deeply");
                }
                // A key and its value while the keys are read, then the value being copied.
                if (lua_checkstack(_L, 4) == 0) {
                    return Fail("the Lua stack cannot grow");
                }

                LuaValues::TableShape shape;
                LuaValues::ReadShape(_L, index, shape, _valuesLeft, _bytesLeft);
                if (shape.foreignKey) {
                    return Fail("it has a key that is neither a string nor an integer");
                }
                if (shape.tooLarge) {
                    return Fail(kTooLarge);
                }
                // What the table holds is counted here, with its keys in hand and before any of it is copied, so
                // that the keys of the tables open at one time never add up to more than the bounds.
                _valuesLeft -= shape.Count();
                _bytesLeft -= shape.stringBytes;

                _ancestors.push_back(table);
                const bool copied = shape.sequence ? CopySequence(index, shape.length, out) : CopyProperties(index, shape, out);
                _ancestors.pop_back();
                return copied;
            }

            // An element a weak table lost since its keys were read is nil by now, and is copied as such.
            bool CopySequence(int index, lua_Integer length, TransferredValue &out) {
                out.kind = Kind::Array;
                out.items.resize(static_cast<size_t>(length));
                for (lua_Integer position = 1; position <= length; ++position) {
                    lua_rawgeti(_L, index, position);
                    _path.EnterIndex(static_cast<size_t>(position));
                    if (!CopyCounted(lua_gettop(_L), out.items[static_cast<size_t>(position) - 1])) {
                        return false;
                    }
                    _path.Leave();
                    lua_pop(_L, 1);
                }
                return true;
            }

            // In the order Bind::Value::Keys lists them, so that a copy does not depend on where the keys fell in
            // the table. An entry a weak table lost since its keys were read is left out.
            bool CopyProperties(int index, LuaValues::TableShape &shape, TransferredValue &out) {
                out.kind = Kind::Object;
                out.properties.reserve(shape.Count());
                for (const lua_Integer key : shape.integers) {
                    std::string name = std::to_string(key);
                    if (LuaValues::HasStringKey(shape, name)) {
                        continue;
                    }
                    if (lua_rawgeti(_L, index, key) == LUA_TNIL) {
                        lua_pop(_L, 1);
                        continue;
                    }
                    if (!Spend(name.size()) || !CopyProperty(std::move(name), out)) {
                        return false;
                    }
                }
                // The keys were counted when they were read, and move into the copy.
                for (std::string &key : shape.strings) {
                    if (!LuaValues::IsValidUtf8(key)) {
                        return Fail("a key is not valid UTF-8");
                    }
                    lua_pushlstring(_L, key.data(), key.size());
                    if (lua_rawget(_L, index) == LUA_TNIL) {
                        lua_pop(_L, 1);
                        continue;
                    }
                    if (!CopyProperty(std::move(key), out)) {
                        return false;
                    }
                }
                return true;
            }

            // Copies the value on top of the stack as the property `name` of `out`, and pops it.
            bool CopyProperty(std::string name, TransferredValue &out) {
                auto &[key, value] = out.properties.emplace_back(std::move(name), TransferredValue {});
                _path.EnterProperty(key);
                if (!CopyCounted(lua_gettop(_L), value)) {
                    return false;
                }
                _path.Leave();
                lua_pop(_L, 1);
                return true;
            }

            // The userdata is on the stack, which keeps the object a host type reads its payload from.
            bool CopyObject(int index, TransferredValue &out) {
                const Framework::Scripting::Bind::Detail::LuaClass *definition = Bind::Detail::LuaClassAt(_L, index);
                if (definition == nullptr) {
                    return Fail("a userdata cannot be copied");
                }
                if (!CopyAs(*definition, index, out)) {
                    return Fail("a " + definition->desc.name + " cannot be copied");
                }
                return Spend(out.bytes.size());
            }

            // As the host type of the class, or else of the first of its bases that has one.
            bool CopyAs(const Framework::Scripting::Bind::Detail::LuaClass &definition, int index, TransferredValue &out) {
                const auto &types = HostTypes();
                const auto type   = types.find(definition.desc.name);
                if (type != types.end() && type->second.copy(_L, index, out.bytes)) {
                    out.kind = Kind::HostObject;
                    out.text = type->first;
                    return true;
                }
                return std::any_of(definition.bases.begin(), definition.bases.end(), [&](const Framework::Scripting::Bind::Detail::LuaBase &base) {
                    return CopyAs(*base.definition, index, out);
                });
            }

            // Counts `bytes` of strings or binary data against the copy's bound.
            bool Spend(size_t bytes) {
                if (bytes > _bytesLeft) {
                    return Fail(kTooLarge);
                }
                _bytesLeft -= bytes;
                return true;
            }

            const LuaValueLimits _limits;
            std::vector<const void *> _ancestors;
            size_t _valuesLeft;
            size_t _bytesLeft;
        };

        class Builder final: public Walk {
          public:
            explicit Builder(lua_State *L): Walk(L) {}

            // Pushes `value`: one value. The stack is not left as it was when it fails.
            bool Build(const TransferredValue &value) {
                switch (value.kind) {
                case Kind::Undefined:
                case Kind::Null: lua_pushnil(_L); return true;
                case Kind::Boolean: lua_pushboolean(_L, value.boolean ? 1 : 0); return true;
                case Kind::Number:
                case Kind::Date: LuaValues::PushNumber(_L, value.number); return true;
                case Kind::BigInt: return BuildBigInt(value);
                case Kind::String: lua_pushlstring(_L, value.text.data(), value.text.size()); return true;
                case Kind::ArrayBuffer:
                case Kind::TypedArray: lua_pushlstring(_L, reinterpret_cast<const char *>(value.bytes.data()), value.bytes.size()); return true;
                case Kind::Array: return BuildSequence(value.items, false);
                case Kind::Set: return BuildSequence(value.items, true);
                // The name, the message and the stack of an Error ride among its properties.
                case Kind::Object:
                case Kind::Error: return BuildProperties(value);
                case Kind::Map: return BuildMap(value);
                case Kind::HostObject: return BuildHostObject(value);
                case Kind::RegExp: return Fail("a RegExp cannot be copied to Lua");
                case Kind::Function: return BuildFunction(value);
                }
                return Fail("it is of a kind Lua does not know");
            }

          private:
            // A table is about to be built: it, a key and a value need room, and so does what builds a scalar in it.
            bool Enter() {
                if (_depth >= Framework::Scripting::LuaValues::kMaxDepth) {
                    return Fail("it is nested too deeply");
                }
                if (lua_checkstack(_L, 4) == 0) {
                    return Fail("the Lua stack cannot grow");
                }
                ++_depth;
                return true;
            }

            static int Capacity(size_t count) {
                return static_cast<int>(std::min<size_t>(count, INT_MAX));
            }

            bool BuildBigInt(const TransferredValue &value) {
                uint64_t magnitude = 0;
                bool fits          = true;
                for (size_t word = 0; word < value.bytes.size() / sizeof(uint64_t); ++word) {
                    uint64_t bits = 0;
                    std::memcpy(&bits, value.bytes.data() + word * sizeof(uint64_t), sizeof(bits));
                    if (word == 0) {
                        magnitude = bits;
                    }
                    else if (bits != 0) {
                        fits = false;
                    }
                }
                // The lowest integer has a magnitude the highest does not reach.
                const uint64_t lowest = uint64_t {1} << 63;
                if (!fits || magnitude > lowest || (magnitude == lowest && !value.boolean)) {
                    return Fail("a BigInt this large does not fit a Lua integer");
                }
                lua_pushinteger(_L, static_cast<lua_Integer>(value.boolean ? uint64_t {0} - magnitude : magnitude));
                return true;
            }

            // A null or undefined element is a hole. The table is made with room for every element all the same.
            // The members of a Set arrive in the order it holds them.
            bool BuildSequence(const std::vector<TransferredValue> &items, bool set) {
                if (!Enter()) {
                    return false;
                }
                lua_createtable(_L, Capacity(items.size()), 0);
                for (size_t i = 0; i < items.size(); ++i) {
                    if (IsNil(items[i])) {
                        continue;
                    }
                    if (set) {
                        _path.EnterMember(i);
                    }
                    else {
                        _path.EnterIndex(i);
                    }
                    if (!Build(items[i])) {
                        return false;
                    }
                    _path.Leave();
                    lua_rawseti(_L, -2, static_cast<lua_Integer>(i) + 1);
                }
                --_depth;
                return true;
            }

            // Every key stays the string it is: one that spells an integer is not guessed back into one.
            bool BuildProperties(const TransferredValue &value) {
                if (!Enter()) {
                    return false;
                }
                lua_createtable(_L, 0, Capacity(value.properties.size()));
                for (const auto &[name, field] : value.properties) {
                    if (IsNil(field)) {
                        continue;
                    }
                    lua_pushlstring(_L, name.data(), name.size());
                    _path.EnterProperty(name);
                    if (!Build(field)) {
                        return false;
                    }
                    _path.Leave();
                    // A function of another runtime can be called as a method of this table from now on.
                    if (field.kind == Kind::Function) {
                        Bind::LuaNoteHolder(_L, -3, -1);
                    }
                    lua_rawset(_L, -3);
                }
                --_depth;
                return true;
            }

            // Keys and values interleaved. A table takes a string or a number for a key, and a Map distinguishes
            // 2 from "2" as a table does.
            bool BuildMap(const TransferredValue &value) {
                if (!Enter()) {
                    return false;
                }
                lua_createtable(_L, 0, Capacity(value.items.size() / 2));
                for (size_t i = 0; i + 1 < value.items.size(); i += 2) {
                    const TransferredValue &key   = value.items[i];
                    const TransferredValue &entry = value.items[i + 1];
                    _path.EnterMapKey(i / 2);
                    if (key.kind == Kind::String) {
                        lua_pushlstring(_L, key.text.data(), key.text.size());
                    }
                    else if (key.kind != Kind::Number) {
                        return Fail("only a string or a number can be a key in Lua");
                    }
                    else if (std::isnan(key.number)) {
                        return Fail("NaN cannot be a key in Lua");
                    }
                    else {
                        LuaValues::PushNumber(_L, key.number);
                    }
                    _path.Leave();

                    if (IsNil(entry)) {
                        lua_pop(_L, 1);
                        continue;
                    }
                    _path.EnterMapValue(i / 2);
                    if (!Build(entry)) {
                        return false;
                    }
                    _path.Leave();
                    lua_rawset(_L, -3);
                }
                --_depth;
                return true;
            }

            // One that was exported between JavaScript runtimes alone (function_references.h) has no callable.
            bool BuildFunction(const TransferredValue &value) {
                if (value.callable == nullptr) {
                    return Fail("a function cannot be copied");
                }
                Bind::LuaPushCallable(_L, value.callable);
                return true;
            }

            bool BuildHostObject(const TransferredValue &value) {
                const auto &types = HostTypes();
                const auto type   = types.find(value.text);
                if (type == types.end()) {
                    return Fail("host type '" + value.text + "' is not available in Lua");
                }
                if (lua_checkstack(_L, LUA_MINSTACK) == 0) {
                    return Fail("the Lua stack cannot grow");
                }
                const int top = lua_gettop(_L);
                if (!type->second.rebuild(_L, value.bytes) || lua_gettop(_L) != top + 1) {
                    return Fail("host type '" + value.text + "' could not be rebuilt");
                }
                return true;
            }

            size_t _depth = 0;
        };
    } // namespace

    void LuaTransfer::RegisterHostType(std::string name, HostCopy copy, HostRebuild rebuild) {
        HostTypes().insert_or_assign(std::move(name), HostType {std::move(copy), std::move(rebuild)});
    }

    void LuaTransfer::UnregisterHostType(std::string_view name) {
        auto &types = HostTypes();
        if (const auto type = types.find(name); type != types.end()) {
            types.erase(type);
        }
    }

    Utils::Result<TransferredValue, std::string> LuaTransfer::Copy(lua_State *L, int index) {
        return Copy(L, index, LuaValueLimits {});
    }

    Utils::Result<TransferredValue, std::string> LuaTransfer::Copy(lua_State *L, int index, const LuaValueLimits &limits) {
        using Result = Utils::Result<TransferredValue, std::string>;
        FW_PROFILE_SCOPE_N("Scripting::CopyLuaValue");

        Copier copier(L, limits);
        TransferredValue out;
        std::string unwound;
        const bool copied = LuaValues::Protected(
            L,
            [&copier, &out]() {
                return copier.Copy(2, out);
            },
            lua_absindex(L, index), false, unwound);
        FW_PROFILE_VALUE(copier.GetCopiedCount());
        if (!copied) {
            return Result::Err(unwound.empty() ? copier.GetError() : copier.Describe(unwound));
        }
        return Result::Ok(std::move(out));
    }

    bool LuaTransfer::Rebuild(lua_State *L, const TransferredValue &value, std::string &error) {
        FW_PROFILE_SCOPE_N("Scripting::RebuildLuaValue");

        Builder builder(L);
        std::string unwound;
        const bool built = LuaValues::Protected(
            L,
            [&builder, &value]() {
                return builder.Build(value);
            },
            0, true, unwound);
        if (!built) {
            error = unwound.empty() ? builder.GetError() : builder.Describe(unwound);
        }
        return built;
    }
} // namespace Framework::Scripting
