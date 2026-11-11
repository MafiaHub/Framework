/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_class.h"
#include "types.h"

#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Framework::Scripting::Bind {
    // How a C++ type crosses the Lua stack, per the type model (spec 1.2). Specialise for a C++ type to teach Lua
    // thunks about it:
    //
    //   static bool Is(lua_State *L, int index);          // whether the value at `index` is a T
    //   static T Get(lua_State *L, int index);            // precondition: Is()
    //   static void Push(lua_State *L, const T &value);   // pushes exactly one value
    //   static const char *Expected();                    // what an argument error says was expected
    //
    // T is the unqualified type: `const bool` has no converter, `bool` has.
    //
    // `index` is any acceptable index, relative or absent. Is and Get run no script code, convert nothing in place
    // and leave the stack as they found it. The caller keeps room for the one value Push adds; a converter that
    // needs more stack than that reserves it.
    //
    // Is, Get and Push can raise a Lua error, when the stack cannot grow or Lua runs out of memory, and Get can
    // throw std::bad_alloc for a T that allocates.
    template <typename T, typename Enable = void>
    struct LuaConvert;

    // The type of the value at `index`, as an argument error names it: the class of a bound object, the Lua type
    // of anything else.
    inline const char *LuaTypeNameAt(lua_State *L, int index) {
        const Detail::LuaClass *definition = Detail::LuaClassAt(L, index);
        return definition != nullptr ? definition->desc.name.c_str() : luaL_typename(L, index);
    }

    namespace Detail {
        // The converter of a parameter or a return value declared as A. A reference to a bound class keeps its
        // reference, so that it reaches into the userdata; everything else converts as its value type.
        template <typename A>
        using LuaConvertFor = LuaConvert<std::conditional_t<std::is_lvalue_reference_v<A> && HasScriptClassName<std::remove_cvref_t<A>>, A, std::remove_cvref_t<A>>>;

        // The object of class T, or the T part of an object of a class derived from it, in the userdata at
        // `index`. nullptr for anything else.
        template <typename T>
        T *LuaObjectAt(lua_State *L, int index) {
            return static_cast<T *>(LuaToObject(L, index, LuaClassOf<T>::definition));
        }

        // Pushes a userdata holding a T built from `args`. Raises a Lua error when the class of T is not
        // materialised in the state.
        template <typename T, typename... Args>
        void LuaNewObject(lua_State *L, Args &&...args) {
            static_assert(alignof(T) <= alignof(LuaMaxAlign), "Bind: a bound class cannot ask for more alignment than Lua gives a userdata");
            ::new (LuaAllocateObject(L, LuaClassOf<T>::definition, ScriptClassName<T>::value, sizeof(T))) T(std::forward<Args>(args)...);
            lua_setmetatable(L, -2);
        }

        template <typename T>
        inline constexpr bool IsUnqualified = std::is_same_v<T, std::remove_cv_t<T>>;

        // `value` as the floating-point type To. Beyond the range of To it is an infinity of its sign, which the
        // cast alone leaves undefined.
        template <typename To, typename From>
        constexpr To NarrowFloat(From value) {
            if constexpr (std::numeric_limits<To>::max_exponent < std::numeric_limits<From>::max_exponent) {
                if (value > static_cast<From>(std::numeric_limits<To>::max())) {
                    return std::numeric_limits<To>::infinity();
                }
                if (value < static_cast<From>(std::numeric_limits<To>::lowest())) {
                    return -std::numeric_limits<To>::infinity();
                }
            }
            return static_cast<To>(value);
        }

        // Whether `value` is in the range of the integral type T. A type as wide as lua_Integer takes every value:
        // an unsigned one reads a negative integer as its two's complement.
        template <typename T>
        constexpr bool FitsInteger(lua_Integer value) {
            if constexpr (sizeof(T) >= sizeof(lua_Integer)) {
                return true;
            }
            else {
                return value >= static_cast<lua_Integer>(std::numeric_limits<T>::min()) && value <= static_cast<lua_Integer>(std::numeric_limits<T>::max());
            }
        }

        // Whether the value at `index` is an integer, or a float with an integral value, in the range of T. The
        // type is checked first because lua_tointegerx alone would convert a string.
        template <typename T>
        bool IsLuaInteger(lua_State *L, int index) {
            if (lua_type(L, index) != LUA_TNUMBER) {
                return false;
            }
            int exact               = 0;
            const lua_Integer value = lua_tointegerx(L, index, &exact);
            return exact != 0 && FitsInteger<T>(value);
        }
    } // namespace Detail

    // A qualified type would otherwise find the wrong converter (`const bool` is integral and is not `bool`) or
    // none.
    template <typename T>
    struct LuaConvert<T, std::enable_if_t<!Detail::IsUnqualified<T>>> final {
        static_assert(Detail::IsUnqualified<T>, "Bind::LuaConvert: ask for the converter of the unqualified type (std::remove_cv_t<T>), not of a const or volatile one");
    };

    template <>
    struct LuaConvert<bool> final {
        static bool Is(lua_State *L, int index) {
            return lua_type(L, index) == LUA_TBOOLEAN;
        }

        static bool Get(lua_State *L, int index) {
            return lua_toboolean(L, index) != 0;
        }

        static void Push(lua_State *L, const bool &value) {
            lua_pushboolean(L, value ? 1 : 0);
        }

        static const char *Expected() {
            return "boolean";
        }
    };

    template <typename T>
    struct LuaConvert<T, std::enable_if_t<Detail::IsUnqualified<T> && std::is_integral_v<T> && !std::is_same_v<T, bool>>> final {
        static bool Is(lua_State *L, int index) {
            return Detail::IsLuaInteger<T>(L, index);
        }

        static T Get(lua_State *L, int index) {
            return static_cast<T>(lua_tointegerx(L, index, nullptr));
        }

        static void Push(lua_State *L, const T &value) {
            lua_pushinteger(L, static_cast<lua_Integer>(value));
        }

        static const char *Expected() {
            return "integer";
        }
    };

    // An enum crosses as its underlying integer. Whether the integer names an enumerator is not checked.
    template <typename T>
    struct LuaConvert<T, std::enable_if_t<Detail::IsUnqualified<T> && std::is_enum_v<T>>> final {
        using Underlying = std::underlying_type_t<T>;

        static bool Is(lua_State *L, int index) {
            return Detail::IsLuaInteger<Underlying>(L, index);
        }

        static T Get(lua_State *L, int index) {
            return static_cast<T>(static_cast<Underlying>(lua_tointegerx(L, index, nullptr)));
        }

        static void Push(lua_State *L, const T &value) {
            lua_pushinteger(L, static_cast<lua_Integer>(static_cast<Underlying>(value)));
        }

        static const char *Expected() {
            return "integer";
        }
    };

    // An integer or a float is accepted, a float is pushed. A number beyond the range of `float` is read as an
    // infinity.
    template <typename T>
    struct LuaConvert<T, std::enable_if_t<Detail::IsUnqualified<T> && std::is_floating_point_v<T>>> final {
        static bool Is(lua_State *L, int index) {
            return lua_type(L, index) == LUA_TNUMBER;
        }

        static T Get(lua_State *L, int index) {
            return Detail::NarrowFloat<T>(lua_tonumberx(L, index, nullptr));
        }

        static void Push(lua_State *L, const T &value) {
            lua_pushnumber(L, Detail::NarrowFloat<lua_Number>(value));
        }

        static const char *Expected() {
            return "number";
        }
    };

    // A string only. Lua would convert a number, in place, so the type is checked rather than lua_isstring.
    template <>
    struct LuaConvert<std::string> final {
        static bool Is(lua_State *L, int index) {
            return lua_type(L, index) == LUA_TSTRING;
        }

        static std::string Get(lua_State *L, int index) {
            size_t length    = 0;
            const char *text = lua_tolstring(L, index, &length);
            return std::string(text, length);
        }

        static void Push(lua_State *L, const std::string &value) {
            lua_pushlstring(L, value.data(), value.size());
        }

        static const char *Expected() {
            return "string";
        }
    };

    // Get returns a view into the Lua string, valid while the value it was read from stays on the stack.
    template <>
    struct LuaConvert<std::string_view> final {
        static bool Is(lua_State *L, int index) {
            return lua_type(L, index) == LUA_TSTRING;
        }

        static std::string_view Get(lua_State *L, int index) {
            size_t length    = 0;
            const char *text = lua_tolstring(L, index, &length);
            return std::string_view(text, length);
        }

        static void Push(lua_State *L, const std::string_view &value) {
            lua_pushlstring(L, value.data(), value.size());
        }

        static const char *Expected() {
            return "string";
        }
    };

    // Get returns the Lua string itself, valid while the value it was read from stays on the stack. It ends at the
    // first zero the script put in it. A null pointer pushes nil.
    template <>
    struct LuaConvert<const char *> final {
        static bool Is(lua_State *L, int index) {
            return lua_type(L, index) == LUA_TSTRING;
        }

        static const char *Get(lua_State *L, int index) {
            return lua_tolstring(L, index, nullptr);
        }

        static void Push(lua_State *L, const char *const &value) {
            lua_pushstring(L, value);
        }

        static const char *Expected() {
            return "string";
        }
    };

    // nil or an absent argument is an empty optional; anything else is what T makes of it.
    template <typename T>
    struct LuaConvert<std::optional<T>> final {
        static bool Is(lua_State *L, int index) {
            return lua_isnoneornil(L, index) || LuaConvert<T>::Is(L, index);
        }

        static std::optional<T> Get(lua_State *L, int index) {
            if (lua_isnoneornil(L, index)) {
                return std::nullopt;
            }
            return LuaConvert<T>::Get(L, index);
        }

        static void Push(lua_State *L, const std::optional<T> &value) {
            if (value) {
                LuaConvert<T>::Push(L, *value);
            }
            else {
                lua_pushnil(L);
            }
        }

        static const char *Expected() {
            return LuaConvert<T>::Expected();
        }
    };

    // A vector is a sequence: a table whose keys are exactly 1..n, read raw. A table with a hole or any other key
    // is not one.
    template <typename T, typename Allocator>
    struct LuaConvert<std::vector<T, Allocator>> final {
        static_assert(!Detail::IsOptional<T>::value, "Bind::LuaConvert: std::vector<std::optional<T>> has no Lua form, a sequence cannot hold nil");

        static bool Is(lua_State *L, int index) {
            if (lua_type(L, index) != LUA_TTABLE) {
                return false;
            }
            index = lua_absindex(L, index);
            luaL_checkstack(L, 2, nullptr);

            // Keys are distinct, so `count` positive integer keys of which the largest is `count` are 1..count.
            lua_Integer count   = 0;
            lua_Integer largest = 0;
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                const lua_Integer key = lua_isinteger(L, -2) ? lua_tointegerx(L, -2, nullptr) : 0;
                if (key < 1 || !LuaConvert<T>::Is(L, -1)) {
                    lua_pop(L, 2);
                    return false;
                }
                lua_pop(L, 1);
                largest = std::max(largest, key);
                ++count;
            }
            return largest == count;
        }

        static std::vector<T, Allocator> Get(lua_State *L, int index) {
            index = lua_absindex(L, index);
            luaL_checkstack(L, 1, nullptr);

            // A sequence has one border, so its raw length is its element count.
            const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, index));
            std::vector<T, Allocator> values;
            values.reserve(static_cast<size_t>(count));
            for (lua_Integer position = 1; position <= count; ++position) {
                lua_rawgeti(L, index, position);
                values.push_back(LuaConvert<T>::Get(L, -1));
                lua_pop(L, 1);
            }
            return values;
        }

        static void Push(lua_State *L, const std::vector<T, Allocator> &value) {
            luaL_checkstack(L, 2, nullptr);
            lua_createtable(L, static_cast<int>(std::min<size_t>(value.size(), INT_MAX)), 0);
            lua_Integer position = 0;
            for (const auto &element : value) {
                LuaConvert<T>::Push(L, element);
                lua_rawseti(L, -2, ++position);
            }
        }

        static const char *Expected() {
            return "table";
        }
    };

    // A bound class, one named with FW_SCRIPT_CLASS and defined with Bind::Class, crosses as the userdata that
    // holds the object. An object of a derived class is accepted as its base. Get copies the object out and Push
    // copies or moves it into a new userdata; a derived object pushed as its base is sliced.
    template <typename T>
    struct LuaConvert<T, std::enable_if_t<Detail::HasScriptClassName<T>>> final {
        static bool Is(lua_State *L, int index) {
            return Detail::LuaObjectAt<T>(L, index) != nullptr;
        }

        static T Get(lua_State *L, int index) {
            return *Detail::LuaObjectAt<T>(L, index);
        }

        static void Push(lua_State *L, const T &value) {
            Detail::LuaNewObject<T>(L, value);
        }

        static void Push(lua_State *L, T &&value) {
            Detail::LuaNewObject<T>(L, std::move(value));
        }

        static const char *Expected() {
            return ScriptClassName<T>::value;
        }
    };

    // T & and const T & of a bound class: Get returns the object in the userdata, valid while the userdata is.
    // Push copies, as for T.
    template <typename T>
    struct LuaConvert<T &, std::enable_if_t<Detail::HasScriptClassName<std::remove_const_t<T>>>> final {
        using Class = std::remove_const_t<T>;

        static bool Is(lua_State *L, int index) {
            return Detail::LuaObjectAt<Class>(L, index) != nullptr;
        }

        static T &Get(lua_State *L, int index) {
            return *Detail::LuaObjectAt<Class>(L, index);
        }

        static void Push(lua_State *L, const Class &value) {
            Detail::LuaNewObject<Class>(L, value);
        }

        static const char *Expected() {
            return ScriptClassName<Class>::value;
        }
    };

    // T * and const T * of a bound class: Get returns the object in the userdata, valid while the userdata is, and
    // never nullptr, since nil is not an object. Push copies what the pointer designates; nullptr pushes nil.
    template <typename T>
    struct LuaConvert<T *, std::enable_if_t<Detail::HasScriptClassName<std::remove_const_t<T>>>> final {
        using Class = std::remove_const_t<T>;

        static bool Is(lua_State *L, int index) {
            return Detail::LuaObjectAt<Class>(L, index) != nullptr;
        }

        static T *Get(lua_State *L, int index) {
            return Detail::LuaObjectAt<Class>(L, index);
        }

        static void Push(lua_State *L, const Class *value) {
            if (value != nullptr) {
                Detail::LuaNewObject<Class>(L, *value);
            }
            else {
                lua_pushnil(L);
            }
        }

        static const char *Expected() {
            return ScriptClassName<Class>::value;
        }
    };
} // namespace Framework::Scripting::Bind
