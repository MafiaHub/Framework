/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_class.h"

#include "lua_convert.h"

#include <lauxlib.h>

#include <algorithm>
#include <exception>
#include <stdexcept>

// Where a class lives in a state:
//
//   registry[&definition]               the metatable of its instances
//   registry[&definition.classTableKey] its class table, the one PublishLua hands out
//   metatable[&classKey]                &definition, which is how a userdata is recognised as a bound object
//
// An instance is a userdata holding the object, with no user value. Its metatable keeps methods and getters in one
// table and setters in another, both flattened over the bases, and reaches them through upvalues, so a script
// that indexes an object never sees a metamethod. The class table is an empty proxy whose metatable reads from the
// table that holds the statics and refuses every write.
//
// A function reaches its LuaFunction through a light userdata, as an upvalue or as a value of the member tables,
// and every call goes through Invoke. The dispatchers check the receiver and the arguments first and raise with
// luaL_error, which names the position of the calling script.

namespace Framework::Scripting::Bind::Detail {
    namespace {
        // Not const, so that no other object can come to share its address.
        char classKey = 0;

        // The field of a class table that constructs an instance.
        constexpr const char *kConstructorName = "new";

        // In the order of LuaMetaEvent.
        constexpr std::array<const char *, kLuaMetaEventCount> kMetaEventNames = {"__add", "__sub", "__mul", "__div", "__unm", "__eq", "__lt", "__le", "__len", "__concat"};
        static_assert(static_cast<size_t>(LuaMetaEvent::Concat) + 1 == kLuaMetaEventCount, "kLuaMetaEventCount is the number of LuaMetaEvent values");

        bool IsUnary(LuaMetaEvent event) {
            return event == LuaMetaEvent::Unm || event == LuaMetaEvent::Len;
        }

        void PushPointer(lua_State *L, const void *pointer) {
            lua_pushlightuserdata(L, const_cast<void *>(pointer));
        }

        const LuaFunction &FunctionUpvalue(lua_State *L) {
            return *static_cast<const LuaFunction *>(lua_touserdata(L, lua_upvalueindex(1)));
        }

        const LuaClass &ClassUpvalue(lua_State *L, int upvalue) {
            return *static_cast<const LuaClass *>(lua_touserdata(L, lua_upvalueindex(upvalue)));
        }

        LuaFunction MakeFunction(const std::string &owner, std::string name, char separator, Side side, LuaThunk thunk) {
            LuaFunction function;
            function.label = owner + separator + name;
            function.name  = std::move(name);
            function.side  = side;
            for (size_t i = 0; i < thunk.params.size(); ++i) {
                if (!thunk.params[i].optional) {
                    function.required = static_cast<int>(i) + 1;
                }
            }
            function.thunk = std::move(thunk);
            return function;
        }

        LuaFunction MakeFunction(const LuaClass &owner, std::string name, char separator, Side side, LuaThunk thunk) {
            LuaFunction function = MakeFunction(owner.desc.name, std::move(name), separator, side, std::move(thunk));
            function.owner       = &owner;
            return function;
        }

        bool Declares(const std::deque<LuaFunction> &functions, const std::string &name) {
            return std::any_of(functions.begin(), functions.end(), [&name](const LuaFunction &function) {
                return function.name == name;
            });
        }

        // Throws when `definition` itself declares a method or a property named `name`: a script finds both under
        // the one key of an instance, so a second would hide the first.
        void RefuseSecondMember(const LuaClass &definition, const std::string &name) {
            const bool property = std::any_of(definition.properties.begin(), definition.properties.end(), [&name](const LuaProperty &declared) {
                return declared.getter.name == name;
            });
            if (property || Declares(definition.methods, name)) {
                throw std::logic_error("Bind: '" + definition.desc.name + "." + name + "' is declared twice, and a member cannot be overloaded");
            }
        }

        // The getter of the property `name` of `definition`, its own before those of its bases, or nullptr.
        const LuaFunction *FindGetter(const LuaClass &definition, const std::string &name) {
            for (const LuaProperty &property : definition.properties) {
                if (property.getter.name == name) {
                    return &property.getter;
                }
            }
            for (const LuaBase &base : definition.bases) {
                if (const LuaFunction *found = FindGetter(*base.definition, name)) {
                    return found;
                }
            }
            return nullptr;
        }

        // --- Calling a C++ function ---

