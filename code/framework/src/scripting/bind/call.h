/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "callable.h"
#include "doc.h"
#include "types.h"
#include "value.h"

#include "../script_engine.h"

#include <function2/function2.hpp>

#include <utils/result.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Framework::Scripting {
    class ResourceManager;
    struct TransferredValue; // value_transfer.h
} // namespace Framework::Scripting

namespace Framework::Scripting::Bind {
    class Call;

    // A context function: the implementation of a function a script calls, written once for both languages. It
    // reads its arguments from the Call and answers through it. A std::exception it throws is thrown to the script
    // as an Error carrying what().
    using NativeFunction = fu2::function<void(Call &)>;

    namespace Detail {
        // What a builder is given as a context function, as opposed to a typed callable.
        template <typename F>
        concept ContextCallable = std::invocable<F &, Call &>;

        // Builds the object a script receives from the one at `source`: in `block` when there is one, with new
        // otherwise. Returns the object.
        using BoundBuilder = void *(*)(void *block, void *source);
    } // namespace Detail

    // What a context function receives: the arguments of one call from a script, the means to make values and to
    // answer, and who is calling. It names neither language, and neither does a builtin written against it.
    //
    // A Call and the Values it gives are good until the function returns. The Call is not kept beyond it; a Value
    // that is reads as Nil from then on (value.h).
    //
    // Errors. ThrowTypeError and ThrowError do not leave the function: it runs on to its return, and the script
    // gets the error then, a TypeError or an Error in JavaScript and in Lua a raised string, which Lua prefixes
    // with the position of the calling script. The first throw wins and what the function returned is dropped;
    // HasThrown lets it return early. Which failure is which is scripting/builtins/README.md's to say, and the
    // wording, "<Api>.<method>: <what went wrong>", is the function's: nothing is added to it.
    class Call {
      public:
        Call(const Call &)            = delete;
        Call &operator=(const Call &) = delete;

        virtual Language GetLanguage() const = 0;
        virtual Side GetSide() const         = 0;

        // The resource the calling script belongs to, or empty when it cannot be told.
        virtual std::string ResourceName() const = 0;

        // The resource manager of the runtime the script runs in (SetCallResourceManager), or nullptr.
        virtual ResourceManager *GetResourceManager() const = 0;

        // The receiver of a method is not an argument, in either language.
        virtual size_t ArgCount() const = 0;

        // Nil beyond ArgCount.
        virtual Value Arg(size_t index) const = 0;

        // In a method of a class, what it was called on, whatever that is: what a method returns for chaining.
        // Nil in a function that is not a method.
        virtual Value This() const = 0;

        // In a method of a class, the receiver as a T: the object the method was called on, when it is a T or of a
        // class derived from T. nullptr for any other receiver, for none, and in a function that is not a method;
        // a method then returns without effect, as for a receiver whose object is gone.
        template <typename T>
        T *Self() const {
            static_assert(Detail::HasScriptClassName<T>, "Bind::Call::Self: T is a bound class, named with FW_SCRIPT_CLASS");
            return static_cast<T *>(SelfAs(Detail::LuaClassOf<T>::definition));
        }

        // undefined in JavaScript.
        virtual Value Nil() = 0;
        // null in JavaScript, for an API that answers null; nil in Lua, which has the one. Its kind is Nil.
        virtual Value Null()         = 0;
        virtual Value Boolean(bool)  = 0;
        virtual Value Number(double) = 0;
        // A Lua integer; a JavaScript number, exact within 2^53.
        virtual Value Integer(int64_t)         = 0;
        virtual Value String(std::string_view) = 0;
        virtual Value NewObject()              = 0;
        // In Lua a table, which is an Array once it has an element.
        virtual Value NewArray() = 0;

        // A script function that calls `function`, with a Call of its own. It keeps `function` for as long as the
        // script keeps it, beyond this call.
        virtual Value NewFunction(NativeFunction function) = 0;

        // The value a copy made with Value::ToTransfer holds, built in this runtime: how a value of another
        // runtime, of either language, arrives. Throws to the script, and gives Nil, for what the language cannot
        // receive: a RegExp or a host type it does not have in Lua, a host type the runtime lacks in JavaScript.
        //
        // A function in it arrives as a function that invokes the original in the runtime that owns it, with
        // copies of its arguments, and gives a copy of its result: the value itself when the original finished at
        // once, and otherwise what Defer gives, a promise in JavaScript and a wait in Lua. In the runtime that owns
        // it, it arrives as the original itself.
        virtual Value FromTransfer(const TransferredValue &value) = 0;

