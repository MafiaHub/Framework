/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "call.h"
#include "class.h"
#include "descriptors.h"
#include "doc.h"
#include "lua_class.h"
#include "types.h"
#include "v8_backend.h"
#include "value.h"

#include <function2/function2.hpp>
#include <lua.h>
#include <v8.h>
#include <v8pp/function.hpp>
#include <v8pp/metadata.hpp>

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Framework::Scripting::Bind {
    // The definition of a global object of functions and constants, such as a service a script reaches as `Chat`:
    // declared once in C++, described for the catalog and materialised into each runtime. It is what Class is,
    // without instances.
    //
    // A Namespace is a handle: a copy is the same definition, and the definition itself is never destroyed, because
    // the catalog and every runtime it was materialised into keep pointing into it.
    //
    // A function is a context function, void(Bind::Call &), whose Doc gives the types, or a typed callable (a
    // function, a static member function or a captureless lambda), which the language converts the arguments for
    // as it does for a static of a class. Functions and constants share the names of the object: Function,
    // Constant and RuntimeConstant throw std::logic_error naming <Namespace>.<member> for a name the namespace
    // already declares.
    //
    //     void SendToAll(Bind::Call &call) {
    //         if (!call.Arg(0).IsString()) {
    //             call.ThrowTypeError("Chat.sendToAll: expected (text, opts?)");
    //             return;
    //         }
    //         const std::string author = call.Arg(1).Get("author").AsString();
    //         ...
    //     }
    //
    //     static const Bind::Namespace &Definition() {
    //         static const auto def = Bind::Namespace("Chat", "Delivery of chat messages.")
    //             .Function("sendToAll", &SendToAll, Bind::Doc("Broadcasts a message.")
    //                 .Param("text", Bind::TypeOf<std::string>(), "Message body.")
    //                 .OptionalParam("options", Bind::Type::Custom("{ author?: string }", "table"), "Who says it."),
    //                 Bind::Side::Server);
    //         return def;
    //     }
    class Namespace final {
      public:
        // What a constant holds.
        using ConstantValue = std::variant<bool, double, std::string>;

        // The values of the constants a definition leaves to the runtime (RuntimeConstant), by name.
        using RuntimeConstants = std::vector<std::pair<std::string, ConstantValue>>;

        // `side` is where the namespace exists at all: Shared in every runtime, with the members of each side;
        // Server or Client in the runtimes of that side only. Materialising it anywhere else does nothing, so a
        // module may register every definition it knows, in any order, and two definitions of one name and of
        // opposite sides never meet.
        Namespace(std::string name, std::string description, Side side = Side::Shared);

        Namespace &Function(std::string name, NativeFunction function, Doc doc, Side side = Side::Shared);

        template <typename F>
            requires(!Detail::ContextCallable<F>)
        Namespace &Function(std::string name, F function, Doc doc, Side side = Side::Shared) {
            static_assert(Detail::FreeCallable<F>, "Bind::Namespace::Function: F is a context function, taking Bind::Call &, or a function, a static member function or a lambda");
            using Signature   = typename Detail::SignatureOf<F>::Type;
            FunctionDesc desc = Detail::DescribeSignature(Signature {}, QualifiedName(name), doc, true, side);
            desc.name         = std::move(name);
            return AddFunction(std::move(desc), Detail::LuaFunctionThunkOf(Signature {}, function), V8Typed(Signature {}, function));
        }

        // A value a script reads and cannot replace: a boolean, a number or a string. A number that is integral
        // within 2^53 is a Lua integer.
        Namespace &Constant(std::string name, bool value, std::string description, Side side = Side::Shared);
        Namespace &Constant(std::string name, double value, std::string description, Side side = Side::Shared);
        Namespace &Constant(std::string name, std::string value, std::string description, Side side = Side::Shared);

        // Without these two an int would be ambiguous and a string literal a boolean.
        template <typename T>
            requires((std::is_arithmetic_v<T> && !std::same_as<T, bool> && !std::same_as<T, double>) || std::is_enum_v<T>)
        Namespace &Constant(std::string name, T value, std::string description, Side side = Side::Shared) {
            return Constant(std::move(name), static_cast<double>(value), std::move(description), side);
        }
        Namespace &Constant(std::string name, const char *value, std::string description, Side side = Side::Shared) {
            return Constant(std::move(name), std::string(value), std::move(description), side);
        }

        // A constant the definition only declares, of type T: bool, double or std::string. Its value is what is
        // known when a runtime is set up and not before, as the version of a mod is, so each materialisation is
        // given it. To a script it is a constant like any other.
        template <typename T>
            requires(std::same_as<T, bool> || std::same_as<T, double> || std::same_as<T, std::string>)
        Namespace &RuntimeConstant(std::string name, std::string description, Side side = Side::Shared) {
            return AddConstant(std::move(name), TypeOf<T>(), T {}, std::move(description), side, true);
        }

        // Described as a class that is a namespace: its functions are static, its constants read-only static
        // properties.
        const ClassDesc &Describe() const;

        // Sets target[name] to a new object carrying the members that exist in a runtime of side `runtime`, and
        // records it in `catalog` the way a builtin written by hand does: a global_object with its functions and,
        // for the constants, read-only properties. Returns the object, or an empty handle, with nothing set and
        // nothing recorded, in a runtime the namespace does not exist in. Every call makes a new one. Call with
        // `context` entered. `constants` has the value of every runtime constant of that side. Throws
        // std::logic_error, before anything is set, when a function returns what JavaScript cannot receive, and
        // when a runtime constant has no value or one of another type.
        v8::Local<v8::Object> MaterialiseV8(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, v8pp::metadata::registry &catalog, Side runtime, const RuntimeConstants &constants = {}) const;

        // Publishes the namespace as a global of the state under its name: a table a script reads the members of
        // and cannot write to, with those that exist in a runtime of side `runtime`. Does nothing in a runtime
        // the namespace does not exist in: the global of that name is left as it is. In a state that already has
        // it, whatever the side and whatever `constants`, it only puts it back under its name. The global is set
        // raw. `constants` is what it is for MaterialiseV8, and what is wrong with it throws std::logic_error before
        // anything is made in the state. Runs unprotected: a Lua error here panics unless the caller is in a
        // protected call.
        void MaterialiseLua(lua_State *L, Side runtime, const RuntimeConstants &constants = {}) const;

        // MaterialiseLua, under the name a Registrar calls for a class too.
        void RegisterLuaGlobal(lua_State *L, Side runtime, const RuntimeConstants &constants = {}) const {
            MaterialiseLua(L, runtime, constants);
        }

      private:
        struct Record;

        // Makes the JavaScript function of a member. Empty for a function JavaScript cannot have.
        using V8Maker = fu2::function<v8::MaybeLocal<v8::Function>(v8::Isolate *isolate, v8::Local<v8::Context> context) const>;

        // Through v8pp, which converts the arguments and words the errors, as for a static of a class.
        template <typename F, typename R, typename... Args>
        static V8Maker V8Typed(Detail::Signature<void, R, Args...>, F function) {
            if constexpr (!Detail::V8CanReturn<R>()) {
                return {};
            }
            else {
                return [function = Detail::V8Plain(std::move(function))](v8::Isolate *isolate, v8::Local<v8::Context>) -> v8::MaybeLocal<v8::Function> {
                    return v8pp::wrap_function(isolate, std::string_view(), Detail::V8Copy(function));
                };
            }
        }

        // The name a Doc that disagrees with the signature is reported under.
        std::string QualifiedName(const std::string &member) const;

        Namespace &AddFunction(FunctionDesc desc, Detail::LuaThunk lua, V8Maker v8);
        // `value` is the constant, or with `runtime` only what tells its type.
        Namespace &AddConstant(std::string name, TypeRef type, ConstantValue value, std::string description, Side side, bool runtime = false);

        std::shared_ptr<Record> _record;
    };
} // namespace Framework::Scripting::Bind