        int Raise(lua_State *L, const std::string &message) {
            luaL_where(L, 1);
            lua_pushlstring(L, message.data(), message.size());
            lua_concat(L, 2);
            return lua_error(L);
        }

        // A std::exception from the C++ function becomes a Lua error carrying what(), raised once the catch block
        // is left. A Lua error passing through is not a std::exception and is left alone.
        int Invoke(lua_State *L, const LuaFunction &function, void *self) {
            std::string error;
            try {
                return function.thunk.invoke(L, function, self);
            }
            catch (const std::exception &exception) {
                error = exception.what();
            }
            return Raise(L, error);
        }

        // --- Checking a call ---

        int ArgumentCount(lua_State *L, int first) {
            return std::max(lua_gettop(L) - first + 1, 0);
        }

        bool TakesCount(const LuaFunction &function, int given) {
            return given >= function.required && given <= static_cast<int>(function.thunk.params.size());
        }

        // The position, from 1, of the first argument that is not what `function` takes there; 0 when all are.
        int FirstBadArgument(lua_State *L, const LuaFunction &function, int first) {
            const std::vector<LuaParam> &params = function.thunk.params;
            for (size_t i = 0; i < params.size(); ++i) {
                if (!params[i].is(L, first + static_cast<int>(i))) {
                    return static_cast<int>(i) + 1;
                }
            }
            return 0;
        }

        // Whether `function` takes the values from index `first` on as its arguments.
        bool TakesArguments(lua_State *L, const LuaFunction &function, int first) {
            return TakesCount(function, ArgumentCount(L, first)) && FirstBadArgument(L, function, first) == 0;
        }

        // Raises unless `function` takes the values from index `first` on as its arguments.
        void CheckArguments(lua_State *L, const LuaFunction &function, int first) {
            const int given = ArgumentCount(L, first);
            if (!TakesCount(function, given)) {
                luaL_error(L, "wrong number of arguments to '%s' (%d..%d expected, got %d)", function.label.c_str(), function.required, static_cast<int>(function.thunk.params.size()), given);
            }
            if (const int bad = FirstBadArgument(L, function, first)) {
                luaL_error(L, "bad argument #%d to '%s' (%s expected, got %s)", bad, function.label.c_str(), function.thunk.params[static_cast<size_t>(bad) - 1].expected(), LuaTypeNameAt(L, first + bad - 1));
            }
        }

        // The object at index 1 as the class that declares `function`. Raises when there is no such object, which
        // is what a method called with `.` runs into.
        void *CheckReceiver(lua_State *L, const LuaFunction &function) {
            void *object = LuaToObject(L, 1, function.owner);
            if (object == nullptr) {
                luaL_error(L, "bad receiver for '%s' (%s expected, got %s)", function.label.c_str(), function.owner->desc.name.c_str(), LuaTypeNameAt(L, 1));
            }
            return object;
        }

        void *Upcast(const LuaClass &actual, void *object, const LuaClass &wanted) {
            if (&actual == &wanted) {
                return object;
            }
            for (const LuaBase &base : actual.bases) {
                if (void *found = Upcast(*base.definition, base.upcast(object), wanted)) {
                    return found;
                }
            }
            return nullptr;
        }

        // --- What Lua calls ---

        // A context function is handed the call as it is: it looks for its receiver and reads its arguments itself.
        int CallMethod(lua_State *L) {
            const LuaFunction &function = FunctionUpvalue(L);
            if (function.thunk.native != nullptr) {
                return LuaInvokeNative(L, *function.thunk.native, true, function.label.c_str());
            }
            void *self = CheckReceiver(L, function);
            CheckArguments(L, function, 2);
            return Invoke(L, function, self);
        }

        int CallStatic(lua_State *L) {
            const LuaFunction &function = FunctionUpvalue(L);
            if (function.thunk.native != nullptr) {
                return LuaInvokeNative(L, *function.thunk.native, false, function.label.c_str());
            }
            CheckArguments(L, function, 1);
            return Invoke(L, function, nullptr);
        }

        // __call of a class table, which Lua passes first. The upvalue is the constructor, as for `new`.
        int CallClass(lua_State *L) {
            lua_remove(L, 1);
            return CallStatic(L);
        }

        int RefuseConstruction(lua_State *L) {
            return luaL_error(L, "%s cannot be constructed from script", ClassUpvalue(L, 1).desc.name.c_str());
        }

