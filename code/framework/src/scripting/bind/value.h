/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "types.h"

#include <utils/result.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Scripting {
    struct TransferredValue; // value_transfer.h
} // namespace Framework::Scripting

namespace Framework::Scripting::Bind {
    class ScriptCallable; // callable.h
    class Value;

    // What a script value is, in either language.
    enum class ValueKind : uint8_t {
        Nil, // JavaScript undefined and null, Lua nil
        Boolean,
        Number,
        String,
        Function,
        Array,  // a JavaScript array; a Lua table whose keys are exactly 1..n, with n at least 1
        Object, // any other JavaScript object or Lua table, the empty table included
        Bound,  // an instance of a bound class
        Other   // what the other language has nothing for: a symbol, a bigint, a coroutine, a userdata of no class
    };

    // The error of Value::ToJson for a JavaScript value JSON.stringify gives no text for, undefined instead: a
    // function, a symbol, an object whose toJSON gives one of them. It tells such a value, which an API may always
    // have taken for none, from one that failed to be written.
    inline constexpr const char *kNoJsonForm = "a value of this kind has no JSON form";

    namespace Detail {
        // What a Value forwards to: the values of one call, by slot. Slot 0 is nil. Implemented once per language,
        // by the Call of that language (lua_call.cpp, v8_call.cpp).
        //
        // Calls run one inside another on a thread, and a backend lives exactly as long as its call. Each has a
        // generation no other ever has, which is all a Value keeps of it: a Value never holds a pointer to a
        // backend, and is led to one only while its call is the innermost one running.
        class ValueBackend {
          public:
            ValueBackend(const ValueBackend &)            = delete;
            ValueBackend &operator=(const ValueBackend &) = delete;

            // The backend of the innermost call running on this thread, or nullptr outside any.
            static ValueBackend *Innermost();

            uint64_t Generation() const {
                return _generation;
            }

            // The kind of the value. Unless `exact`, a Lua table is an Object without being looked into, which is
            // all that telling it from a scalar, a function or a bound object needs.
            virtual ValueKind KindOf(size_t slot, bool exact) const = 0;
            // Whether the value is an Array, or an Object without a key.
            virtual bool IsListAt(size_t slot)                          = 0;
            virtual bool BooleanOf(size_t slot) const                   = 0;
            virtual double NumberOf(size_t slot) const                  = 0;
            virtual std::optional<int64_t> IntegerOf(size_t slot) const = 0;
            virtual std::string StringOf(size_t slot) const             = 0;
            virtual std::string DisplayStringOf(size_t slot)            = 0;
            // The object in the value at `slot` seen as the class of `definition`, or nullptr.
            virtual void *BoundOf(size_t slot, const LuaClass *definition) const         = 0;
            virtual Value GetKey(size_t slot, std::string_view key)                      = 0;
            // What a script reads as value.key: GetKey for an Object or an Array, the property of a bound object.
            virtual Value MemberOf(size_t slot, std::string_view key) = 0;
            // Whether the value is the null of JavaScript.
            virtual bool IsNullAt(size_t slot) const                                     = 0;
            virtual void SetKey(size_t slot, std::string_view key, const Value &value)   = 0;
            virtual std::vector<std::string> KeysOf(size_t slot)                         = 0;
            virtual size_t LengthOf(size_t slot) const                                   = 0;
            virtual Value ElementAt(size_t slot, size_t zeroBased)                       = 0;
            virtual void AppendTo(size_t slot, const Value &value)                       = 0;
            virtual Utils::Result<TransferredValue, std::string> TransferOf(size_t slot) = 0;
            virtual Utils::Result<std::string, std::string> JsonOf(size_t slot)          = 0;
            // The callable of the function at `slot`, or nullptr for what is not a function.
            virtual std::shared_ptr<ScriptCallable> CallableOf(size_t slot) = 0;

          protected:
            // Becomes the innermost call of the thread, until it is destroyed.
            ValueBackend();
            ~ValueBackend();

          private:
            uint64_t _generation;
            ValueBackend *_outer;
        };

        struct ValueAccess;
    } // namespace Detail

    // A handle to a script value: an argument, or what the Call or another Value made. A copy designates the same
    // value. It names neither language: the same code reads a JavaScript value and a Lua one.
    //
    // A handle is good in its own call, until that call returns. Anywhere else it is Nil, and reads, is stored and
    // is returned as the nil of the call it is used in: once its call has returned, as when a function made with
    // NewFunction kept one, and in a call nested in its own. Keeping one is a mistake, not a danger.
    //
    // Reading is strict. A value of another kind is not converted: AsString of a number is empty, AsNumber of a
    // string is 0. Reading or writing through a value that is not an object, or not an array, does nothing and gives
    // Nil, nothing or 0.
    //
    // Objects. Only an Object or an Array has keys: a bound object and a function have none in either language,
    // whatever a script could read on them. A Lua table is read and written raw: no metamethod of the script runs
    // inside the host function, so a proxy table shows what it holds itself. A table whose metatable is protected
    // (__metatable), as the table of a class or of a namespace is, is not written to. A JavaScript object is read
    // and written as a script would: a getter, a setter or a proxy runs, and what it throws is thrown by the call
    // once the function has returned.
    class Value final {
      public:
        // Nil.
        Value() = default;

