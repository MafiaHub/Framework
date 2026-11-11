/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_runtime_ut.h"
#include "lua_test_helpers.h"
#include "node_test_helpers.h"

#include "scripting/bind/call.h"
#include "scripting/bind/class.h"
#include "scripting/bind/namespace.h"
#include "scripting/bind/value.h"
#include "scripting/builtins/builtins.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/lua/lua_transfer.h"
#include "scripting/node_engine.h"
#include "scripting/node_runtime.h"
#include "scripting/scripting_catalog.h"
#include "scripting/value_transfer.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace LuaTransferTest {
    namespace Bind = Framework::Scripting::Bind;
    using Bind::Side;
    using Framework::Scripting::LuaRuntime;
    using Framework::Scripting::LuaTransfer;
    using Framework::Scripting::TransferredValue;
    using Framework::Scripting::ValueTransfer;
    using Copied = Framework::Utils::Result<TransferredValue, std::string>;

    // A handle as a project writes one: it wraps an id, and is built from it. The entity of id 0 is gone.
    class Ticket {
      public:
        explicit Ticket(uint64_t id): _id(id) {
            if (id == 0) {
                throw std::runtime_error("ticket 0 no longer exists");
            }
        }

        uint64_t GetId() const {
            return _id;
        }

      private:
        uint64_t _id;
    };

    // Derived from a class that crosses, without saying how it crosses itself.
    class VipTicket final: public Ticket {
      public:
        explicit VipTicket(uint64_t id): Ticket(id) {}
    };

    // A bound class nobody taught the transfer or JSON.
    struct Plain final {
        int value = 0;
    };

    // A bound class whose host type raises Lua errors.
    struct Faulty final {};
} // namespace LuaTransferTest

FW_SCRIPT_CLASS(LuaTransferTest::Ticket, "TransferTicket");
FW_SCRIPT_CLASS(LuaTransferTest::VipTicket, "TransferVipTicket");
FW_SCRIPT_CLASS(LuaTransferTest::Plain, "TransferPlain");
FW_SCRIPT_CLASS(LuaTransferTest::Faulty, "TransferFaulty");

namespace LuaTransferTest {
    inline const Bind::Class<Ticket> &TicketDefinition() {
        using Bind::Doc;
        static const auto definition = Bind::Class<Ticket>("TransferTicket", "A handle on a ticket.").Constructor<uint64_t>(Doc("Finds a ticket.").Param("id", "Its id.")).Property("id", &Ticket::GetId, "Its id.").Transfer(&Ticket::GetId);
        return definition;
    }

    inline const Bind::Class<VipTicket> &VipTicketDefinition() {
        using Bind::Doc;
        TicketDefinition();
        static const auto definition = Bind::Class<VipTicket>("TransferVipTicket", "A ticket with privileges.").Inherit<Ticket>().Constructor<uint64_t>(Doc("Finds a ticket.").Param("id", "Its id."));
        return definition;
    }

    inline const Bind::Class<Plain> &PlainDefinition() {
        using Bind::Doc;
        static const auto definition = Bind::Class<Plain>("TransferPlain", "Crosses nothing.").Constructor<>(Doc("Creates one."));
        return definition;
    }

    inline const Bind::Class<Faulty> &FaultyDefinition() {
        using Bind::Doc;
        static const auto definition = Bind::Class<Faulty>("TransferFaulty", "Fails to cross.").Constructor<>(Doc("Creates one."));
        return definition;
    }

    // What the functions of Codec are given back when a conversion fails.
    inline std::string Failed(const std::string &error) {
        return "<failed> " + error;
    }

    // The dynamic interface over the conversions, the same function for both languages.
    inline const Bind::Namespace &CodecDefinition() {
        using Bind::Call;
        using Bind::Doc;
        using Bind::TypeOf;
        namespace Type                          = Bind::Type;
        static const Bind::Namespace definition = Bind::Namespace("Codec", "Copies values and writes them as JSON.")
                                                      .Function(
                                                          "copy",
                                                          [](Call &call) {
                                                              const auto copied = call.Arg(0).ToTransfer();
                                                              if (!copied) {
                                                                  call.Return(Failed(copied.GetError()));
                                                                  return;
                                                              }
                                                              call.Return(call.FromTransfer(copied.GetValue()));
                                                          },
                                                          Doc("Copies a value and rebuilds it.").Param("value", Type::Any(), "Value to copy.").ReturnType(Type::Any()))
                                                      .Function(
                                                          "rebuild",
                                                          [](Call &call) {
                                                              TransferredValue value;
                                                              value.kind = static_cast<TransferredValue::Kind>(call.Arg(0).AsInteger().value_or(0));
                                                              value.text = call.Arg(1).AsString();
                                                              call.Return(call.FromTransfer(value));
                                                              call.Return("went on");
                                                          },
                                                          Doc("Rebuilds a value of a kind.").Param("kind", TypeOf<int>(), "Kind.").Param("text", TypeOf<std::string>(), "Text.").ReturnType(Type::Any()))
                                                      .Function(
                                                          "json",
                                                          [](Call &call) {
                                                              const auto json = call.Arg(0).ToJson();
                                                              call.Return(json ? json.GetValue() : Failed(json.GetError()));
                                                          },
                                                          Doc("Writes a value as JSON.").Param("value", Type::Any(), "Value to write.").ReturnType(TypeOf<std::string>()))
                                                      .Function(
                                                          "jsonOrThrow",
                                                          [](Call &call) {
                                                              const auto json = call.Arg(0).ToJson();
                                                              if (!json) {
                                                                  call.ThrowError("Codec.jsonOrThrow: " + json.GetError());
                                                                  return;
                                                              }
                                                              call.Return(json.GetValue());
                                                          },
                                                          Doc("Writes a value as JSON, or throws.").Param("value", Type::Any(), "Value to write.").ReturnType(TypeOf<std::string>()))
                                                      .Function(
                                                          "parse",
                                                          [](Call &call) {
                                                              const auto parsed = call.ParseJson(call.Arg(0).AsString());
                                                              if (!parsed) {
                                                                  call.Return(Failed(parsed.GetError()));
                                                                  return;
                                                              }
                                                              call.Return(parsed.GetValue());
                                                          },
                                                          Doc("Reads JSON.").Param("text", TypeOf<std::string>(), "JSON text.").ReturnType(Type::Any()));
        return definition;
    }

    inline void SetGlobal(lua_State *L, const char *name) {
        lua_pushglobaltable(L);
        lua_pushstring(L, name);
        lua_pushvalue(L, -3);
        lua_rawset(L, -3);
        lua_pop(L, 2);
    }