        int RefuseClassWrite(lua_State *L) {
            return luaL_error(L, "attempt to set '%s' on class %s", luaL_tolstring(L, 2, nullptr), ClassUpvalue(L, 1).desc.name.c_str());
        }

        // __newindex of a read-only table that is not a class. Upvalue 1 is its name.
        int RefuseWrite(lua_State *L) {
            return luaL_error(L, "attempt to set '%s' on %s", luaL_tolstring(L, 2, nullptr), lua_tostring(L, lua_upvalueindex(1)));
        }

        // __index. Upvalue 1 maps a name to a method, which is returned, or to the getter of a property, a light
        // userdata, which is called.
        int Index(lua_State *L) {
            lua_settop(L, 2);
            lua_rawget(L, lua_upvalueindex(1));
            if (lua_type(L, 2) != LUA_TLIGHTUSERDATA) {
                return 1;
            }
            const LuaFunction &getter = *static_cast<const LuaFunction *>(lua_touserdata(L, 2));
            lua_settop(L, 1);
            return Invoke(L, getter, CheckReceiver(L, getter));
        }

        // __newindex. Upvalue 1 maps a name to the setter of a property, upvalue 2 is the class.
        int NewIndex(lua_State *L) {
            lua_settop(L, 3);
            lua_pushvalue(L, 2);
            lua_rawget(L, lua_upvalueindex(1));
            const LuaFunction *setter = static_cast<const LuaFunction *>(lua_touserdata(L, 4));
            if (setter == nullptr) {
                return luaL_error(L, "attempt to set '%s' on %s", luaL_tolstring(L, 2, nullptr), ClassUpvalue(L, 2).desc.name.c_str());
            }
            lua_settop(L, 3);
            lua_remove(L, 2);
            void *self = CheckReceiver(L, *setter);
            CheckArguments(L, *setter, 2);
            Invoke(L, *setter, self);
            return 0;
        }

        // What LuaPushProperty calls: the object is at index 1 and the getter, a light userdata, at index 2.
        int ReadProperty(lua_State *L) {
            const LuaFunction &getter = *static_cast<const LuaFunction *>(lua_touserdata(L, 2));
            lua_settop(L, 1);
            return Invoke(L, getter, CheckReceiver(L, getter));
        }

        // __gc. A finalizer that ran earlier in the same collection may have kept the userdata, so it loses its
        // metatable with its object and stops passing for one.
        int Collect(lua_State *L) {
            ClassUpvalue(L, 1).destroy(lua_touserdata(L, 1));
            lua_pushnil(L);
            lua_setmetatable(L, 1);
            return 0;
        }

        bool DeclaresMeta(const LuaClass &definition, size_t event) {
            if (!definition.metas[event].empty()) {
                return true;
            }
            return std::any_of(definition.bases.begin(), definition.bases.end(), [event](const LuaBase &base) {
                return DeclaresMeta(*base.definition, event);
            });
        }

        // The first function declared for `event`, by the class before its bases, that takes the operands on the
        // stack. `first` receives the first one declared at all.
        const LuaFunction *FindMeta(lua_State *L, const LuaClass &definition, size_t event, const LuaFunction *&first) {
            for (const LuaFunction &function : definition.metas[event]) {
                if (first == nullptr) {
                    first = &function;
                }
                if (TakesArguments(L, function, 1)) {
                    return &function;
                }
            }
            for (const LuaBase &base : definition.bases) {
                if (const LuaFunction *found = FindMeta(L, *base.definition, event, first)) {
                    return found;
                }
            }
            return nullptr;
        }

        // A metamethod of an operator. Upvalue 1 is the class, upvalue 2 the event.
        int CallMeta(lua_State *L) {
            const LuaMetaEvent event = static_cast<LuaMetaEvent>(lua_tointeger(L, lua_upvalueindex(2)));
            if (IsUnary(event)) {
                // Lua passes the operand of a unary operator twice.
                lua_settop(L, 1);
            }

            const LuaFunction *first = nullptr;
            if (const LuaFunction *function = FindMeta(L, ClassUpvalue(L, 1), static_cast<size_t>(event), first)) {
                return Invoke(L, *function, nullptr);
            }
            if (event == LuaMetaEvent::Eq) {
                // Values no function compares are different, not an error.
                lua_pushboolean(L, 0);
                return 1;
            }
            CheckArguments(L, *first, 1);
            return 0;
        }

        // --- Materialisation ---

