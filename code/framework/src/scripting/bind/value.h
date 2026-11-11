/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Scripting::Bind {
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

    namespace Detail {
        // What a Value forwards to: the values of one call, by slot. Slot 0 is nil. Implemented once per language,
        // by the Call of that language (lua_call.cpp, v8_call.cpp).
        class ValueBackend {
          public:
            virtual ValueKind KindOf(size_t slot) const                 = 0;
            virtual bool BooleanOf(size_t slot) const                   = 0;
            virtual double NumberOf(size_t slot) const                  = 0;
            virtual std::optional<int64_t> IntegerOf(size_t slot) const = 0;
            virtual std::string StringOf(size_t slot) const             = 0;
            virtual std::string DisplayStringOf(size_t slot)            = 0;
            // The object in the value at `slot` seen as the class of `definition`, or nullptr.
            virtual void *BoundOf(size_t slot, const LuaClass *definition) const       = 0;
            virtual Value GetKey(size_t slot, std::string_view key)                    = 0;
            virtual void SetKey(size_t slot, std::string_view key, const Value &value) = 0;
            virtual std::vector<std::string> KeysOf(size_t slot)                       = 0;
            virtual size_t LengthOf(size_t slot) const                                 = 0;
            virtual Value ElementAt(size_t slot, size_t zeroBased)                     = 0;
            virtual void AppendTo(size_t slot, const Value &value)                     = 0;

          protected:
            ~ValueBackend() = default;
        };

        struct ValueAccess;
    } // namespace Detail

    // A handle to a script value, good until the call it comes from returns: an argument, or what the Call or
    // another Value made. A copy designates the same value. It names neither language: the same code reads a
    // JavaScript value and a Lua one.
    //
    // Reading is strict. A value of another kind is not converted: AsString of a number is empty, AsNumber of a
    // string is 0. Reading or writing through a value that is not an object, or not an array, does nothing and gives
    // Nil, nothing or 0.
    //
    // Objects. A Lua table is read and written raw: no metamethod of the script runs inside the host function, so a
    // proxy table shows what it holds itself, and a bound object has no keys. A table whose metatable is protected
    // (__metatable), as the table of a class or of a namespace is, is not written to. A JavaScript object is read
    // and written as a script would: a getter, a setter or a proxy runs, and what it throws is thrown by the call
    // once the function has returned.
    class Value final {
      public:
        // Nil, and of no call.
        Value() = default;

        ValueKind Kind() const {
            return _backend != nullptr ? _backend->KindOf(_slot) : ValueKind::Nil;
        }

        bool IsNil() const {
            return Kind() == ValueKind::Nil;
        }
        bool IsBoolean() const {
            return Kind() == ValueKind::Boolean;
        }
        bool IsNumber() const {
            return Kind() == ValueKind::Number;
        }
        bool IsString() const {
            return Kind() == ValueKind::String;
        }
        bool IsFunction() const {
            return Kind() == ValueKind::Function;
        }
        bool IsArray() const {
            return Kind() == ValueKind::Array;
        }
        bool IsObject() const {
            return Kind() == ValueKind::Object;
        }
        bool IsBound() const {
            return Kind() == ValueKind::Bound;
        }

        // True for the boolean true and for nothing else.
        bool AsBoolean() const {
            return _backend != nullptr && _backend->BooleanOf(_slot);
        }

        // 0 for what is not a number.
        double AsNumber() const {
            return _backend != nullptr ? _backend->NumberOf(_slot) : 0.0;
        }

        // A number with an integral value that fits 64 bits. Empty for any other number and any other value.
        std::optional<int64_t> AsInteger() const {
            return _backend != nullptr ? _backend->IntegerOf(_slot) : std::nullopt;
        }

        // The bytes of a string: UTF-8 from JavaScript, whatever the script put in it from Lua. Empty for what is
        // not a string.
        std::string AsString() const {
            return _backend != nullptr ? _backend->StringOf(_slot) : std::string();
        }

        // Any value as String(value) or tostring(value) spells it, which may run the script's toString or
        // __tostring. Empty when that fails.
        std::string ToDisplayString() const {
            return _backend != nullptr ? _backend->DisplayStringOf(_slot) : std::string();
        }

        // The object of the bound class T in the value, or the T part of an object of a class derived from it,
        // valid while the value is. nullptr for anything else.
        template <typename T>
        T *AsBound() const {
            static_assert(Detail::HasScriptClassName<T>, "Bind::Value::AsBound: T is a bound class, named with FW_SCRIPT_CLASS");
            return _backend != nullptr ? static_cast<T *>(_backend->BoundOf(_slot, Detail::LuaClassOf<T>::definition)) : nullptr;
        }

        // The value under `key`, Nil when there is none. A Lua integer key is found under its decimal text.
        Value Get(std::string_view key) const;

        // Nil removes the key in Lua and stores undefined in JavaScript.
        void Set(std::string_view key, Value value);

        // The keys of an object a script enumerates: own enumerable string keys in JavaScript, in the order of the
        // object; string keys and integer keys, these as decimal text, in Lua, in no order. Those of an array are
        // its indices as the language counts them.
        std::vector<std::string> Keys() const {
            return _backend != nullptr ? _backend->KeysOf(_slot) : std::vector<std::string>();
        }

        // Elements of a JavaScript array; the border of a Lua table, which is its element count when it is an
        // Array. 0 for anything else.
        size_t Length() const {
            return _backend != nullptr ? _backend->LengthOf(_slot) : 0;
        }

        // Indices start at zero in both languages.
        Value At(size_t zeroBased) const;

        // Adds an element at the end of a JavaScript array or of a Lua table, which NewArray gives empty. A Lua
        // table cannot hold nil: appending Nil leaves it as it was.
        void Append(Value value);

      private:
        friend struct Detail::ValueAccess;

        Value(Detail::ValueBackend *backend, size_t slot): _backend(backend), _slot(slot) {}

        Detail::ValueBackend *_backend = nullptr;
        size_t _slot                   = 0;
    };

    namespace Detail {
        // For a backend: makes the Value of one of its slots, and finds the slot of a Value it is handed back.
        struct ValueAccess final {
            static Value Make(ValueBackend *backend, size_t slot) {
                return Value(backend, slot);
            }

            // The slot of `value` in `backend`. A value of another call, or of none, is nil: slot 0.
            static size_t SlotIn(const ValueBackend *backend, const Value &value) {
                return value._backend == backend ? value._slot : 0;
            }
        };
    } // namespace Detail

    inline Value Value::Get(std::string_view key) const {
        return _backend != nullptr ? _backend->GetKey(_slot, key) : Value();
    }

    inline void Value::Set(std::string_view key, Value value) {
        if (_backend != nullptr) {
            _backend->SetKey(_slot, key, value);
        }
    }

    inline Value Value::At(size_t zeroBased) const {
        return _backend != nullptr ? _backend->ElementAt(_slot, zeroBased) : Value();
    }

    inline void Value::Append(Value value) {
        if (_backend != nullptr) {
            _backend->AppendTo(_slot, value);
        }
    }
} // namespace Framework::Scripting::Bind
