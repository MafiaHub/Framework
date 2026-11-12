/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "call.h"
#include "lua_callable.h"
#include "lua_class.h"
#include "value.h"

#include "../lua/lua_json.h"
#include "../lua/lua_runtime.h"
#include "../lua/lua_transfer.h"

#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <charconv>
#include <exception>
#include <iterator>
#include <new>
#include <optional>
#include <string>
#include <vector>

// The Lua side of Call and Value.
//
// A value is a slot of the stack of the thread that runs the function, by its absolute index: the arguments are
// where the script put them, and what the function makes or reads out of a table is pushed above them and stays
// there until the function returns. Slot 0 is nil, so reading nil costs no stack, and so is any index above the top.
// An index means this function's part of the stack only while the function is the one running on the thread, which
// is when a Value is led here (value.h).
//
// Tables are read and written raw. The script called the function, so a metamethod would be script code running
// inside host code, where it could raise through the function or wait. The one place a script's code does run is
// ToDisplayString, in a protected call.
//
// Nothing is raised while the function runs, apart from what Lua itself raises when it is out of memory or of
// stack, which unwinds the function as a C++ exception does. What the function threw is kept as text and raised
// by LuaInvokeNative, outside the catch block and after the Call is gone. That is also where a function that
// deferred its result has its coroutine wait.

namespace Framework::Scripting::Bind {
    namespace {
        // Not const, so that no other object can come to share its address.
        char holderKey = 0;

        static_assert(alignof(NativeFunction) <= alignof(Detail::LuaMaxAlign), "a NativeFunction fits the alignment of a userdata");

        // tostring(value), for a protected call.
        int Spell(lua_State *L) {
            luaL_tolstring(L, 1, nullptr);
            return 1;
        }