        void PushClosure(lua_State *L, lua_CFunction dispatcher, const LuaFunction &function) {
            PushPointer(L, &function);
            lua_pushcclosure(L, dispatcher, 1);
        }

        // Fills the member tables with what `definition` declares for `runtime`, after its bases, so that a class
        // replaces what it declares again. A method that replaces a property of a base drops its setter too.
        void AddMembers(lua_State *L, const LuaClass &definition, Side runtime, int members, int setters) {
            for (const LuaBase &base : definition.bases) {
                AddMembers(L, *base.definition, runtime, members, setters);
            }

            for (const LuaProperty &property : definition.properties) {
                if (!Materialises(property.getter.side, runtime)) {
                    continue;
                }
                PushPointer(L, &property.getter);
                lua_setfield(L, members, property.getter.name.c_str());
                if (property.setter) {
                    PushPointer(L, &*property.setter);
                }
                else {
                    lua_pushnil(L);
                }
                lua_setfield(L, setters, property.getter.name.c_str());
            }

            for (const LuaFunction &method : definition.methods) {
                if (!Materialises(method.side, runtime)) {
                    continue;
                }
                PushClosure(L, CallMethod, method);
                lua_setfield(L, members, method.name.c_str());
                lua_pushnil(L);
                lua_setfield(L, setters, method.name.c_str());
            }
        }

        // Pushes the metatable of the instances of `definition`.
        void PushInstanceMetatable(lua_State *L, const LuaClass &definition, Side runtime) {
            lua_newtable(L);
            const int metatable = lua_gettop(L);
            lua_newtable(L);
            const int members = lua_gettop(L);
            lua_newtable(L);
            const int setters = lua_gettop(L);
            AddMembers(L, definition, runtime, members, setters);

            if (lua_getfield(L, members, "toString") == LUA_TFUNCTION) {
                lua_setfield(L, metatable, "__tostring");
            }
            else {
                lua_pop(L, 1);
            }

            lua_pushvalue(L, members);
            lua_pushcclosure(L, Index, 1);
            lua_setfield(L, metatable, "__index");

            lua_pushvalue(L, setters);
            PushPointer(L, &definition);
            lua_pushcclosure(L, NewIndex, 2);
            lua_setfield(L, metatable, "__newindex");

            if (definition.destroy != nullptr) {
                PushPointer(L, &definition);
                lua_pushcclosure(L, Collect, 1);
                lua_setfield(L, metatable, "__gc");
            }

            for (size_t event = 0; event < kLuaMetaEventCount; ++event) {
                if (!DeclaresMeta(definition, event)) {
                    continue;
                }
                PushPointer(L, &definition);
                lua_pushinteger(L, static_cast<lua_Integer>(event));
                lua_pushcclosure(L, CallMeta, 2);
                lua_setfield(L, metatable, kMetaEventNames[event]);
            }

            lua_pushboolean(L, 0);
            lua_setfield(L, metatable, "__metatable");
            lua_pushstring(L, definition.desc.name.c_str());
            lua_setfield(L, metatable, "__name");
            PushPointer(L, &definition);
            lua_rawsetp(L, metatable, &classKey);

            lua_settop(L, metatable);
        }

        // Pushes the table that holds the fields of the class, then the class table that reads from it.
        void PushClassTables(lua_State *L, const LuaClass &definition, Side runtime) {
            lua_newtable(L);
            const int fields = lua_gettop(L);
            for (const LuaFunction &function : definition.statics) {
                if (Materialises(function.side, runtime)) {
                    PushClosure(L, CallStatic, function);
                    lua_setfield(L, fields, function.name.c_str());
                }
            }
            // A class without a constructor still has `new` and can be called: both say why they fail.
            if (definition.constructor) {
                PushClosure(L, CallStatic, *definition.constructor);
            }
            else {
                PushPointer(L, &definition);
                lua_pushcclosure(L, RefuseConstruction, 1);
            }
            lua_setfield(L, fields, kConstructorName);

            lua_newtable(L);
            lua_newtable(L);
            lua_pushvalue(L, fields);
            lua_setfield(L, -2, "__index");
            PushPointer(L, &definition);
            lua_pushcclosure(L, RefuseClassWrite, 1);
            lua_setfield(L, -2, "__newindex");
            if (definition.constructor) {
                PushClosure(L, CallClass, *definition.constructor);
            }
            else {
                PushPointer(L, &definition);
                lua_pushcclosure(L, RefuseConstruction, 1);
            }
            lua_setfield(L, -2, "__call");
            lua_pushboolean(L, 0);
            lua_setfield(L, -2, "__metatable");
            lua_setmetatable(L, -2);
        }
    } // namespace