    // A JavaScript runtime and two Lua states, each with the value types, the test classes and Codec.
    class Worlds final {
      public:
        explicit Worlds(size_t otherMemoryLimit = 0): lua(LuaRuntimeTest::Options()), other(LuaRuntimeTest::Options(otherMemoryLimit)) {
            using namespace Framework::Scripting;
            _engine.Init();
            Builtins::RegisterTransferTypes();
            // After the framework's own, as a project registers its handles.
            static bool registered = false;
            if (!registered) {
                registered = true;
                TicketDefinition().RegisterTransfer();
            }
            VipTicketDefinition();
            PlainDefinition();

            std::string error;
            js = _engine.CreateRuntime(error);
            // A catalog of its own, so that the test classes stay out of the one the framework exports.
            static int count = 0;
            SetScriptingCatalog(js->GetIsolate(), "lua_transfer/" + std::to_string(++count));
            Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                v8::Local<v8::Object> global = context->Global();
                Builtins::RegisterValueTypes(isolate, global);
                _ticket = TicketDefinition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), Side::Server);
                _vip    = VipTicketDefinition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), Side::Server);
                _plain  = PlainDefinition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), Side::Server);
                global->Set(context, v8pp::to_v8(isolate, "TransferTicket"), _ticket->js_function_template()->GetFunction(context).ToLocalChecked()).Check();
                global->Set(context, v8pp::to_v8(isolate, "TransferVipTicket"), _vip->js_function_template()->GetFunction(context).ToLocalChecked()).Check();
                global->Set(context, v8pp::to_v8(isolate, "TransferPlain"), _plain->js_function_template()->GetFunction(context).ToLocalChecked()).Check();
                CodecDefinition().MaterialiseV8(isolate, context, global, GetScriptingCatalog(isolate), Side::Server);
            });

            for (LuaRuntime *runtime : {&lua, &other}) {
                lua_State *L = runtime->State();
                Builtins::RegisterValueTypesLua(L, Side::Server);
                TicketDefinition().RegisterLuaGlobal(L, Side::Server);
                VipTicketDefinition().RegisterLuaGlobal(L, Side::Server);
                PlainDefinition().RegisterLuaGlobal(L, Side::Server);
                CodecDefinition().RegisterLuaGlobal(L, Side::Server);
                // Spells a value with its types: an integer as 1, a float as 1.0, a table with its keys in order.
                LuaRuntimeTest::Run(*runtime, R"lua(
                    function show(value)
                        local kind = type(value)
                        if kind == 'string' then return '"' .. value .. '"' end
                        if kind ~= 'table' then return tostring(value) end
                        local keys = {}
                        for key in next, value do keys[#keys + 1] = key end
                        table.sort(keys, function(a, b)
                            if type(a) == type(b) then return a < b end
                            return type(a) == 'number'
                        end)
                        local parts = {}
                        for index, key in ipairs(keys) do
                            local label = type(key) == 'string' and key or '[' .. tostring(key) .. ']'
                            parts[index] = label .. '=' .. show(rawget(value, key))
                        end
                        return '{' .. table.concat(parts, ',') .. '}'
                    end
                )lua");
            }
        }

        ~Worlds() {
            _plain.reset();
            _vip.reset();
            _ticket.reset();
            // The caches and the catalog are keyed by isolate address, which a later isolate may reuse.
            Framework::Scripting::Builtins::UnregisterAll(js->GetIsolate());
            Framework::Scripting::ClearScriptingCatalog(js->GetIsolate());
            js.reset();
            _engine.Shutdown();
        }

        template <typename Action>
        void Enter(Action action) {
            v8::Isolate *isolate = js->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = js->GetContext();
            v8::Context::Scope contextScope(context);
            action(isolate, context);
        }

        // What `source` gives in JavaScript, as String() spells it.
        std::string Js(const std::string &source) {
            return NodeTest::Eval(*js, source);
        }

        // What the expression gives in the first Lua state, as tostring() spells it.
        std::string Lua(const std::string &expression) {
            return LuaRuntimeTest::Eval(lua, expression);
        }

        // The same in the second state, the one values arrive in.
        std::string Other(const std::string &expression) {
            return LuaRuntimeTest::Eval(other, expression);
        }

        // The copy of what `expression` gives in JavaScript.
        Copied CopyJs(const std::string &expression) {
            Copied copied = Copied::Err("not run");
            Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                v8::Local<v8::Value> value = v8::Script::Compile(context, v8pp::to_v8(isolate, "(" + expression + ")")).ToLocalChecked()->Run(context).ToLocalChecked();
                copied                     = ValueTransfer::Copy(isolate, context, value);
            });
            return copied;
        }

        // Rebuilds `value` as globalThis.received. Empty, or why it could not.
        std::string StoreJs(const TransferredValue &value) {
            std::string error;
            Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                v8::TryCatch tryCatch(isolate);
                v8::Local<v8::Value> rebuilt;
                if (!ValueTransfer::Rebuild(isolate, context, value).ToLocal(&rebuilt)) {
                    v8::String::Utf8Value text(isolate, tryCatch.Exception());
                    error = std::string("rebuild threw: ") + (*text ? *text : "");
                    return;
                }
                context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "received"), rebuilt).Check();
            });
            return error;
        }

        // The copy of what `expression` gives in a Lua state.
        static Copied CopyLua(LuaRuntime &runtime, const std::string &expression) {
            lua_State *L            = runtime.State();
            const std::string chunk = "return " + expression;
            std::string error;
            if (luaL_loadbufferx(L, chunk.data(), chunk.size(), "=eval", "t") != LUA_OK) {
                error = lua_tostring(L, -1);
                lua_pop(L, 1);
                return Copied::Err("<lua> " + error);
            }
            if (!runtime.Call(0, 1, error)) {
                return Copied::Err("<lua> " + error);
            }
            Copied copied = LuaTransfer::Copy(L, -1);
            lua_pop(L, 1);
            return copied;
        }

        // Rebuilds `value` as the global `received` of a Lua state. Empty, or why it could not.
        static std::string StoreLua(LuaRuntime &runtime, const TransferredValue &value) {
            lua_State *L = runtime.State();
            std::string error;
            if (!LuaTransfer::Rebuild(L, value, error)) {
                return error.empty() ? "rebuild failed without saying why" : error;
            }
            SetGlobal(L, "received");
            return {};
        }

        // Rebuilds `value` in a JavaScript runtime nothing was registered in, and spells it there.
        std::string SpellInFreshJs(const TransferredValue &value) {
            std::string error;
            std::unique_ptr<Framework::Scripting::NodeRuntime> fresh = _engine.CreateRuntime(error);
            std::string spelled;
            {
                v8::Isolate *isolate = fresh->GetIsolate();
                v8::Locker locker(isolate);
                v8::Isolate::Scope isolateScope(isolate);
                v8::HandleScope handleScope(isolate);
                v8::Local<v8::Context> context = fresh->GetContext();
                v8::Context::Scope contextScope(context);
                v8::TryCatch tryCatch(isolate);
                v8::Local<v8::Value> rebuilt;
                if (ValueTransfer::Rebuild(isolate, context, value).ToLocal(&rebuilt)) {
                    v8::String::Utf8Value text(isolate, rebuilt);
                    spelled = *text ? *text : "";
                }
                else {
                    v8::String::Utf8Value text(isolate, tryCatch.Exception());
                    spelled = std::string("rebuild threw: ") + (*text ? *text : "");
                }
            }
            Framework::Scripting::Builtins::UnregisterAll(fresh->GetIsolate());
            Framework::Scripting::ClearScriptingCatalog(fresh->GetIsolate());
            return spelled;
        }

        // From the first Lua state into the second, as `received`.
        std::string LuaToLua(const std::string &expression) {
            const Copied copied = CopyLua(lua, expression);
            return copied ? StoreLua(other, copied.GetValue()) : copied.GetError();
        }

        // From JavaScript into the second Lua state, as `received`.
        std::string JsToLua(const std::string &expression) {
            const Copied copied = CopyJs(expression);
            return copied ? StoreLua(other, copied.GetValue()) : copied.GetError();
        }

        // From the first Lua state into JavaScript, as globalThis.received.
        std::string LuaToJs(const std::string &expression) {
            const Copied copied = CopyLua(lua, expression);
            return copied ? StoreJs(copied.GetValue()) : copied.GetError();
        }

        // Why the first Lua state cannot copy `expression`, or empty when it can.
        std::string Refusal(const std::string &expression) {
            const Copied copied = CopyLua(lua, expression);
            return copied ? std::string() : copied.GetError();
        }

        std::unique_ptr<Framework::Scripting::NodeRuntime> js;
        LuaRuntime lua;
        LuaRuntime other;

      private:
        Framework::Scripting::NodeEngine _engine;
        std::unique_ptr<v8pp::class_<Ticket>> _ticket;
        std::unique_ptr<v8pp::class_<VipTicket>> _vip;
        std::unique_ptr<v8pp::class_<Plain>> _plain;
    };

    inline const char *KindName(TransferredValue::Kind kind) {
        using Kind = TransferredValue::Kind;
        switch (kind) {
        case Kind::Undefined: return "Undefined";
        case Kind::Null: return "Null";
        case Kind::Boolean: return "Boolean";
        case Kind::Number: return "Number";
        case Kind::BigInt: return "BigInt";
        case Kind::String: return "String";
        case Kind::Array: return "Array";
        case Kind::Object: return "Object";
        case Kind::Date: return "Date";
        case Kind::RegExp: return "RegExp";
        case Kind::Map: return "Map";
        case Kind::Set: return "Set";
        case Kind::ArrayBuffer: return "ArrayBuffer";
        case Kind::TypedArray: return "TypedArray";
        case Kind::Error: return "Error";
        case Kind::HostObject: return "HostObject";
        case Kind::Function: return "Function";
        }
        return "?";
    }

    // The kind a Lua value is copied as, or the refusal.
    inline std::string KindOf(Worlds &worlds, const std::string &expression) {
        const Copied copied = Worlds::CopyLua(worlds.lua, expression);
        return copied ? KindName(copied.GetValue().kind) : copied.GetError();
    }

    // The names of the properties of a copied object, in the order they were copied, joined with commas.
    inline std::string PropertyNames(const TransferredValue &value) {
        std::string names;
        for (const auto &[name, field] : value.properties) {
            names += (names.empty() ? "" : ",") + name;
        }
        return names;
    }

    // `depth` tables, each the only element of the one around it.
    inline std::string NestedLua(int depth) {
        return "(function() local nested = {} for _ = 2, " + std::to_string(depth) + " do nested = { nested } end return nested end)()";
    }

    inline TransferredValue NestedArrays(int depth) {
        TransferredValue value;
        value.kind = TransferredValue::Kind::Array;
        for (int i = 1; i < depth; ++i) {
            TransferredValue outer;
            outer.kind = TransferredValue::Kind::Array;
            outer.items.push_back(std::move(value));
            value = std::move(outer);
        }
        return value;
    }
} // namespace LuaTransferTest