        // What JSON text holds, as JSON.parse reads it in JavaScript and LuaJson::Decode in Lua (lua_json.h), where
        // null is nil. The error says what is wrong with text that is not JSON; nothing is thrown to the script,
        // since a payload that does not parse is rarely the fault of the script reading it.
        virtual Utils::Result<Value, std::string> ParseJson(std::string_view text) = 0;

        // A new instance of the bound class of `bound`, holding a copy of it, or what is moved out of an rvalue.
        // Throws to the script, and gives Nil, in a runtime that does not have the class.
        template <typename T>
        Value Wrap(T &&bound) {
            using Class = std::remove_cvref_t<T>;
            static_assert(Detail::HasScriptClassName<Class>, "Bind::Call::Wrap: the object is of a bound class, named with FW_SCRIPT_CLASS");
            static_assert(std::is_constructible_v<Class, T &&>, "Bind::Call::Wrap: the object can be copied, or is an rvalue that can be moved");
            const Detail::BoundBuilder build = [](void *block, void *source) -> void * {
                auto &object = *static_cast<std::remove_reference_t<T> *>(source);
                return block != nullptr ? ::new (block) Class(std::forward<T>(object)) : new Class(std::forward<T>(object));
            };
            return WrapBound(Detail::LuaClassOf<Class>::definition, ScriptClassName<Class>::value, build, const_cast<void *>(static_cast<const void *>(std::addressof(bound))));
        }

        // What the script gets back. Without it the function returns nothing: undefined in JavaScript, no value in
        // Lua. Called again, it replaces the result.
        virtual void Return(Value value) = 0;

        void Return(bool value) {
            Return(Boolean(value));
        }
        void Return(double value) {
            Return(Number(value));
        }
        void Return(int64_t value) {
            Return(Integer(value));
        }
        void Return(std::string_view value) {
            Return(String(value));
        }
        // Without these two an int would be ambiguous and a string literal a boolean.
        template <std::integral T>
            requires(!std::same_as<T, bool> && !std::same_as<T, int64_t>)
        void Return(T value) {
            Return(Integer(static_cast<int64_t>(value)));
        }
        void Return(const char *value) {
            Return(String(value));
        }
        void Return(std::nullptr_t) = delete;

        virtual void ThrowTypeError(std::string message) = 0;
        virtual void ThrowError(std::string message)     = 0;
        virtual bool HasThrown() const                   = 0;

        // Makes the result of the call one that is settled later, through the Deferred this gives (callable.h),
        // which is the same one each time. What the function returns no longer counts. In JavaScript the script
        // gets a promise. In Lua the coroutine that called waits once the function has returned, unless the
        // deferred was settled by then: the call then returns or raises at once. Where a Lua script cannot wait, on
        // the main thread or in a coroutine it made itself, a call that is still unsettled when the function
        // returns raises "cannot wait here" instead, and its deferred does nothing.
        //
        // What the function threw is thrown all the same, and the deferred then does nothing. In a Lua state no
        // LuaRuntime made there is nothing to wait with: do not call it there.
        virtual Deferred Defer() = 0;

      protected:
        Call()  = default;
        ~Call() = default;

        // The receiver as the class of `definition`, or nullptr.
        virtual void *SelfAs(const Detail::LuaClass *definition) const = 0;

        // A new instance of the class of `definition`, which `name` names in an error, built by `build` from
        // `source`.
        virtual Value WrapBound(const Detail::LuaClass *definition, const char *name, Detail::BoundBuilder build, void *source) = 0;
    };

    // Tells the calls made in a runtime which resource manager they answer GetResourceManager with, and a
    // JavaScript call which one names its resource. `runtime` is the v8::Isolate * or the LuaRuntime *; a null
    // manager forgets the runtime, which whoever registered it does before the runtime or the manager goes.
    // A runtime nobody registered answers with the manager of the scripting module in CoreModules, if there is one.
    void SetCallResourceManager(const void *runtime, ResourceManager *manager);

    namespace Detail {
        // The manager of `runtime`, as SetCallResourceManager describes it.
        ResourceManager *CallResourceManager(const void *runtime);

        // What a function that stands for `callable` in another runtime runs: it invokes it with copies of its
        // arguments and answers with a copy of the result, at once when the callable finished at once and through
        // Call::Defer otherwise. One body for both languages.
        NativeFunction CallableForwarder(std::shared_ptr<ScriptCallable> callable);
    } // namespace Detail
} // namespace Framework::Scripting::Bind