    void LuaAddMethod(LuaClass &definition, std::string name, Side side, LuaThunk thunk) {
        RefuseSecondMember(definition, name);
        definition.methods.push_back(MakeFunction(definition, std::move(name), ':', side, std::move(thunk)));
    }

    void LuaAddStatic(LuaClass &definition, std::string name, Side side, LuaThunk thunk) {
        if (name == kConstructorName) {
            throw std::logic_error("Bind: '" + definition.desc.name + "." + name + "' is the constructor and cannot be declared as a static");
        }
        if (Declares(definition.statics, name)) {
            throw std::logic_error("Bind: '" + definition.desc.name + "." + name + "' is declared twice, and a static cannot be overloaded");
        }
        definition.statics.push_back(MakeFunction(definition, std::move(name), '.', side, std::move(thunk)));
    }

    void LuaAddProperty(LuaClass &definition, std::string name, Side side, LuaThunk getter, std::optional<LuaThunk> setter) {
        RefuseSecondMember(definition, name);
        LuaProperty property;
        if (setter) {
            property.setter = MakeFunction(definition, name, '.', side, std::move(*setter));
        }
        property.getter = MakeFunction(definition, std::move(name), '.', side, std::move(getter));
        definition.properties.push_back(std::move(property));
    }

    void LuaSetConstructor(LuaClass &definition, LuaThunk thunk) {
        if (definition.constructor) {
            throw std::logic_error("Bind: '" + definition.desc.name + "' declares a second constructor");
        }
        definition.constructor = MakeFunction(definition, kConstructorName, '.', Side::Shared, std::move(thunk));
    }

    void LuaAddMeta(LuaClass &definition, LuaMetaEvent event, LuaThunk thunk) {
        const size_t index    = static_cast<size_t>(event);
        const size_t operands = IsUnary(event) ? 1 : 2;
        LuaFunction function  = MakeFunction(definition, kMetaEventNames[index], '.', Side::Shared, std::move(thunk));
        if (function.thunk.params.size() != operands) {
            throw std::logic_error("Bind: '" + function.label + "' has " + std::to_string(operands) + " operand(s) but its function takes " + std::to_string(function.thunk.params.size()) + " parameter(s)");
        }
        definition.metas[index].push_back(std::move(function));
    }

    void LuaSetJsonProperties(LuaClass &definition, const std::vector<std::string> &names) {
        if (!definition.json.empty()) {
            throw std::logic_error("Bind: '" + definition.desc.name + "' names its JSON properties twice");
        }
        if (names.empty()) {
            throw std::logic_error("Bind: '" + definition.desc.name + "' names no JSON property");
        }
        std::vector<const LuaFunction *> getters;
        for (const std::string &name : names) {
            const LuaFunction *getter = FindGetter(definition, name);
            if (getter == nullptr) {
                throw std::logic_error("Bind: '" + definition.desc.name + "." + name + "' is named for JSON and is not a property of the class");
            }
            getters.push_back(getter);
        }
        definition.json = std::move(getters);
    }

    const std::vector<const LuaFunction *> *LuaJsonProperties(const LuaClass &definition) {
        if (!definition.json.empty()) {
            return &definition.json;
        }
        for (const LuaBase &base : definition.bases) {
            if (const auto *found = LuaJsonProperties(*base.definition)) {
                return found;
            }
        }
        return nullptr;
    }

    // Called as a function of its own, so that the getter finds its receiver where every thunk expects it.
    void LuaPushProperty(lua_State *L, int index, const LuaFunction &getter) {
        index = lua_absindex(L, index);
        luaL_checkstack(L, 3, "cannot read a property");
        lua_pushcfunction(L, ReadProperty);
        lua_pushvalue(L, index);
        PushPointer(L, &getter);
        lua_call(L, 2, 1);
    }