        // Of a Lua table this looks at every key, to tell an Array from an Object.
        ValueKind Kind() const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->KindOf(target.slot, true) : ValueKind::Nil;
        }

        bool IsNil() const {
            return Scalar() == ValueKind::Nil;
        }
        // Whether the value is JavaScript's null and not its undefined, for an API that gives null a meaning a
        // value left out does not have (`attachTo: null` detaches). Both are Nil, and IsNil is true of both. Never
        // true in Lua, which has the one nil: a table cannot hold it, so where such an API takes null in an
        // object, a Lua script passes false, which the function accepts for it there.
        bool IsNull() const {
            const Target target = Find();
            return target.backend != nullptr && target.backend->IsNullAt(target.slot);
        }
        bool IsBoolean() const {
            return Scalar() == ValueKind::Boolean;
        }
        bool IsNumber() const {
            return Scalar() == ValueKind::Number;
        }
        bool IsString() const {
            return Scalar() == ValueKind::String;
        }
        bool IsFunction() const {
            return Scalar() == ValueKind::Function;
        }
        bool IsBound() const {
            return Scalar() == ValueKind::Bound;
        }

        // A JavaScript array, or a Lua table whose keys are exactly 1..n. Not the check for "a list was passed":
        // the empty table of Lua is an Object, as nothing tells it from one, and so is `{}` where JavaScript has
        // `[]`. IsList is that check.
        bool IsArray() const {
            return Kind() == ValueKind::Array;
        }
        bool IsObject() const {
            return Kind() == ValueKind::Object;
        }

        // Whether the value passes for a list: an Array, or an Object without a key, in both languages. Length
        // and At read any of them.
        bool IsList() const {
            const Target target = Find();
            return target.backend != nullptr && target.backend->IsListAt(target.slot);
        }

        // True for the boolean true and for nothing else.
        bool AsBoolean() const {
            const Target target = Find();
            return target.backend != nullptr && target.backend->BooleanOf(target.slot);
        }