MODULE(lua_transfer, {
    using namespace LuaTransferTest;
    using Kind = TransferredValue::Kind;

    // --- From Lua and back into Lua, row by row ---

    IT("copies nil, booleans and strings between Lua states", {
        Worlds worlds;
        EQUALS(KindOf(worlds, "nil"), "Undefined");
        EQUALS(KindOf(worlds, "true"), "Boolean");
        EQUALS(KindOf(worlds, "'text'"), "String");
        EQUALS(worlds.LuaToLua("{ flag = true, off = false, text = 'caf\\u{e9} \\u{1f600}', empty = '' }"), "");
        EQUALS(worlds.Other("show(received)"), "{empty=\"\",flag=true,off=false,text=\"caf\xC3\xA9 \xF0\x9F\x98\x80\"}");
        EQUALS(worlds.LuaToLua("nil"), "");
        EQUALS(worlds.Other("received"), "nil");
    });

    IT("copies an integer within 2^53 as a number and brings an integral number back as an integer", {
        Worlds worlds;
        EQUALS(KindOf(worlds, "7"), "Number");
        EQUALS(KindOf(worlds, "1.5"), "Number");
        EQUALS(KindOf(worlds, "2^53 | 0"), "Number");
        EQUALS(KindOf(worlds, "-(2^53 | 0)"), "Number");
        EQUALS(worlds.LuaToLua("{ 7, 1.5, 2.0, -3, 2^53 | 0, 1e300, 2^60 }"), "");
        EQUALS(worlds.Other("show(received)"), "{[1]=7,[2]=1.5,[3]=2,[4]=-3,[5]=9007199254740992,[6]=1e+300,[7]=1.152921504606847e+18}");
        EQUALS(worlds.Other("math.type(received[3]) .. ' ' .. math.type(received[6]) .. ' ' .. math.type(received[7])"), "integer float float");
    });

    IT("keeps what a float alone can say: a NaN, an infinity and a negative zero", {
        Worlds worlds;
        EQUALS(worlds.LuaToLua("{ 0/0, 1/0, -1/0, -0.0 }"), "");
        EQUALS(worlds.Other("tostring(received[1] ~= received[1]) .. ' ' .. tostring(received[2]) .. ' ' .. tostring(received[3]) .. ' ' .. tostring(1 / received[4])"), "true inf -inf -inf");
    });

    IT("copies an integer beyond 2^53 as a BigInt and brings it back exact", {
        Worlds worlds;
        EQUALS(KindOf(worlds, "(2^53 | 0) + 1"), "BigInt");
        EQUALS(KindOf(worlds, "-(2^53 | 0) - 1"), "BigInt");
        EQUALS(worlds.LuaToLua("{ (2^53 | 0) + 1, -(2^53 | 0) - 1, math.maxinteger, math.mininteger }"), "");
        EQUALS(worlds.Other("show(received)"), "{[1]=9007199254740993,[2]=-9007199254740993,[3]=9223372036854775807,[4]=-9223372036854775808}");
        EQUALS(worlds.Other("math.type(received[4])"), "integer");
    });

    IT("copies a table whose keys are 1..n as an Array, the empty table as an empty one, any other as an Object", {
        Worlds worlds;
        EQUALS(KindOf(worlds, "{ 'a', 'b' }"), "Array");
        EQUALS(KindOf(worlds, "{}"), "Array");
        EQUALS(KindOf(worlds, "{ a = 1 }"), "Object");
        EQUALS(KindOf(worlds, "{ 'a', nil, 'c' }"), "Object");
        EQUALS(KindOf(worlds, "{ [0] = 'a', 'b' }"), "Object");
        EQUALS(KindOf(worlds, "{ 'a', extra = true }"), "Object");

        const Copied list = Worlds::CopyLua(worlds.lua, "{ 'a', { 'b', {} }, { k = { 1, 2 } } }");
        EQUALS(list.IsOk(), true);
        EQUALS(list.GetValue().items.size(), 3u);
        EQUALS(list.GetValue().items[1].items[1].kind == Kind::Array && list.GetValue().items[1].items[1].items.empty(), true);
        EQUALS(Worlds::StoreLua(worlds.other, list.GetValue()), "");
        EQUALS(worlds.Other("show(received)"), "{[1]=\"a\",[2]={[1]=\"b\",[2]={}},[3]={k={[1]=1,[2]=2}}}");
        EQUALS(worlds.Other("#received"), "3");
    });

    IT("copies the keys of an Object in one order, integers as their decimal text before strings", {
        Worlds worlds;
        const Copied object = Worlds::CopyLua(worlds.lua, "{ zebra = 1, [10] = 2, alpha = 3, [-2] = 4, [3] = 5, ['7'] = 6, Beta = 7 }");
        EQUALS(object.IsOk(), true);
        EQUALS(PropertyNames(object.GetValue()), "-2,3,10,7,Beta,alpha,zebra");
        // A key that was an integer arrives as the string it was copied as: nothing guesses it back.
        EQUALS(Worlds::StoreLua(worlds.other, object.GetValue()), "");
        EQUALS(worlds.Other("show(received)"), "{-2=4,10=2,3=5,7=6,Beta=7,alpha=3,zebra=1}");
        EQUALS(worlds.Other("tostring(received[10]) .. ' ' .. tostring(received['10'])"), "nil 2");
    });

    IT("lets a string key win over the integer key of the same text", {
        Worlds worlds;
        const Copied object = Worlds::CopyLua(worlds.lua, "{ [5] = 'integer', ['5'] = 'string', x = 1 }");
        EQUALS(object.IsOk(), true);
        EQUALS(PropertyNames(object.GetValue()), "5,x");
        EQUALS(object.GetValue().properties[0].second.text, "string");
    });

    IT("copies a table by what it holds itself and drops its metatable", {
        Worlds worlds;
        LuaRuntimeTest::Run(worlds.lua, R"lua(
            ran = 0
            local function count() ran = ran + 1 end
            proxy = setmetatable({ own = 1 }, {
                __index = function(_, key) count() return 'computed' end,
                __pairs = function(self) count() return next, { fake = true } end,
                __len = function() count() return 99 end,
                __tostring = function() count() return 'proxy' end,
            })
            Account = {}
            Account.__index = Account
            function Account:deposit() end
            account = setmetatable({ balance = 10 }, Account)
        )lua");
        EQUALS(worlds.LuaToLua("{ proxy = proxy, account = account }"), "");
        EQUALS(worlds.Other("show(received)"), "{account={balance=10},proxy={own=1}}");
        EQUALS(worlds.Other("tostring(getmetatable(received.account)) .. ' ' .. tostring(received.account.deposit)"), "nil nil");
        EQUALS(worlds.Lua("ran"), "0");
    });

    IT("sends a copy, not the table", {
        Worlds worlds;
        LuaRuntimeTest::Run(worlds.lua, "state = { count = 1, list = { 1 } }");
        EQUALS(worlds.LuaToLua("state"), "");
        LuaRuntimeTest::Run(worlds.other, "received.count = 99 received.list[1] = 99");
        EQUALS(worlds.Lua("show(state)"), "{count=1,list={[1]=1}}");
    });

    IT("copies a table reached twice twice", {
        Worlds worlds;
        LuaRuntimeTest::Run(worlds.lua, "shared = { 1 }");
        EQUALS(worlds.LuaToLua("{ a = shared, b = shared }"), "");
        EQUALS(worlds.Other("show(received) .. ' ' .. tostring(received.a == received.b)"), "{a={[1]=1},b={[1]=1}} false");
    });

    IT("rebuilds the value types as themselves in another Lua state", {
        Worlds worlds;
        EQUALS(KindOf(worlds, "Vector3(1, 2, 3)"), "HostObject");
        EQUALS(worlds.LuaToLua("{ at = Vector3(1, 2, 3), uv = Vector2(0.5, -1), v4 = Vector4(1, 2, 3, 4), turn = Quaternion(1, 0, 0, 0), tint = Color(0.25, 0.5, 0.75, 1) }"), "");
        EQUALS(worlds.Other("tostring(received.at) .. ' ' .. tostring(received.uv) .. ' ' .. tostring(received.v4)"), "Vector3(1, 2, 3) Vector2(0.5, -1) Vector4(1, 2, 3, 4)");
        EQUALS(worlds.Other("tostring(received.turn) .. ' ' .. tostring(received.tint)"), "Quaternion(1, 0, 0, 0) Color(0.25, 0.5, 0.75, 1)");
        EQUALS(worlds.Other("tostring(received.at == Vector3(1, 2, 3)) .. ' ' .. tostring(received.at:add(Vector3.one()).x)"), "true 2.0");
    });

    IT("copies a value type as the bytes the JavaScript side copies it as", {
        Worlds worlds;
        const std::vector<std::pair<std::string, std::string>> values = {
            {"Vector2(0.5, -1)", "new Vector2(0.5, -1)"},
            {"Vector3(1, 2, 3)", "new Vector3(1, 2, 3)"},
            {"Vector4(1, 2, 3, 4)", "new Vector4(1, 2, 3, 4)"},
            {"Quaternion(0.5, 0.25, 0.125, 1)", "new Quaternion(0.5, 0.25, 0.125, 1)"},
            {"Color(0.25, 0.5, 0.75, 1)", "new Color(0.25, 0.5, 0.75, 1)"},
        };
        for (const auto &[lua, js] : values) {
            const Copied fromLua = Worlds::CopyLua(worlds.lua, lua);
            const Copied fromJs  = worlds.CopyJs(js);
            EQUALS(fromLua.IsOk() && fromJs.IsOk(), true);
            EQUALS(fromLua.GetValue().kind == Kind::HostObject && fromJs.GetValue().kind == Kind::HostObject, true);
            EQUALS(fromLua.GetValue().text, fromJs.GetValue().text);
            EQUALS(fromLua.GetValue().bytes == fromJs.GetValue().bytes && !fromLua.GetValue().bytes.empty(), true);
        }
    });

    // --- Between JavaScript and Lua ---

    IT("brings scalars, arrays and objects from JavaScript into Lua", {
        Worlds worlds;
        EQUALS(worlds.JsToLua("{ n: 1.5, i: 42, big: 2 ** 53, huge: 2 ** 60, s: 'caf\\u00e9', b: true, f: false, z: null, u: undefined, list: [1, [2, 'three', []]], nested: { deep: { ok: true } } }"), "");
        EQUALS(worlds.Other("show(received)"), "{b=true,big=9007199254740992,f=false,huge=1.152921504606847e+18,i=42,list={[1]=1,[2]={[1]=2,[2]=\"three\",[3]={}}},n=1.5,nested={deep={ok=true}},s=\"caf\xC3\xA9\"}");
        EQUALS(worlds.Other("math.type(received.i) .. ' ' .. math.type(received.n) .. ' ' .. math.type(received.big) .. ' ' .. math.type(received.huge)"), "integer float integer float");
        EQUALS(worlds.JsToLua("undefined"), "");
        EQUALS(worlds.Other("received"), "nil");
        EQUALS(worlds.JsToLua("null"), "");
        EQUALS(worlds.Other("received"), "nil");
    });

    IT("leaves a hole where a JavaScript array has null or undefined", {
        Worlds worlds;
        EQUALS(worlds.JsToLua("[1, null, 3, undefined, 5]"), "");
        EQUALS(worlds.Other("show(received)"), "{[1]=1,[3]=3,[5]=5}");
        EQUALS(worlds.JsToLua("[null, [undefined], null]"), "");
        EQUALS(worlds.Other("show(received)"), "{[2]={}}");
    });

    IT("keeps the key of a JavaScript object a string, even one that spells an integer", {
        Worlds worlds;
        EQUALS(worlds.JsToLua("{ 10: 'ten', 2: 'two', name: 'n' }"), "");
        EQUALS(worlds.Other("show(received)"), "{10=\"ten\",2=\"two\",name=\"n\"}");
        EQUALS(worlds.Other("tostring(received[10]) .. ' ' .. received['10']"), "nil ten");
    });

    IT("brings a BigInt that fits 64 bits into Lua as an integer and refuses a larger one", {
        Worlds worlds;
        EQUALS(worlds.JsToLua("[0n, -5n, 2n ** 63n - 1n, -(2n ** 63n), 2n ** 53n + 1n]"), "");
        EQUALS(worlds.Other("show(received)"), "{[1]=0,[2]=-5,[3]=9223372036854775807,[4]=-9223372036854775808,[5]=9007199254740993}");
        EQUALS(worlds.Other("math.type(received[1])"), "integer");
        EQUALS(worlds.JsToLua("{ ids: [1n, 2n ** 63n] }"), "Cannot copy value.ids[1]: a BigInt this large does not fit a Lua integer");
        EQUALS(worlds.JsToLua("{ id: -(2n ** 63n) - 1n }"), "Cannot copy value.id: a BigInt this large does not fit a Lua integer");
        EQUALS(worlds.JsToLua("2n ** 200n"), "Cannot copy value: a BigInt this large does not fit a Lua integer");
    });

    IT("brings a Date into Lua as integer milliseconds", {
        Worlds worlds;
        EQUALS(worlds.JsToLua("{ when: new Date(86400000), never: new Date(NaN) }"), "");
        EQUALS(worlds.Other("math.type(received.when) .. ' ' .. received.when"), "integer 86400000");
        EQUALS(worlds.Other("tostring(received.never ~= received.never)"), "true");
    });

    IT("brings a Map into Lua as a table and a Set as a sequence", {
        Worlds worlds;
        EQUALS(worlds.JsToLua("{ map: new Map([['k', { v: 1 }], [2, 'two'], [1.5, 'half'], ['7', 'seven'], ['gone', null]]), set: new Set([1, 'two', [3]]) }"), "");
        EQUALS(worlds.Other("show(received.map)"), "{[1.5]=\"half\",[2]=\"two\",7=\"seven\",k={v=1}}");
        EQUALS(worlds.Other("tostring(received.map[2]) .. ' ' .. tostring(received.map[7]) .. ' ' .. tostring(received.map.gone)"), "two nil nil");
        EQUALS(worlds.Other("show(received.set)"), "{[1]=1,[2]=\"two\",[3]={[1]=3}}");
    });

    IT("refuses a Map key Lua cannot index a table with, and says which", {
        Worlds worlds;
        EQUALS(worlds.JsToLua("{ map: new Map([['a', 1], [{ id: 1 }, 2]]) }"), "Cannot copy value.map.<key 1>: only a string or a number can be a key in Lua");
        EQUALS(worlds.JsToLua("new Map([[true, 1]])"), "Cannot copy value.<key 0>: only a string or a number can be a key in Lua");
        EQUALS(worlds.JsToLua("new Map([[NaN, 1]])"), "Cannot copy value.<key 0>: NaN cannot be a key in Lua");
        EQUALS(worlds.JsToLua("new Map([['a', /x/]])"), "Cannot copy value.<value 0>: a RegExp cannot be copied to Lua");
        EQUALS(worlds.JsToLua("new Set([1, /x/])"), "Cannot copy value.<member 1>: a RegExp cannot be copied to Lua");
    });

    IT("brings binary data into Lua as a string of its bytes", {
        Worlds worlds;
        EQUALS(worlds.JsToLua("{ raw: new Uint8Array([104, 105]).buffer, view: new Uint8Array([0, 255, 65]), words: new Uint16Array([0x4241]), buf: Buffer.from('h\\u00e9'), none: new ArrayBuffer(0) }"), "");
        EQUALS(worlds.Other("received.raw .. ' ' .. received.words .. ' ' .. received.buf .. ' ' .. #received.none"), "hi AB h\xC3\xA9 0");
        EQUALS(worlds.Other("table.concat({ received.view:byte(1, -1) }, ',')"), "0,255,65");
    });

    IT("brings an Error into Lua as a table with its name, message, stack and own fields", {
        Worlds worlds;
        EQUALS(worlds.JsToLua("Object.assign(new TypeError('bad input'), { code: 'E_BAD', detail: { line: 3 } })"), "");
        EQUALS(worlds.Other("received.name .. '|' .. received.message .. '|' .. received.code .. '|' .. received.detail.line .. '|' .. tostring(received.stack:find('bad input', 1, true) ~= nil)"), "TypeError|bad input|E_BAD|3|true");
        EQUALS(worlds.Other("tostring(getmetatable(received))"), "nil");
    });

    IT("refuses to bring a RegExp into Lua, and says where it is", {
        Worlds worlds;
        EQUALS(worlds.JsToLua("{ rules: [{ ok: 1 }, { match: /ab+c/gi }] }"), "Cannot copy value.rules[1].match: a RegExp cannot be copied to Lua");
        EQUALS(lua_gettop(worlds.other.State()), 0);
    });

    IT("brings the value types from JavaScript into Lua as themselves", {
        Worlds worlds;
        EQUALS(worlds.JsToLua("{ at: new Vector3(1, 2, 3), uv: new Vector2(0.5, -1), v4: new Vector4(1, 2, 3, 4), turn: new Quaternion(1, 0, 0, 0), tint: new Color(0.25, 0.5, 0.75, 1) }"), "");
        EQUALS(worlds.Other("tostring(received.at) .. ' ' .. tostring(received.uv) .. ' ' .. tostring(received.v4)"), "Vector3(1, 2, 3) Vector2(0.5, -1) Vector4(1, 2, 3, 4)");
        EQUALS(worlds.Other("tostring(received.turn) .. ' ' .. tostring(received.tint)"), "Quaternion(1, 0, 0, 0) Color(0.25, 0.5, 0.75, 1)");
        EQUALS(worlds.Other("tostring(received.at == Vector3(1, 2, 3))"), "true");
    });

    IT("takes scalars, tables and sequences from Lua into JavaScript", {
        Worlds worlds;
        EQUALS(worlds.LuaToJs("{ n = 1.5, i = 42, s = 'caf\\u{e9}', b = true, list = { 1, { 2, 'three', {} } }, nested = { deep = { ok = true } }, [3] = 'three' }"), "");
        EQUALS(worlds.Js("JSON.stringify(received)"), "{\"3\":\"three\",\"b\":true,\"i\":42,\"list\":[1,[2,\"three\",[]]],\"n\":1.5,\"nested\":{\"deep\":{\"ok\":true}},\"s\":\"caf\xC3\xA9\"}");
        EQUALS(worlds.Js("[Array.isArray(received.list), Array.isArray(received.list[1][2]), Array.isArray(received.nested), Object.getPrototypeOf(received) === Object.prototype].join()"), "true,true,false,true");
        EQUALS(worlds.LuaToJs("nil"), "");
        EQUALS(worlds.Js("String(received)"), "undefined");
        EQUALS(worlds.LuaToJs("{}"), "");
        EQUALS(worlds.Js("Array.isArray(received) && received.length === 0"), "true");
    });

    IT("takes a Lua integer beyond 2^53 into JavaScript as a bigint", {
        Worlds worlds;
        EQUALS(worlds.LuaToJs("{ math.maxinteger, math.mininteger, 2^53 | 0, 7 }"), "");
        EQUALS(worlds.Js("received.map((value) => typeof value + ':' + value).join()"), "bigint:9223372036854775807,bigint:-9223372036854775808,number:9007199254740992,number:7");
    });

    IT("takes the value types from Lua into JavaScript as themselves", {
        Worlds worlds;
        EQUALS(worlds.LuaToJs("{ at = Vector3(1, 2, 3), uv = Vector2(0.5, -1), v4 = Vector4(1, 2, 3, 4), turn = Quaternion(1, 0, 0, 0), tint = Color(0.25, 0.5, 0.75, 1) }"), "");
        EQUALS(worlds.Js("[received.at instanceof Vector3, received.uv instanceof Vector2, received.v4 instanceof Vector4, received.turn instanceof Quaternion, received.tint instanceof Color].join()"), "true,true,true,true,true");
        EQUALS(worlds.Js("[received.at, received.uv, received.v4, received.turn, received.tint].join(' ')"), "Vector3(1, 2, 3) Vector2(0.5, -1) Vector4(1, 2, 3, 4) Quaternion(1, 0, 0, 0) Color(0.25, 0.5, 0.75, 1)");
    });

    IT("builds a value type in a JavaScript runtime that had not seen its class yet, and says when a class is missing", {
        Worlds worlds;
        const Copied vector = Worlds::CopyLua(worlds.lua, "Vector3(1, 2, 3)");
        EQUALS(worlds.SpellInFreshJs(vector.GetValue()), "Vector3(1, 2, 3)");
        const Copied ticket = Worlds::CopyLua(worlds.lua, "TransferTicket(5)");
        EQUALS(worlds.SpellInFreshJs(ticket.GetValue()), "rebuild threw: Error: TransferTicket is not available in this JavaScript runtime");
    });

    // --- Host types ---

    IT("carries a handle declared with Class::Transfer between the three runtimes as its id", {
        Worlds worlds;
        const Copied copied = Worlds::CopyLua(worlds.lua, "TransferTicket(41)");
        EQUALS(copied.IsOk(), true);
        EQUALS(copied.GetValue().kind == Kind::HostObject, true);
        EQUALS(copied.GetValue().text, "TransferTicket");
        uint64_t id = 0;
        EQUALS(copied.GetValue().bytes.size(), sizeof(id));
        std::memcpy(&id, copied.GetValue().bytes.data(), sizeof(id));
        EQUALS(id, 41u);

        EQUALS(worlds.LuaToLua("{ ticket = TransferTicket(41) }"), "");
        EQUALS(worlds.Other("received.ticket.id"), "41");
        EQUALS(worlds.LuaToJs("{ ticket = TransferTicket(42) }"), "");
        EQUALS(worlds.Js("(received.ticket instanceof TransferTicket) + ' ' + received.ticket.id"), "true 42");
        EQUALS(worlds.JsToLua("{ ticket: new TransferTicket(43) }"), "");
        EQUALS(worlds.Other("received.ticket.id"), "43");
    });

    IT("carries an object of a derived class as the base that crosses", {
        Worlds worlds;
        EQUALS(worlds.LuaToLua("TransferVipTicket(7)"), "");
        EQUALS(worlds.Other("received.id"), "7");
        const Copied copied = Worlds::CopyLua(worlds.lua, "TransferVipTicket(7)");
        EQUALS(copied.GetValue().text, "TransferTicket");
    });

    IT("says where a handle whose object is gone could not be rebuilt", {
        Worlds worlds;
        TransferredValue ticket;
        ticket.kind  = Kind::HostObject;
        ticket.text  = "TransferTicket";
        ticket.bytes = std::vector<uint8_t>(sizeof(uint64_t), 0);
        TransferredValue holder;
        holder.kind = Kind::Object;
        holder.properties.emplace_back("owner", ticket);
        EQUALS(Worlds::StoreLua(worlds.other, holder), "Cannot copy value.owner: ticket 0 no longer exists");
        EQUALS(lua_gettop(worlds.other.State()), 0);
    });

    IT("refuses a host type Lua does not know, and one the state does not have", {
        Worlds worlds;
        TransferredValue unknown;
        unknown.kind = Kind::HostObject;
        unknown.text = "Player";
        TransferredValue list;
        list.kind = Kind::Array;
        list.items.push_back(unknown);
        EQUALS(Worlds::StoreLua(worlds.other, list), "Cannot copy value[0]: host type 'Player' is not available in Lua");

        // A state that never registered the value types.
        LuaRuntime bare(LuaRuntimeTest::Options());
        const Copied vector = Worlds::CopyLua(worlds.lua, "{ at = Vector3(1, 2, 3) }");
        EQUALS(Worlds::StoreLua(bare, vector.GetValue()), "Cannot copy value.at: Vector3 is not available in this Lua state");
        EQUALS(lua_gettop(bare.State()), 0);
    });

    IT("replaces a host type registered again under the same name", {
        Worlds worlds;
        LuaTransfer::RegisterHostType(
            "TransferProbe",
            [](lua_State *, int, std::vector<uint8_t> &) {
                return false;
            },
            [](lua_State *L, const std::vector<uint8_t> &) {
                lua_pushstring(L, "first");
                return true;
            });
        LuaTransfer::RegisterHostType(
            "TransferProbe",
            [](lua_State *, int, std::vector<uint8_t> &) {
                return false;
            },
            [](lua_State *L, const std::vector<uint8_t> &bytes) {
                if (bytes.empty()) {
                    return false;
                }
                lua_pushstring(L, "second");
                return true;
            });
        TransferredValue probe;
        probe.kind  = Kind::HostObject;
        probe.text  = "TransferProbe";
        probe.bytes = {1};
        EQUALS(Worlds::StoreLua(worlds.other, probe), "");
        EQUALS(worlds.Other("received"), "second");
        probe.bytes.clear();
        EQUALS(Worlds::StoreLua(worlds.other, probe), "Cannot copy value: host type 'TransferProbe' could not be rebuilt");
    });

    IT("fails the copy, with the path, when a host type raises a Lua error", {
        Worlds worlds;
        FaultyDefinition().RegisterLuaGlobal(worlds.lua.State(), Side::Server);
        LuaTransfer::RegisterHostType(
            "TransferFaulty",
            [](lua_State *L, int, std::vector<uint8_t> &) -> bool {
                return luaL_error(L, "the copy went wrong") != 0;
            },
            [](lua_State *L, const std::vector<uint8_t> &) -> bool {
                return luaL_error(L, "the rebuild went wrong") != 0;
            });
        EQUALS(worlds.Refusal("{ a = { TransferFaulty() } }"), "Cannot copy value.a[1]: the copy went wrong");
        EQUALS(lua_gettop(worlds.lua.State()), 0);

        TransferredValue faulty;
        faulty.kind = Kind::HostObject;
        faulty.text = "TransferFaulty";
        TransferredValue holder;
        holder.kind = Kind::Map;
        holder.items.resize(2);
        holder.items[0].kind = Kind::String;
        holder.items[0].text = "k";
        holder.items[1]      = faulty;
        EQUALS(Worlds::StoreLua(worlds.other, holder), "Cannot copy value.<value 0>: the rebuild went wrong");
        EQUALS(lua_gettop(worlds.other.State()), 0);
    });

    IT("declares a transfer once, and registers only what declared one", {
        const auto message = [](auto action) -> std::string {
            try {
                action();
            }
            catch (const std::logic_error &error) {
                return error.what();
            }
            return {};
        };
        EQUALS(message([] {
            PlainDefinition().RegisterTransfer();
        }),
            "Bind: 'TransferPlain' is registered to cross between runtimes without a Transfer");
        EQUALS(message([] {
            Bind::Class<Ticket> again = TicketDefinition();
            again.Transfer(&Ticket::GetId);
        }),
            "Bind: 'TransferTicket' declares twice how it crosses between runtimes");
    });

    // --- Refusals ---

    IT("refuses what cannot be copied out of Lua and says where", {
        Worlds worlds;
        EQUALS(worlds.Refusal("{ items = { 1, { name = function() end } } }"), "Cannot copy value.items[2].name: a function cannot be copied");
        EQUALS(worlds.Refusal("print"), "Cannot copy value: a function cannot be copied");
        EQUALS(worlds.Refusal("{ job = coroutine.create(function() end) }"), "Cannot copy value.job: a coroutine cannot be copied");
        EQUALS(worlds.Refusal("{ list = { TransferPlain() } }"), "Cannot copy value.list[1]: a TransferPlain cannot be copied");
        EQUALS(worlds.Refusal("(function() local loop = { inner = {} } loop.inner.back = loop return loop end)()"), "Cannot copy value.inner.back: it refers back to itself");
        EQUALS(worlds.Refusal("{ [true] = 1 }"), "Cannot copy value: it has a key that is neither a string nor an integer");
        EQUALS(worlds.Refusal("{ a = { [1.5] = 1 } }"), "Cannot copy value.a: it has a key that is neither a string nor an integer");
        EQUALS(worlds.Refusal("{ a = { [{}] = 1 } }"), "Cannot copy value.a: it has a key that is neither a string nor an integer");
        EQUALS(lua_gettop(worlds.lua.State()), 0);
    });

    IT("refuses a userdata that is not a bound object", {
        Worlds worlds;
        lua_State *L = worlds.lua.State();
        lua_newuserdatauv(L, 8, 0);
        EQUALS(LuaTransfer::Copy(L, -1).GetError(), "Cannot copy value: a userdata cannot be copied");
        lua_pop(L, 1);
        lua_pushlightuserdata(L, &worlds);
        EQUALS(LuaTransfer::Copy(L, 1).GetError(), "Cannot copy value: a userdata cannot be copied");
        lua_pop(L, 1);
    });

    IT("refuses a string or a key that is not valid UTF-8", {
        Worlds worlds;
        EQUALS(worlds.Refusal("{ name = 'caf\\xe9' }"), "Cannot copy value.name: string is not valid UTF-8");
        EQUALS(worlds.Refusal("'\\xff'"), "Cannot copy value: string is not valid UTF-8");
        EQUALS(worlds.Refusal("{ list = { { ['\\xc3'] = 1 } } }"), "Cannot copy value.list[1]: a key is not valid UTF-8");
        // Overlong forms, surrogates and what lies beyond Unicode are not UTF-8 either.
        EQUALS(worlds.Refusal("'\\xc0\\x80'"), "Cannot copy value: string is not valid UTF-8");
        EQUALS(worlds.Refusal("'\\xed\\xa0\\x80'"), "Cannot copy value: string is not valid UTF-8");
        EQUALS(worlds.Refusal("'\\xf4\\x90\\x80\\x80'"), "Cannot copy value: string is not valid UTF-8");
        EQUALS(worlds.Refusal("'\\xe2\\x82'"), "Cannot copy value: string is not valid UTF-8");
        EQUALS(worlds.Refusal("'\\0 \\u{7f} \\u{80} \\u{7ff} \\u{800} \\u{ffff} \\u{10000} \\u{10ffff}'"), "");
    });

    IT("copies 128 tables nested in one another and refuses one more", {
        Worlds worlds;
        EQUALS(worlds.LuaToLua(NestedLua(128)), "");
        const std::string refusal = worlds.Refusal(NestedLua(129));
        EQUALS(refusal.rfind("Cannot copy value[1][1]", 0) == 0, true);
        EQUALS(refusal.ends_with("[1]: it is nested too deeply"), true);

        EQUALS(Worlds::StoreLua(worlds.other, NestedArrays(128)), "");
        const std::string rebuilt = Worlds::StoreLua(worlds.other, NestedArrays(129));
        EQUALS(rebuilt.rfind("Cannot copy value[0][0]", 0) == 0, true);
        EQUALS(rebuilt.ends_with("[0]: it is nested too deeply"), true);
    });

    IT("refuses a value too large to copy instead of expanding it", {
        Worlds worlds;
        // 40 levels of a table holding the same child twice: a few kilobytes that would copy as 2^40 values.
        LuaRuntimeTest::Run(worlds.lua, "tree = { 1 } for _ = 1, 40 do tree = { tree, tree } end");
        const std::string doubled = worlds.Refusal("tree");
        EQUALS(doubled.rfind("Cannot copy value[", 0) == 0, true);
        EQUALS(doubled.ends_with("it is too large to copy"), true);

        // One byte more of text than a copy may hold.
        LuaRuntimeTest::Run(worlds.lua, "long = string.rep('a', (256 << 20) + 1)");
        EQUALS(worlds.Refusal("{ text = long }"), "Cannot copy value.text: it is too large to copy");
    });

    // --- Protection ---

    IT("fails to rebuild in a state that is out of memory without raising, and leaves the state usable", {
        Worlds worlds(4u << 20);
        TransferredValue big;
        big.kind = Kind::Array;
        TransferredValue text;
        text.kind = Kind::String;
        text.text = std::string(64 * 1024, 'x');
        big.items.assign(128, text);
        text.text = "small";

        const int top           = lua_gettop(worlds.other.State());
        const std::string error = Worlds::StoreLua(worlds.other, big);
        EQUALS(error.rfind("Cannot copy value[", 0) == 0, true);
        EQUALS(error.ends_with("not enough memory"), true);
        EQUALS(lua_gettop(worlds.other.State()), top);
        EQUALS(Worlds::StoreLua(worlds.other, text), "");
        EQUALS(worlds.Other("received"), "small");
    });

    IT("leaves the stack as it found it, but for the value a rebuild pushes", {
        Worlds worlds;
        lua_State *L = worlds.lua.State();
        lua_pushinteger(L, 1);
        lua_newtable(L);
        lua_pushinteger(L, 3);
        const Copied middle = LuaTransfer::Copy(L, 2);
        EQUALS(middle.IsOk() && middle.GetValue().kind == Kind::Array, true);
        const Copied relative = LuaTransfer::Copy(L, -3);
        EQUALS(relative.IsOk() && relative.GetValue().number == 1.0, true);
        EQUALS(lua_gettop(L), 3);

        std::string error;
        EQUALS(LuaTransfer::Rebuild(L, relative.GetValue(), error), true);
        EQUALS(lua_gettop(L), 4);
        EQUALS(lua_tointeger(L, -1), 1);
        lua_settop(L, 0);
    });

    IT("copies inside a coroutine as on the main thread", {
        Worlds worlds;
        lua_State *L      = worlds.lua.State();
        lua_State *thread = lua_newthread(L);
        lua_createtable(thread, 0, 1);
        lua_pushstring(thread, "v");
        lua_setfield(thread, -2, "k");
        const Copied copied = LuaTransfer::Copy(thread, 1);
        EQUALS(copied.IsOk() && PropertyNames(copied.GetValue()) == "k", true);
        std::string error;
        EQUALS(LuaTransfer::Rebuild(thread, copied.GetValue(), error), true);
        EQUALS(lua_gettop(thread), 2);
        lua_settop(L, 0);
    });

    // --- Through the dynamic interface ---

    IT("copies and rebuilds through Value::ToTransfer and Call::FromTransfer in both languages", {
        Worlds worlds;
        EQUALS(worlds.Lua("show(Codec.copy({ 1, 'two', { k = 2.5 }, Vector3(1, 2, 3) }))"), "{[1]=1,[2]=\"two\",[3]={k=2.5},[4]=Vector3(1, 2, 3)}");
        EQUALS(worlds.Js("JSON.stringify(Codec.copy([1, 'two', { k: 2.5 }, new Vector3(1, 2, 3)]))"), "[1,\"two\",{\"k\":2.5},{\"x\":1,\"y\":2,\"z\":3}]");
        EQUALS(worlds.Lua("(function() local t = { 1 } return Codec.copy(t) ~= t end)()"), "true");
        EQUALS(worlds.Js("(() => { const t = [1]; return Codec.copy(t) !== t; })()"), "true");
        EQUALS(worlds.Lua("tostring(Codec.copy(nil))"), "nil");
        EQUALS(worlds.Js("String(Codec.copy(undefined))"), "undefined");
        EQUALS(worlds.Js("Codec.copy(new Date(5)).getTime()"), "5");
    });

    IT("hands the function the refusal of ToTransfer, in both languages", {
        Worlds worlds;
        EQUALS(worlds.Lua("Codec.copy({ on = function() end })"), "<failed> Cannot copy value.on: a function cannot be copied");
        EQUALS(worlds.Js("Codec.copy({ on() {} })"), "<failed> Cannot copy value.on: a function cannot be copied here");
    });

    IT("throws to the script what FromTransfer cannot rebuild, once the function has returned", {
        Worlds worlds;
        const int regExp = static_cast<int>(Kind::RegExp);
        const int host   = static_cast<int>(Kind::HostObject);
        EQUALS(worlds.Lua("select(2, pcall(Codec.rebuild, " + std::to_string(regExp) + ", 'x'))"), "Cannot copy value: a RegExp cannot be copied to Lua");
        EQUALS(worlds.Lua("select(2, pcall(Codec.rebuild, " + std::to_string(host) + ", 'Nope'))"), "Cannot copy value: host type 'Nope' is not available in Lua");
        EQUALS(worlds.Js("(() => { try { Codec.rebuild(" + std::to_string(host) + ", 'Nope'); return 'no throw'; } catch (error) { return error.message; } })()"), "Unknown host type Nope");
        EQUALS(worlds.Js("Codec.rebuild(" + std::to_string(static_cast<int>(Kind::String)) + ", 'x')"), "went on");
    });
});
