/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "descriptors.h"
#include "doc.h"

#include <function2/function2.hpp>
#include <lua.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Framework::Scripting::Bind {
    // The Lua operators a class can declare with Class::LuaMeta.
    enum class LuaMetaEvent : uint8_t {
        Add,
        Sub,
        Mul,
        Div,
        Unm,
        Eq,
        Lt,
        Le,
        Len,
        Concat
    };

    // Adds to a class what exists in Lua only. `metatableIndex` is the metatable of the instances, `classTableIndex`
    // the table the fields of the class are read from. What the hook leaves on the stack is dropped.
    using LuaClassHook = fu2::function<void(lua_State *L, int metatableIndex, int classTableIndex) const>;

    // What Lua needs of a bound class, without its C++ type: Class<T> fills a LuaClass, and everything here works on
    // one. Nothing in it changes once the definition is built.
    namespace Detail {
        inline constexpr size_t kLuaMetaEventCount = 10;

        struct LuaClass;
        struct LuaFunction;

        // What lua_newuserdatauv aligns a block for.
        union LuaMaxAlign {
            LUAI_MAXALIGN;
        };

        // Calls the C++ function behind `function` with the arguments on the stack and pushes what it returns.
        // `self` is the receiver, cast to the class that declares the function, or nullptr when it takes none. The
        // receiver and the arguments were checked. Returns the number of values pushed.
        using LuaInvoker = int (*)(lua_State *L, const LuaFunction &function, void *self);

        // How one parameter is checked: the Is and Expected of its LuaConvert.
        struct LuaParam {
            bool (*is)(lua_State *L, int index) = nullptr;
            const char *(*expected)()           = nullptr;
            bool optional                       = false;
        };

        // The part of a function that depends on its C++ signature.
        struct LuaThunk {
            std::vector<LuaParam> params; // without the receiver
            LuaInvoker invoke = nullptr;
            std::shared_ptr<const void> target; // the callable `invoke` casts back to its type
        };

        struct LuaFunction {
            std::string name;  // the key a script finds it under: "add"
            std::string label; // as an error names it: "Counter:add"
            Side side = Side::Shared;
            LuaThunk thunk;
            int required          = 0;       // the leading parameters a call cannot omit
            const LuaClass *owner = nullptr; // the class that declares it
        };

        struct LuaProperty {
            LuaFunction getter;
            std::optional<LuaFunction> setter;
        };

        struct LuaBase {
            const LuaClass *definition    = nullptr;
            void *(*upcast)(void *object) = nullptr; // from an object of the derived class to its base part
        };

        // A script holds pointers into a LuaClass (closure upvalues, the registry), so it lives at one address and
        // outlives every state it was materialised into.
        struct LuaClass {
            ClassDesc desc;
            std::vector<LuaBase> bases;
            std::deque<LuaFunction> methods;
            std::deque<LuaFunction> statics;
            std::deque<LuaProperty> properties;
            std::optional<LuaFunction> constructor;
            std::array<std::deque<LuaFunction>, kLuaMetaEventCount> metas;
            std::vector<LuaClassHook> hooks;
            void (*destroy)(void *object) = nullptr; // nullptr for a trivially destructible class
            char classTableKey            = 0;       // its address keys the class table in the registry
        };

        // The definition of the C++ type T, or nullptr until Class<T> is constructed.
        template <typename T>
        struct LuaClassOf final {
            static inline const LuaClass *definition = nullptr;
        };

        void LuaAddMethod(LuaClass &definition, std::string name, Side side, LuaThunk thunk);
        void LuaAddStatic(LuaClass &definition, std::string name, Side side, LuaThunk thunk);
        void LuaAddProperty(LuaClass &definition, std::string name, Side side, LuaThunk getter, std::optional<LuaThunk> setter);
        // Throws std::logic_error when the class already has a constructor.
        void LuaSetConstructor(LuaClass &definition, LuaThunk thunk);
        // Functions added for one event are tried in the order they were added. Throws std::logic_error when the
        // function does not take as many operands as the operator has.
        void LuaAddMeta(LuaClass &definition, LuaMetaEvent event, LuaThunk thunk);

        // Creates the metatable and the class table of `definition` in the state of `L`, after those of its bases,
        // with the members that exist in a runtime of side `runtime`. Does nothing in a state that has them.
        void LuaMaterialiseClass(lua_State *L, const LuaClass &definition, Side runtime);

        // target[name] = class table, raw. Throws std::logic_error when the class is not materialised in the state.
        void LuaPublishClass(lua_State *L, const LuaClass &definition, int targetIndex);

        // The class of the bound object at `index`, or nullptr when the value is not one.
        const LuaClass *LuaClassAt(lua_State *L, int index);

        // The object at `index` seen as `wanted`: the object itself, or its base part when its class derives from
        // `wanted`. nullptr when the value is not such an object or `wanted` is nullptr.
        void *LuaToObject(lua_State *L, int index, const LuaClass *wanted);

        // Pushes a userdata of `size` bytes, then the metatable of `definition`, and returns the block. The caller
        // builds the object in it and calls lua_setmetatable(L, -2), in that order: a block without its metatable
        // is never finalized. Raises a Lua error when `definition` is nullptr or not materialised in the state;
        // `name` is what the error calls the class.
        void *LuaAllocateObject(lua_State *L, const LuaClass *definition, const char *name, size_t size);
    } // namespace Detail
} // namespace Framework::Scripting::Bind