        class LuaCall final
            : public Call
            , public Detail::ValueBackend {
          public:
            LuaCall(lua_State *L, bool method): _L(L), _receiver(method && lua_gettop(L) >= 1), _first(method ? 2 : 1), _count(static_cast<size_t>(std::max(lua_gettop(L) - (method ? 1 : 0), 0))) {}

            // What the function leaves the script: the number of results, now on top of the stack, or the message
            // to raise in `error`, or in `deferred` the result it left for later, which then is what counts.
            int Finish(std::optional<std::string> &error, std::shared_ptr<Detail::LuaDeferred> &deferred) {
                if (_thrown) {
                    error = std::move(_thrown);
                    return 0;
                }
                deferred = std::move(_deferred);
                if (deferred != nullptr || !_returned) {
                    return 0;
                }
                Reserve(1);
                Push(_result);
                return 1;
            }

            // --- Call ---

            Language GetLanguage() const override {
                return Language::Lua;
            }

            Side GetSide() const override {
                return LuaRuntime::From(_L)->Options().side;
            }

            std::string ResourceName() const override {
                return LuaRuntime::From(_L)->Options().resourceName;
            }

            ResourceManager *GetResourceManager() const override {
                return Detail::CallResourceManager(LuaRuntime::From(_L));
            }

            size_t ArgCount() const override {
                return _count;
            }

            Value Arg(size_t index) const override {
                return At(index < _count ? _first + static_cast<int>(index) : 0);
            }

            Value This() const override {
                return At(_receiver ? 1 : 0);
            }

            Value Nil() override {
                return At(0);
            }

            Value Null() override {
                return At(0);
            }

            Value Boolean(bool value) override {
                Reserve(1);
                lua_pushboolean(_L, value ? 1 : 0);
                return Top();
            }

            Value Number(double value) override {
                Reserve(1);
                lua_pushnumber(_L, static_cast<lua_Number>(value));
                return Top();
            }

            Value Integer(int64_t value) override {
                Reserve(1);
                lua_pushinteger(_L, static_cast<lua_Integer>(value));
                return Top();
            }

            Value String(std::string_view value) override {
                Reserve(1);
                lua_pushlstring(_L, value.data(), value.size());
                return Top();
            }

            Value NewObject() override {
                Reserve(1);
                lua_newtable(_L);
                return Top();
            }

            Value NewArray() override {
                return NewObject();
            }

            Value NewFunction(NativeFunction function) override {
                PushLuaFunction(_L, std::move(function));
                return Top();
            }

            Utils::Result<Value, std::string> FromTransfer(const TransferredValue &value) override {
                std::string error;
                if (!LuaTransfer::Rebuild(_L, value, error)) {
                    return Utils::Result<Value, std::string>::Err(std::move(error));
                }
                return Utils::Result<Value, std::string>::Ok(Top());
            }

            Utils::Result<Value, std::string> ParseJson(std::string_view text) override {
                std::string error;
                if (!LuaJson::Decode(_L, text, error)) {
                    return Utils::Result<Value, std::string>::Err(std::move(error));
                }
                return Utils::Result<Value, std::string>::Ok(Top());
            }

            void Return(Value value) override {
                _result   = Slot(value);
                _returned = true;
            }

            void ThrowTypeError(std::string message) override {
                ThrowError(std::move(message));
            }

            void ThrowError(std::string message) override {
                if (!_thrown) {
                    _thrown = std::move(message);
                }
            }

            bool HasThrown() const override {
                return _thrown.has_value();
            }

            void WriteOutput(OutputLevel level, std::string_view line) override {
                LuaRuntime::From(_L)->WriteOutput(line, level);
            }

            size_t OutputLineLimit() const override {
                return LuaRuntime::kMaxLineBytes;
            }

            Deferred Defer() override {
                if (_deferred == nullptr) {
                    _deferred = Detail::LuaDefer(_L);
                }
                return Deferred(_deferred);
            }

            // --- ValueBackend ---

            ValueKind KindOf(size_t slot, bool exact) const override {
                const int index = Index(slot);
                switch (index != 0 ? lua_type(_L, index) : LUA_TNIL) {
                case LUA_TNIL: return ValueKind::Nil;
                case LUA_TBOOLEAN: return ValueKind::Boolean;
                case LUA_TNUMBER: return ValueKind::Number;
                case LUA_TSTRING: return ValueKind::String;
                case LUA_TFUNCTION: return ValueKind::Function;
                case LUA_TTABLE: return exact && IsSequence(index) ? ValueKind::Array : ValueKind::Object;
                case LUA_TUSERDATA: return Detail::LuaClassAt(_L, index) != nullptr ? ValueKind::Bound : ValueKind::Other;
                default: return ValueKind::Other;
                }
            }

            bool IsListAt(size_t slot) override {
                if (!Is(slot, LUA_TTABLE)) {
                    return false;
                }
                Reserve(2);
                lua_pushnil(_L);
                if (lua_next(_L, Index(slot)) == 0) {
                    return true;
                }
                lua_pop(_L, 2);
                return IsSequence(Index(slot));
            }

            bool BooleanOf(size_t slot) const override {
                return Is(slot, LUA_TBOOLEAN) && lua_toboolean(_L, Index(slot)) != 0;
            }

            double NumberOf(size_t slot) const override {
                return Is(slot, LUA_TNUMBER) ? static_cast<double>(lua_tonumberx(_L, Index(slot), nullptr)) : 0.0;
            }

            std::optional<int64_t> IntegerOf(size_t slot) const override {
                // The type first: lua_tointegerx alone would convert a string.
                if (!Is(slot, LUA_TNUMBER)) {
                    return std::nullopt;
                }
                int exact                 = 0;
                const lua_Integer integer = lua_tointegerx(_L, Index(slot), &exact);
                return exact != 0 ? std::optional<int64_t>(integer) : std::nullopt;
            }

            std::string StringOf(size_t slot) const override {
                if (!Is(slot, LUA_TSTRING)) {
                    return {};
                }
                size_t length    = 0;
                const char *text = lua_tolstring(_L, Index(slot), &length);
                return std::string(text, length);
            }

            // A __tostring that raises, or that tries to wait, fails here and nowhere else. One the deadline stops
            // does too, and the script is stopped again as soon as the function hands control back to it.
            std::string DisplayStringOf(size_t slot) override {
                Reserve(2);
                lua_pushcfunction(_L, Spell);
                Push(slot);
                std::string text;
                if (lua_pcall(_L, 1, 1, 0) == LUA_OK) {
                    size_t length       = 0;
                    const char *spelled = lua_tolstring(_L, -1, &length);
                    text.assign(spelled, length);
                }
                lua_pop(_L, 1);
                return text;
            }

            void *BoundOf(size_t slot, const Detail::LuaClass *definition) const override {
                const int index = Index(slot);
                return index != 0 ? Detail::LuaToObject(_L, index, definition) : nullptr;
            }

            Value GetKey(size_t slot, std::string_view key) override {
                if (!Is(slot, LUA_TTABLE)) {
                    return At(0);
                }
                Reserve(1);
                lua_pushlstring(_L, key.data(), key.size());
                if (lua_rawget(_L, Index(slot)) != LUA_TNIL) {
                    return Top();
                }
                lua_pop(_L, 1);
                const std::optional<lua_Integer> integer = IntegerKey(Index(slot), key);
                if (!integer || lua_rawgeti(_L, Index(slot), *integer) == LUA_TNIL) {
                    lua_pop(_L, integer ? 1 : 0);
                    return At(0);
                }
                return Top();
            }

            void SetKey(size_t slot, std::string_view key, const Value &value) override {
                if (!IsWritable(slot)) {
                    return;
                }
                Reserve(2);
                // The string key when the table has it, so that it is replaced; the integer key otherwise.
                lua_pushlstring(_L, key.data(), key.size());
                const bool hasString = lua_rawget(_L, Index(slot)) != LUA_TNIL;
                lua_pop(_L, 1);
                const std::optional<lua_Integer> integer = hasString ? std::nullopt : IntegerKey(Index(slot), key);
                if (integer) {
                    Push(Slot(value));
                    lua_rawseti(_L, Index(slot), *integer);
                    return;
                }
                lua_pushlstring(_L, key.data(), key.size());
                Push(Slot(value));
                lua_rawset(_L, Index(slot));
            }

            std::vector<std::string> KeysOf(size_t slot) override {
                std::vector<std::string> strings;
                if (!Is(slot, LUA_TTABLE)) {
                    return strings;
                }
                std::vector<lua_Integer> integers;
                bool others = false;
                Reserve(2);
                lua_pushnil(_L);
                while (lua_next(_L, Index(slot)) != 0) {
                    lua_pop(_L, 1);
                    // The key stays as it is for lua_next: lua_tolstring would turn a number into a string.
                    if (lua_type(_L, -1) == LUA_TSTRING) {
                        size_t length    = 0;
                        const char *text = lua_tolstring(_L, -1, &length);
                        strings.emplace_back(text, length);
                    }
                    else if (lua_isinteger(_L, -1) != 0) {
                        integers.push_back(lua_tointegerx(_L, -1, nullptr));
                    }
                    else {
                        others = true;
                    }
                }
                std::sort(integers.begin(), integers.end());
                std::sort(strings.begin(), strings.end());

                // The keys of an Array are 1..n, and are told as the indices from zero they are everywhere else.
                const bool sequence = !integers.empty() && strings.empty() && !others && integers.front() == 1 && integers.back() == static_cast<lua_Integer>(integers.size());
                std::vector<std::string> keys;
                keys.reserve(integers.size() + strings.size());
                for (const lua_Integer integer : integers) {
                    keys.push_back(std::to_string(sequence ? integer - 1 : integer));
                }
                keys.insert(keys.end(), std::make_move_iterator(strings.begin()), std::make_move_iterator(strings.end()));
                return keys;
            }

            size_t LengthOf(size_t slot) const override {
                return Is(slot, LUA_TTABLE) ? static_cast<size_t>(lua_rawlen(_L, Index(slot))) : 0;
            }

            Value ElementAt(size_t slot, size_t zeroBased) override {
                if (!Is(slot, LUA_TTABLE)) {
                    return At(0);
                }
                Reserve(1);
                if (lua_rawgeti(_L, Index(slot), static_cast<lua_Integer>(zeroBased) + 1) == LUA_TNIL) {
                    lua_pop(_L, 1);
                    return At(0);
                }
                return Top();
            }

            void AppendTo(size_t slot, const Value &value) override {
                if (!IsWritable(slot)) {
                    return;
                }
                Reserve(1);
                Push(Slot(value));
                lua_rawseti(_L, Index(slot), static_cast<lua_Integer>(lua_rawlen(_L, Index(slot))) + 1);
            }

            std::shared_ptr<ScriptCallable> CallableOf(size_t slot) override {
                return Is(slot, LUA_TFUNCTION) ? LuaToCallable(_L, Index(slot)) : nullptr;
            }

            Utils::Result<TransferredValue, std::string> TransferOf(size_t slot) override {
                const int index = Index(slot);
                return index != 0 ? LuaTransfer::Copy(_L, index) : Utils::Result<TransferredValue, std::string>::Ok();
            }

            Utils::Result<std::string, std::string> JsonOf(size_t slot) override {
                const int index = Index(slot);
                return index != 0 ? LuaJson::Encode(_L, index) : Utils::Result<std::string, std::string>::Ok("null");
            }

          protected:
            void *SelfAs(const Detail::LuaClass *definition) const override {
                return _receiver ? Detail::LuaToObject(_L, 1, definition) : nullptr;
            }

            Value WrapBound(const Detail::LuaClass *definition, const char *name, Detail::BoundBuilder build, void *source) override {
                Reserve(2);
                bool materialised = false;
                if (definition != nullptr) {
                    materialised = lua_rawgetp(_L, LUA_REGISTRYINDEX, definition) != LUA_TNIL;
                    lua_pop(_L, 1);
                }
                if (!materialised) {
                    ThrowError(std::string(name) + " is not available in this Lua state");
                    return At(0);
                }
                // A block without its metatable is never finalized, so a constructor that throws leaves no object.
                build(Detail::LuaAllocateObject(_L, definition, name, definition->size), source);
                lua_setmetatable(_L, -2);
                return Top();
            }

          private:
            Value At(int index) const {
                return Detail::ValueAccess::Make(*this, static_cast<size_t>(index));
            }

            Value Top() const {
                return At(lua_gettop(_L));
            }

            // The stack index of `slot`, or 0, which is nil, for a slot the function does not have.
            int Index(size_t slot) const {
                return slot <= static_cast<size_t>(lua_gettop(_L)) ? static_cast<int>(slot) : 0;
            }

            size_t Slot(const Value &value) const {
                return Detail::ValueAccess::SlotIn(*this, value);
            }

            bool Is(size_t slot, int type) const {
                const int index = Index(slot);
                return index != 0 && lua_type(_L, index) == type;
            }

            // The integer key `key` names in the table at `index`, when it is the decimal text of an integer: the
            // index from 1 of the element it counts from zero in an Array, that integer itself in any other table.
            std::optional<lua_Integer> IntegerKey(int index, std::string_view key) const {
                lua_Integer integer = 0;
                const auto parsed   = std::from_chars(key.data(), key.data() + key.size(), integer);
                if (parsed.ec != std::errc() || parsed.ptr != key.data() + key.size() || std::to_string(integer) != key) {
                    return std::nullopt;
                }
                return integer >= 0 && integer < LUA_MAXINTEGER && IsSequence(index) ? integer + 1 : integer;
            }

            void Reserve(int slots) const {
                luaL_checkstack(_L, slots, "too many values in a call");
            }

            // Pushes the value of `slot`. The caller has reserved the room.
            void Push(size_t slot) const {
                if (const int index = Index(slot)) {
                    lua_pushvalue(_L, index);
                }
                else {
                    lua_pushnil(_L);
                }
            }

            // Whether the keys of the table at `index` are exactly 1..n, with n at least 1. They are distinct, so
            // `count` positive integer keys of which the largest is `count` are 1..count.
            bool IsSequence(int index) const {
                Reserve(2);
                lua_Integer count   = 0;
                lua_Integer largest = 0;
                lua_pushnil(_L);
                while (lua_next(_L, index) != 0) {
                    lua_pop(_L, 1);
                    const lua_Integer key = lua_isinteger(_L, -1) != 0 ? lua_tointegerx(_L, -1, nullptr) : 0;
                    if (key < 1) {
                        lua_pop(_L, 1);
                        return false;
                    }
                    largest = std::max(largest, key);
                    ++count;
                }
                return count > 0 && largest == count;
            }

            // Whether `slot` is a table the function may write to: one behind a protected metatable is not, which
            // keeps the table of a class or of a namespace as empty as its reads need it.
            bool IsWritable(size_t slot) const {
                if (!Is(slot, LUA_TTABLE)) {
                    return false;
                }
                Reserve(2);
                if (lua_getmetatable(_L, Index(slot)) == 0) {
                    return true;
                }
                lua_pushstring(_L, "__metatable");
                const bool open = lua_rawget(_L, -2) == LUA_TNIL;
                lua_pop(_L, 2);
                return open;
            }

            lua_State *_L;
            // Whether the call is one of a method that was given a receiver: index 1 is then what it was called
            // on. Told as the call begins, since what the function makes later lands on the same stack.
            bool _receiver;
            int _first;    // the index of the first argument
            size_t _count; // the arguments, without the receiver

            size_t _result = 0;
            bool _returned = false;
            std::optional<std::string> _thrown;

            // What Defer made, if the function called it.
            std::shared_ptr<Detail::LuaDeferred> _deferred;
        };

