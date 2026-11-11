/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "namespace.h"

#include <lauxlib.h>
#include <v8pp/convert.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

namespace Framework::Scripting::Bind {
    // In a Lua state, registry[&record.tableKey] is the table of the namespace: empty, reading from the table that
    // holds the members and refusing every write, as the table of a class does (lua_class.cpp).
    struct Namespace::Record final {
        struct Member final {
            Side side        = Side::Shared;
            size_t described = 0;                        // its index in desc.functions, or in desc.properties for a constant
            std::optional<Detail::LuaFunction> function; // empty for a constant
            V8Maker v8;                                  // of a function
            ConstantValue constant;
            bool runtime = false; // a constant each materialisation is given: `constant` only tells its type
        };

        // What `constants` gives the runtime constant `name`, or nullptr.
        static const ConstantValue *Given(const RuntimeConstants &constants, const std::string &name) {
            const auto given = std::find_if(constants.begin(), constants.end(), [&name](const std::pair<std::string, ConstantValue> &constant) {
                return constant.first == name;
            });
            return given != constants.end() ? &given->second : nullptr;
        }

        // The value of a constant in one materialisation: its own, or what the runtime gives it.
        const ConstantValue &ValueOf(const Member &member, const RuntimeConstants &constants) const {
            return member.runtime ? *Given(constants, desc.properties[member.described].name) : member.constant;
        }

        // Throws for the first runtime constant of side `runtime` that `constants` has no value of its type for.
        void RequireConstants(Side runtime, const RuntimeConstants &constants) const {
            for (const Member &member : members) {
                if (!member.runtime || !Materialises(member.side, runtime)) {
                    continue;
                }
                const std::string &name    = desc.properties[member.described].name;
                const ConstantValue *given = Given(constants, name);
                if (given == nullptr) {
                    throw std::logic_error("Bind: '" + desc.name + "." + name + "' is a runtime constant, and the runtime gives it no value");
                }
                if (given->index() != member.constant.index()) {
                    throw std::logic_error("Bind: '" + desc.name + "." + name + "' is a runtime constant, and the runtime gives it a value of another type");
                }
            }
        }

        ClassDesc desc;
        // In the order they were declared. A Lua state points at the function of a member, so it stays where it is.
        std::deque<Member> members;
        char tableKey = 0;
    };

    namespace {
        // 2^53: an integral double up to it is an exact integer.
        constexpr double kExactIntegerLimit = 9007199254740992.0;

        // The definitions are never destroyed, and neither is what holds them.
        void Keep(std::shared_ptr<void> record) {
            static auto *kept = new std::vector<std::shared_ptr<void>>();
            kept->push_back(std::move(record));
        }

        void RefuseSecond(const ClassDesc &desc, const std::string &name) {
            const bool function = std::any_of(desc.functions.begin(), desc.functions.end(), [&name](const FunctionDesc &declared) {
                return declared.name == name;
            });
            const bool constant = std::any_of(desc.properties.begin(), desc.properties.end(), [&name](const PropertyDesc &declared) {
                return declared.name == name;
            });
            if (function || constant) {
                throw std::logic_error("Bind: '" + desc.name + "." + name + "' is declared twice, and a member cannot be overloaded");
            }
        }

        void PushConstant(lua_State *L, const Namespace::ConstantValue &constant) {
            if (const std::string *text = std::get_if<std::string>(&constant)) {
                lua_pushlstring(L, text->data(), text->size());
                return;
            }
            if (const bool *flag = std::get_if<bool>(&constant)) {
                lua_pushboolean(L, *flag ? 1 : 0);
                return;
            }
            const double number = std::get<double>(constant);
            if (number >= -kExactIntegerLimit && number <= kExactIntegerLimit && std::trunc(number) == number) {
                lua_pushinteger(L, static_cast<lua_Integer>(number));
            }
            else {
                lua_pushnumber(L, static_cast<lua_Number>(number));
            }
        }

        v8::Local<v8::Value> ConstantToV8(v8::Isolate *isolate, const Namespace::ConstantValue &constant) {
            if (const std::string *text = std::get_if<std::string>(&constant)) {
                return v8pp::to_v8(isolate, *text);
            }
            if (const bool *flag = std::get_if<bool>(&constant)) {
                return v8::Boolean::New(isolate, *flag);
            }
            return v8::Number::New(isolate, std::get<double>(constant));
        }
    } // namespace

    Namespace::Namespace(std::string name, std::string description): _record(std::make_shared<Record>()) {
        _record->desc.name        = std::move(name);
        _record->desc.description = std::move(description);
        _record->desc.isNamespace = true;
        Keep(_record);
        ApiCatalog::Get().Record(&_record->desc);
    }

    Namespace &Namespace::Function(std::string name, NativeFunction function, Doc doc, Side side) {
        FunctionDesc desc = Detail::DescribeContextFunction(QualifiedName(name), doc, true, side);
        desc.name         = std::move(name);
        auto shared       = std::make_shared<NativeFunction>(std::move(function));
        return AddFunction(std::move(desc), Detail::LuaNativeThunk(shared), [shared](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            return Detail::V8NativeFunction(isolate, context, shared);
        });
    }