        // 0 for what is not a number.
        double AsNumber() const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->NumberOf(target.slot) : 0.0;
        }

        // A number with an integral value that fits 64 bits. Empty for any other number and any other value.
        std::optional<int64_t> AsInteger() const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->IntegerOf(target.slot) : std::nullopt;
        }

        // The bytes of a string: UTF-8 from JavaScript, whatever the script put in it from Lua. Empty for what is
        // not a string.
        std::string AsString() const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->StringOf(target.slot) : std::string();
        }

        // Any value as String(value) or tostring(value) spells it, which may run the script's toString or
        // __tostring. Empty when that fails, and outside any call.
        std::string ToDisplayString() const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->DisplayStringOf(target.slot) : std::string();
        }

        // The object of the bound class T in the value, or the T part of an object of a class derived from it,
        // valid while the value is. nullptr for anything else.
        template <typename T>
        T *AsBound() const {
            static_assert(Detail::HasScriptClassName<T>, "Bind::Value::AsBound: T is a bound class, named with FW_SCRIPT_CLASS");
            const Target target = Find();
            return target.backend != nullptr ? static_cast<T *>(target.backend->BoundOf(target.slot, Detail::LuaClassOf<T>::definition)) : nullptr;
        }

        // The value under `key`, Nil when there is none.
        //
        // A key that is the decimal text of an integer ("10", not "010") names what Keys lists under it. In an
        // Array that is the element at that index from zero, in both languages. In any other Lua table it is the
        // string key if the table has it, and the integer key otherwise.
        Value Get(std::string_view key) const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->GetKey(target.slot, key) : Value();
        }

        // What a script reads as `value.key`, for a function that takes "anything with an id": a plain object or a
        // handle of any bound class. On an Object or an Array it is Get. On a bound object it is the property of
        // that name: in JavaScript whatever the object answers for it, as a script reading it gets, and what an
        // accessor throws is thrown by the call once the function has returned; in Lua what the getter of a
        // property the class declares for this runtime gives, a method not being one, and a getter that fails
        // makes the call raise its error when the function returns (HasThrown tells). Nil for anything else, a
        // function included.
        Value Member(std::string_view key) const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->MemberOf(target.slot, key) : Value();
        }

        // Nil removes the key in Lua and stores undefined in JavaScript. A key that is the decimal text of an
        // integer is written where Get reads it, so what Keys listed is replaced and not doubled; in a Lua table
        // that has neither key yet it becomes an integer key.
        //
        // In Lua, a function of another runtime (Call::FromTransfer) that is set in a table can from then on be
        // called as a method of that table, as one that arrived in a rebuilt object: `t:f(x)` is `t.f(x)`
        // (LuaNoteHolder, lua_callable.h).
        void Set(std::string_view key, Value value) {
            const Target target = Find();
            if (target.backend != nullptr) {
                target.backend->SetKey(target.slot, key, value);
            }
        }

        // The keys of an Object a script enumerates. JavaScript: its own enumerable string keys, in the order of
        // the object, which is integers ascending, then the others as they were added. Lua: integer keys
        // ascending, as decimal text, then string keys sorted by their bytes; keys of any other type are left out.
        // The keys of an Array are its indices from zero, "0" to "n-1", in both languages.
        std::vector<std::string> Keys() const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->KeysOf(target.slot) : std::vector<std::string>();
        }

        // Elements of a JavaScript array; the border of a Lua table, which is its element count when it is an
        // Array. 0 for anything else.
        size_t Length() const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->LengthOf(target.slot) : 0;
        }

        // Indices start at zero in both languages.
        Value At(size_t zeroBased) const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->ElementAt(target.slot, zeroBased) : Value();
        }

        // Adds an element at the end of a JavaScript array or of a Lua table, which NewArray gives empty. A Lua
        // table cannot hold nil: appending Nil leaves it as it was.
        void Append(Value value) {
            const Target target = Find();
            if (target.backend != nullptr) {
                target.backend->AppendTo(target.slot, value);
            }
        }

        // The function in the value as something the host keeps beyond this call and invokes later, whatever its
        // language (callable.h): an event handler, a key bind, an exported function. The same function gives the
        // same callable for as long as anyone holds it, so a handler can be found again to be removed. nullptr for
        // what is not a function, and so for a handle read outside its call.
        std::shared_ptr<ScriptCallable> ToCallable() const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->CallableOf(target.slot) : nullptr;
        }

        // A copy of the value that can be rebuilt in another runtime, of either language, with Call::FromTransfer:
        // what an event argument, an export value or a call result crosses as. ValueTransfer copies a JavaScript
        // value and LuaTransfer a Lua one; value_transfer.h, which a caller includes for the type, and
        // lua_transfer.h say what each kind of value becomes and what is refused. A function crosses as its
        // callable, and arrives as a function that runs it in the runtime it came from.
        //
        // The error says where in the value the copy gave up. Nothing is thrown to the script: the function
        // decides what a value that cannot cross means. Nil, and so a handle read outside its call, copies as
        // undefined.
        Utils::Result<TransferredValue, std::string> ToTransfer() const;

        // The value as JSON text: what JSON.stringify gives in JavaScript, where a toJSON is asked, and what
        // LuaJson::Encode writes in Lua (lua_json.h), which is the same text for the same data. Nil is null in both
        // languages.
        //
        // The error says why a value has no JSON form: a function, a value that refers back to itself, a Lua table
        // with a key JSON has no form for, or in JavaScript the message of what JSON.stringify or the toJSON of the
        // script threw. Nothing is thrown to the script, in either language: a function that puts up with a value
        // it cannot send goes on, and one that does not throws the error under its own name.
        Utils::Result<std::string, std::string> ToJson() const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->JsonOf(target.slot) : Utils::Result<std::string, std::string>::Ok("null");
        }

      private:
        friend struct Detail::ValueAccess;

        struct Target final {
            Detail::ValueBackend *backend;
            size_t slot;
        };

        Value(uint64_t generation, size_t slot): _generation(generation), _slot(slot) {}

        // Where to read the value: in the innermost call, at its slot when the handle is of that call, at the nil
        // slot otherwise. No backend outside any call.
        Target Find() const {
            Detail::ValueBackend *backend = Detail::ValueBackend::Innermost();
            return {backend, backend != nullptr && backend->Generation() == _generation ? _slot : 0};
        }

        ValueKind Scalar() const {
            const Target target = Find();
            return target.backend != nullptr ? target.backend->KindOf(target.slot, false) : ValueKind::Nil;
        }

        // 0 is the generation of no call.
        uint64_t _generation = 0;
        size_t _slot         = 0;
    };

    namespace Detail {
        // For a backend: makes the Value of one of its slots, and finds the slot of a Value it is handed back.
        struct ValueAccess final {
            static Value Make(const ValueBackend &backend, size_t slot) {
                return Value(backend.Generation(), slot);
            }

            // The slot of `value` in `backend`. A value of another call, or of none, is nil: slot 0.
            static size_t SlotIn(const ValueBackend &backend, const Value &value) {
                return value._generation == backend.Generation() ? value._slot : 0;
            }
        };
    } // namespace Detail
} // namespace Framework::Scripting::Bind