    void LuaMaterialiseClass(lua_State *L, const LuaClass &definition, Side runtime) {
        luaL_checkstack(L, 10, "cannot materialise a class");
        const bool materialised = lua_rawgetp(L, LUA_REGISTRYINDEX, &definition) != LUA_TNIL;
        lua_pop(L, 1);
        if (materialised) {
            return;
        }

        for (const LuaBase &base : definition.bases) {
            LuaMaterialiseClass(L, *base.definition, runtime);
        }

        const int top = lua_gettop(L);
        PushInstanceMetatable(L, definition, runtime);
        const int metatable = lua_gettop(L);
        PushClassTables(L, definition, runtime);
        const int classTable = lua_gettop(L);
        const int fields     = classTable - 1;

        // Registered before the hooks run, so that a hook can already push an instance.
        lua_pushvalue(L, metatable);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &definition);
        lua_pushvalue(L, classTable);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &definition.classTableKey);

        for (const LuaClassHook &hook : definition.hooks) {
            hook(L, metatable, fields);
            lua_settop(L, classTable);
        }
        lua_settop(L, top);
    }

    void LuaPublishClass(lua_State *L, const LuaClass &definition, int targetIndex) {
        targetIndex = lua_absindex(L, targetIndex);
        luaL_checkstack(L, 2, "cannot publish a class");
        if (lua_rawgetp(L, LUA_REGISTRYINDEX, &definition.classTableKey) == LUA_TNIL) {
            lua_pop(L, 1);
            throw std::logic_error("Bind: '" + definition.desc.name + "' is published in a Lua state it was not materialised in");
        }
        lua_pushstring(L, definition.desc.name.c_str());
        lua_insert(L, -2);
        lua_rawset(L, targetIndex);
    }

    void LuaRegisterClassGlobal(lua_State *L, const LuaClass &definition, Side runtime) {
        LuaMaterialiseClass(L, definition, runtime);
        luaL_checkstack(L, 1, "cannot register a class");
        lua_pushglobaltable(L);
        LuaPublishClass(L, definition, -1);
        lua_pop(L, 1);
    }

    const LuaClass *LuaClassAt(lua_State *L, int index) {
        if (lua_type(L, index) != LUA_TUSERDATA) {
            return nullptr;
        }
        luaL_checkstack(L, 2, nullptr);
        if (lua_getmetatable(L, index) == 0) {
            return nullptr;
        }
        lua_rawgetp(L, -1, &classKey);
        const LuaClass *definition = static_cast<const LuaClass *>(lua_touserdata(L, -1));
        lua_pop(L, 2);
        return definition;
    }

    void *LuaToObject(lua_State *L, int index, const LuaClass *wanted) {
        const LuaClass *actual = LuaClassAt(L, index);
        if (actual == nullptr || wanted == nullptr) {
            return nullptr;
        }
        return Upcast(*actual, lua_touserdata(L, index), *wanted);
    }

    void *LuaAllocateObject(lua_State *L, const LuaClass *definition, const char *name, size_t size) {
        luaL_checkstack(L, 2, nullptr);
        if (definition == nullptr || lua_rawgetp(L, LUA_REGISTRYINDEX, definition) == LUA_TNIL) {
            luaL_error(L, "%s is not available in this Lua state", name);
        }
        void *block = lua_newuserdatauv(L, size, 0);
        lua_insert(L, -2);
        return block;
    }

    int LuaRaiseNotCopyable(lua_State *L, const LuaFunction &function, const char *name) {
        return luaL_error(L, "'%s' cannot return a %s by reference: the class cannot be copied", function.label.c_str(), name);
    }

    LuaThunk LuaNativeThunk(std::shared_ptr<NativeFunction> function) {
        LuaThunk thunk;
        thunk.native = std::move(function);
        return thunk;
    }

    LuaFunction LuaMakeFunction(const std::string &owner, std::string name, Side side, LuaThunk thunk) {
        return MakeFunction(owner, std::move(name), '.', side, std::move(thunk));
    }

    void LuaPushFunction(lua_State *L, const LuaFunction &function) {
        PushClosure(L, CallStatic, function);
    }

    void LuaPushReadOnlyTable(lua_State *L, int fieldsIndex, const std::string &name) {
        fieldsIndex = lua_absindex(L, fieldsIndex);
        lua_newtable(L);
        lua_newtable(L);
        lua_pushvalue(L, fieldsIndex);
        lua_setfield(L, -2, "__index");
        lua_pushlstring(L, name.data(), name.size());
        lua_pushcclosure(L, RefuseWrite, 1);
        lua_setfield(L, -2, "__newindex");
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");
        lua_setmetatable(L, -2);
    }
} // namespace Framework::Scripting::Bind::Detail