        // __gc of what a function made by PushLuaFunction holds. The userdata loses its metatable with its
        // function, which is how a call that still reaches it tells.
        int CollectFunction(lua_State *L) {
            static_cast<NativeFunction *>(lua_touserdata(L, 1))->~NativeFunction();
            lua_pushnil(L);
            lua_setmetatable(L, 1);
            return 0;
        }

        // What a function made by PushLuaFunction runs. Upvalue 1 holds the NativeFunction.
        int CallFunction(lua_State *L) {
            luaL_checkstack(L, 1, nullptr);
            if (lua_getmetatable(L, lua_upvalueindex(1)) == 0) {
                return luaL_error(L, "function called while its state closes");
            }
            lua_pop(L, 1);
            return Detail::LuaInvokeNative(L, *static_cast<NativeFunction *>(lua_touserdata(L, lua_upvalueindex(1))), false);
        }
    } // namespace

    void PushLuaFunction(lua_State *L, NativeFunction function) {
        luaL_checkstack(L, 4, "cannot push a function");
        void *block = lua_newuserdatauv(L, sizeof(NativeFunction), 0);
        if (lua_rawgetp(L, LUA_REGISTRYINDEX, &holderKey) == LUA_TNIL) {
            lua_pop(L, 1);
            lua_createtable(L, 0, 2);
            lua_pushcfunction(L, CollectFunction);
            lua_setfield(L, -2, "__gc");
            lua_pushboolean(L, 0);
            lua_setfield(L, -2, "__metatable");
            lua_pushvalue(L, -1);
            lua_rawsetp(L, LUA_REGISTRYINDEX, &holderKey);
        }
        // Built once nothing above can raise any more, and finalized from the moment it has its metatable.
        ::new (block) NativeFunction(std::move(function));
        lua_setmetatable(L, -2);
        lua_pushcclosure(L, CallFunction, 1);
    }

    namespace Detail {
        int LuaInvokeNative(lua_State *L, NativeFunction &function, bool method, const char *label) {
            std::optional<std::string> error;
            std::shared_ptr<LuaDeferred> deferred;
            int results = 0;
            {
                LuaCall call(L, method);
                try {
                    function(call);
                }
                catch (const std::exception &exception) {
                    call.ThrowError(exception.what());
                }
                results = call.Finish(error, deferred);
            }
            // Settled by now, the result is given at once. Otherwise the coroutine waits here, or Lua raises
            // that it cannot: neither comes back.
            if (deferred != nullptr) {
                results = deferred->Finish(L, label, error);
            }
            if (!error) {
                return results;
            }

            // Nothing the function left on the stack is of use any more, and dropping it makes room.
            lua_settop(L, 0);
            luaL_where(L, 1);
            lua_pushlstring(L, error->data(), error->size());
            lua_concat(L, 2);
            return lua_error(L);
        }
    } // namespace Detail
} // namespace Framework::Scripting::Bind
