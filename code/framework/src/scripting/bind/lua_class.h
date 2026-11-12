/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "call.h"
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

    // Pushes a Lua function that calls `function`, with a Call of its own. The Lua function keeps `function` until
    // it is collected, or the state closes. Raises a Lua error when the stack cannot grow or Lua is out of memory.
    // In a state no LuaRuntime made, `function` cannot ask its Call for the side, the resource or the manager.
    void PushLuaFunction(lua_State *L, NativeFunction function);

    // What Lua needs of a bound class, without its C++ type: Class<T> fills a LuaClass, and everything here works on
    // one. Nothing in it changes once the definition is built.
    namespace Detail {
        inline constexpr size_t kLuaMetaEventCount = 10;

        struct LuaClass;
        struct LuaFunction;
        struct V8ClassAccess; // v8_backend.h

        // What lua_newuserdatauv aligns a block for.
        union LuaMaxAlign final {
            LUAI_MAXALIGN;
        };

        // Calls the C++ function behind `function` with the arguments on the stack and pushes what it returns.
        // `self` is the receiver, cast to the class that declares the function, or nullptr when it takes none. The
        // receiver and the arguments were checked. Returns the number of values pushed.
        using LuaInvoker = int (*)(lua_State *L, const LuaFunction &function, void *self);

        // How one parameter is checked: the Is and Expected of its LuaConvert.
        struct LuaParam final {
            bool (*is)(lua_State *L, int index) = nullptr;
            const char *(*expected)()           = nullptr;
            bool optional                       = false;
        };

        // The part of a function that depends on its C++ signature.
        struct LuaThunk final {
            std::vector<LuaParam> params; // without the receiver
            LuaInvoker invoke = nullptr;
            std::shared_ptr<const void> target; // the callable `invoke` casts back to its type
            // Set for a context function, and nothing else is: it is called with whatever the script passed, and
            // checks its receiver and its arguments itself.
            std::shared_ptr<NativeFunction> native;
        };

        struct LuaFunction final {
            std::string name;  // the key a script finds it under: "add"
            std::string label; // as an error names it: "Counter:add"
            Side side = Side::Shared;
            LuaThunk thunk;
            int required          = 0;       // the leading parameters a call cannot omit
            const LuaClass *owner = nullptr; // the class that declares it
        };

        struct LuaProperty final {
            LuaFunction getter;
            std::optional<LuaFunction> setter;
        };

        struct LuaBase final {
            const LuaClass *definition    = nullptr;
            void *(*upcast)(void *object) = nullptr; // from an object of the derived class to its base part
        };

        // A script holds pointers into a LuaClass (closure upvalues, the registry), so it lives at one address and
        // outlives every state it was materialised into.
        struct LuaClass final {
            ClassDesc desc;
            std::vector<LuaBase> bases;
            std::deque<LuaFunction> methods;
            std::deque<LuaFunction> statics;
            std::deque<LuaProperty> properties;
            std::optional<LuaFunction> constructor;
            std::array<std::deque<LuaFunction>, kLuaMetaEventCount> metas;
            std::vector<LuaClassHook> hooks;
            // The getters of the properties an object is written to JSON with, in order (Class::JsonProperties).
            std::vector<const LuaFunction *> json;
            // Registers the class as a host type of both transfers (Class::Transfer). Empty when it does not cross.
            fu2::function<void() const> registerTransfer;
            void (*destroy)(void *object) = nullptr; // nullptr for a trivially destructible class
            char classTableKey            = 0;       // its address keys the class table in the registry
            size_t size                   = 0;       // of an object
            const V8ClassAccess *v8       = nullptr; // how JavaScript reaches the objects of the class
        };

        // A member cannot be overloaded: these throw std::logic_error naming <Class>.<member> when the class
        // already declares one a script would find under the same name. Methods and properties share the names
        // of an instance; statics share those of the class table with `new`, which is the constructor. A class
        // may declare again what a base declares.
        void LuaAddMethod(LuaClass &definition, std::string name, Side side, LuaThunk thunk);
        void LuaAddStatic(LuaClass &definition, std::string name, Side side, LuaThunk thunk);
        void LuaAddProperty(LuaClass &definition, std::string name, Side side, LuaThunk getter, std::optional<LuaThunk> setter);
        // Throws std::logic_error when the class already has a constructor.
        void LuaSetConstructor(LuaClass &definition, LuaThunk thunk);
        // Functions added for one event are tried in the order they were added. Throws std::logic_error when the
        // function does not take as many operands as the operator has.
        void LuaAddMeta(LuaClass &definition, LuaMetaEvent event, LuaThunk thunk);

        // Names the properties an object of the class is written to JSON with, in the order they are written. Throws
        // std::logic_error when one is not a property of the class or of one of its bases, when there is none, and
        // when the class has named them already.
        void LuaSetJsonProperties(LuaClass &definition, const std::vector<std::string> &names);

        // The getters of the properties an object of `definition` is written to JSON with: those the class names,
        // or else those of the first of its bases that names some. nullptr when none does.
        const std::vector<const LuaFunction *> *LuaJsonProperties(const LuaClass &definition);

        // The getter of the property `name` of the bound object at `index`, as the state materialised its class: a
        // property of the class or of a base, of the side of the runtime. nullptr for a method, for a name the
        // class does not have and for what is not a bound object.
        const LuaFunction *LuaFindGetter(lua_State *L, int index, std::string_view name);

        // Pushes what the property behind `getter` reads on the object at `index`, as indexing the object does.
        // Raises a Lua error, carrying what() when the getter threw.
        void LuaPushProperty(lua_State *L, int index, const LuaFunction &getter);

        // Creates the metatable and the class table of `definition` in the state of `L`, after those of its bases,
        // with the members that exist in a runtime of side `runtime`. Does nothing in a state that has them.
        void LuaMaterialiseClass(lua_State *L, const LuaClass &definition, Side runtime);

        // target[name] = class table, raw. Throws std::logic_error when the class is not materialised in the state.
        void LuaPublishClass(lua_State *L, const LuaClass &definition, int targetIndex);

        // LuaMaterialiseClass, then the class table as a global of the state under the name of the class, set raw.
        void LuaRegisterClassGlobal(lua_State *L, const LuaClass &definition, Side runtime);

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

        // Raises the error of `function` returning, by reference or pointer, an object of the class `name` that
        // is not its receiver: a script would get a copy, and the class cannot be copied.
        int LuaRaiseNotCopyable(lua_State *L, const LuaFunction &function, const char *name);

        // --- Context functions and namespaces ---

        // The thunk of a context function.
        LuaThunk LuaNativeThunk(std::shared_ptr<NativeFunction> function);

        // Runs `function` on the values a script passed, which are the whole stack of `L`, and returns as a
        // lua_CFunction does. `method` says the first of them is the receiver and not an argument. What the
        // function threw is raised here, once the function has returned and its Call is gone. A function that
        // deferred its result and has not settled it makes the coroutine wait here (Call::Defer); `label` is
        // what the error of a script that cannot wait names it by, "Chat.sendToAll".
        int LuaInvokeNative(lua_State *L, NativeFunction &function, bool method, const char *label = nullptr);

        // A function that is not a member of a class: `owner` is what an error names before it, "Chat" in
        // "Chat.sendToAll".
        LuaFunction LuaMakeFunction(const std::string &owner, std::string name, Side side, LuaThunk thunk);

        // Pushes what a script calls for `function`, which takes no receiver. The script keeps a pointer to
        // `function`, which outlives the state.
        void LuaPushFunction(lua_State *L, const LuaFunction &function);

        // Pushes an empty table that reads from the table at `fieldsIndex` and refuses every write, as the table of
        // a class does. `name` is what the error of a write calls it.
        void LuaPushReadOnlyTable(lua_State *L, int fieldsIndex, const std::string &name);
    } // namespace Detail
} // namespace Framework::Scripting::Bind