    Namespace &Namespace::AddFunction(FunctionDesc desc, Detail::LuaThunk lua, V8Maker v8) {
        RefuseSecond(_record->desc, desc.name);
        Record::Member member;
        member.side      = desc.side;
        member.described = _record->desc.functions.size();
        member.function  = Detail::LuaMakeFunction(_record->desc.name, desc.name, desc.side, std::move(lua));
        member.v8        = std::move(v8);
        _record->members.push_back(std::move(member));
        _record->desc.functions.push_back(std::move(desc));
        return *this;
    }

    Namespace &Namespace::AddConstant(std::string name, TypeRef type, ConstantValue value, std::string description, Side side, bool runtime) {
        RefuseSecond(_record->desc, name);
        Record::Member member;
        member.side      = side;
        member.described = _record->desc.properties.size();
        member.constant  = std::move(value);
        member.runtime   = runtime;
        _record->members.push_back(std::move(member));
        _record->desc.properties.push_back({std::move(name), std::move(type), std::move(description), true, true, side});
        return *this;
    }

    Namespace &Namespace::Constant(std::string name, bool value, std::string description, Side side) {
        return AddConstant(std::move(name), TypeOf<bool>(), value, std::move(description), side);
    }

    Namespace &Namespace::Constant(std::string name, double value, std::string description, Side side) {
        return AddConstant(std::move(name), TypeOf<double>(), value, std::move(description), side);
    }

    Namespace &Namespace::Constant(std::string name, std::string value, std::string description, Side side) {
        return AddConstant(std::move(name), TypeOf<std::string>(), std::move(value), std::move(description), side);
    }

    const ClassDesc &Namespace::Describe() const {
        return _record->desc;
    }

    std::string Namespace::QualifiedName(const std::string &member) const {
        return _record->desc.name + "." + member;
    }

    v8::Local<v8::Object> Namespace::MaterialiseV8(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, v8pp::metadata::registry &catalog, Side runtime, const RuntimeConstants &constants) const {
        const ClassDesc &desc = _record->desc;
        for (const Record::Member &member : _record->members) {
            if (member.function && member.v8.empty() && Materialises(member.side, runtime)) {
                throw std::logic_error("Bind: '" + QualifiedName(member.function->name) + "' returns an object of a class that cannot be copied, which JavaScript cannot receive");
            }
        }
        _record->RequireConstants(runtime, constants);

        v8::EscapableHandleScope scope(isolate);
        v8::Local<v8::Object> object   = v8::Object::New(isolate);
        v8pp::metadata::symbol &symbol = catalog.global_object(desc.name, desc.description);
        for (const Record::Member &member : _record->members) {
            if (!Materialises(member.side, runtime)) {
                continue;
            }
            if (member.function) {
                const FunctionDesc &function = desc.functions[member.described];
                object->Set(context, v8pp::to_v8(isolate, function.name), member.v8(isolate, context).ToLocalChecked()).Check();
                symbol.record(v8pp::metadata::function_of<v8::FunctionCallback>(function.name, Detail::V8FunctionOptions(function), false));
            }
            else {
                const PropertyDesc &constant = desc.properties[member.described];
                object->DefineOwnProperty(context, v8pp::to_v8(isolate, constant.name), ConstantToV8(isolate, _record->ValueOf(member, constants)), static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete)).Check();
                symbol.add_property(constant.name, ToTypeScript(constant.type), constant.description, true);
            }
        }
        target->Set(context, v8pp::to_v8(isolate, desc.name), object).Check();
        return scope.Escape(object);
    }

    void Namespace::MaterialiseLua(lua_State *L, Side runtime, const RuntimeConstants &constants) const {
        const ClassDesc &desc = _record->desc;
        luaL_checkstack(L, 6, "cannot materialise a namespace");
        const int top = lua_gettop(L);

        if (lua_rawgetp(L, LUA_REGISTRYINDEX, &_record->tableKey) == LUA_TNIL) {
            lua_pop(L, 1);
            _record->RequireConstants(runtime, constants);
            lua_newtable(L);
            const int fields = lua_gettop(L);
            for (const Record::Member &member : _record->members) {
                if (!Materialises(member.side, runtime)) {
                    continue;
                }
                if (member.function) {
                    Detail::LuaPushFunction(L, *member.function);
                    lua_setfield(L, fields, member.function->name.c_str());
                }
                else {
                    PushConstant(L, _record->ValueOf(member, constants));
                    lua_setfield(L, fields, desc.properties[member.described].name.c_str());
                }
            }
            Detail::LuaPushReadOnlyTable(L, fields, desc.name);
            lua_pushvalue(L, -1);
            lua_rawsetp(L, LUA_REGISTRYINDEX, &_record->tableKey);
        }

        const int table = lua_gettop(L);
        lua_pushglobaltable(L);
        lua_pushlstring(L, desc.name.data(), desc.name.size());
        lua_pushvalue(L, table);
        lua_rawset(L, -3);
        lua_settop(L, top);
    }
} // namespace Framework::Scripting::Bind
