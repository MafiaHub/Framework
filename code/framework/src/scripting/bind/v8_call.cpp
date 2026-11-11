/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "call.h"
#include "lua_class.h"
#include "v8_backend.h"
#include "value.h"

#include "../engine_helpers.h"
#include "../resource/resource_manager.h"
#include "../scripting_catalog.h"

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/function.hpp>

#include <cmath>
#include <exception>
#include <memory>
#include <string>
#include <vector>

// The JavaScript side of Call and Value.
//
// A value is a slot of a vector the call keeps: the receiver of a method, the arguments, then what the function
// makes or reads out of an object. The handles live in the scope of the callback, so they are good until the
// function returns. Slot 0 is undefined, and so is any slot the call does not have.
//
// An object is read and written as a script would, so a getter, a setter or a proxy may run and throw. What it
// throws is caught where it happens and kept as the error of the call, unless the function threw before: the
// function goes on with Nil, and the script gets the exception once the function has returned, as it gets what
// ThrowTypeError and ThrowError were given. An isolate that is being terminated lets nothing more run.

namespace Framework::Scripting::Bind {
    namespace {
        using FunctionHolder = std::shared_ptr<NativeFunction>;

        // 2^63, the first double past the largest int64_t.
        constexpr double kInt64Limit = 9223372036854775808.0;

        class V8Call final
            : public Call
            , public Detail::ValueBackend {
          public:
            V8Call(const v8::FunctionCallbackInfo<v8::Value> &info, bool method): _info(info), _isolate(info.GetIsolate()), _context(_isolate->GetCurrentContext()), _method(method), _first(method ? 2 : 1), _count(static_cast<size_t>(info.Length())) {
                _values.reserve(_count + 9);
                if (method) {
                    _values.push_back(info.This());
                }
                for (int i = 0; i < info.Length(); ++i) {
                    _values.push_back(info[i]);
                }
            }

            // Gives the script what the function returned, or throws what it threw.
            void Finish() {
                switch (_thrown) {
                case Thrown::None:
                    if (_returned) {
                        _info.GetReturnValue().Set(_result);
                    }
                    break;
                case Thrown::TypeError: _isolate->ThrowException(v8::Exception::TypeError(Message())); break;
                case Thrown::Error: _isolate->ThrowException(v8::Exception::Error(Message())); break;
                case Thrown::Caught: _isolate->ThrowException(_caught); break;
                case Thrown::Terminated: break;
                }
            }

            // --- Call ---

            Language GetLanguage() const override {
                return Language::JavaScript;
            }

            Side GetSide() const override {
                return ScriptingSide(_isolate);
            }

            std::string ResourceName() const override {
                const ResourceManager *manager = GetResourceManager();
                return manager != nullptr ? manager->ResolveResourceContext(_isolate) : std::string();
            }

            ResourceManager *GetResourceManager() const override {
                return Detail::CallResourceManager(_isolate);
            }

            size_t ArgCount() const override {
                return _count;
            }

            Value Arg(size_t index) const override {
                return At(index < _count ? _first + index : 0);
            }

            Value This() const override {
                return At(_method ? 1 : 0);
            }

            Value Nil() override {
                return At(0);
            }

            Value Null() override {
                return Keep(v8::Null(_isolate));
            }

            Value Boolean(bool value) override {
                return Keep(v8::Boolean::New(_isolate, value));
            }

            Value Number(double value) override {
                return Keep(v8::Number::New(_isolate, value));
            }

            Value Integer(int64_t value) override {
                return Keep(v8::Number::New(_isolate, static_cast<double>(value)));
            }

            Value String(std::string_view value) override {
                v8::Local<v8::String> string;
                if (!NewString(value).ToLocal(&string)) {
                    ThrowError("string is too long for JavaScript");
                    return At(0);
                }
                return Keep(string);
            }

            Value NewObject() override {
                return Keep(v8::Object::New(_isolate));
            }

            Value NewArray() override {
                return Keep(v8::Array::New(_isolate));
            }

            Value NewFunction(NativeFunction function) override {
                v8::Local<v8::Function> made;
                if (!CanRun() || !Detail::V8NativeFunction(_isolate, _context, std::make_shared<NativeFunction>(std::move(function))).ToLocal(&made)) {
                    return At(0);
                }
                return Keep(made);
            }

            void Return(Value value) override {
                _result   = Local(Slot(value));
                _returned = true;
            }

            void ThrowTypeError(std::string message) override {
                Throw(Thrown::TypeError, std::move(message));
            }

            void ThrowError(std::string message) override {
                Throw(Thrown::Error, std::move(message));
            }

            bool HasThrown() const override {
                return _thrown != Thrown::None;
            }

            // --- ValueBackend ---

            ValueKind KindOf(size_t slot, bool) const override {
                const v8::Local<v8::Value> value = Local(slot);
                if (value->IsNullOrUndefined()) {
                    return ValueKind::Nil;
                }
                if (value->IsBoolean()) {
                    return ValueKind::Boolean;
                }
                if (value->IsNumber()) {
                    return ValueKind::Number;
                }
                if (value->IsString()) {
                    return ValueKind::String;
                }
                if (value->IsFunction()) {
                    return ValueKind::Function;
                }
                if (value->IsArray()) {
                    return ValueKind::Array;
                }
                if (value->IsObject()) {
                    return IsBound(value) ? ValueKind::Bound : ValueKind::Object;
                }
                return ValueKind::Other;
            }

            bool IsListAt(size_t slot) override {
                const v8::Local<v8::Value> value = Local(slot);
                if (value->IsArray()) {
                    return true;
                }
                v8::Local<v8::Array> names;
                return HasKeys(value) && OwnKeys(value.As<v8::Object>()).ToLocal(&names) && names->Length() == 0;
            }

            bool BooleanOf(size_t slot) const override {
                return Local(slot)->IsTrue();
            }

            double NumberOf(size_t slot) const override {
                const v8::Local<v8::Value> value = Local(slot);
                return value->IsNumber() ? value.As<v8::Number>()->Value() : 0.0;
            }

            std::optional<int64_t> IntegerOf(size_t slot) const override {
                const v8::Local<v8::Value> value = Local(slot);
                if (!value->IsNumber()) {
                    return std::nullopt;
                }
                // A NaN compares false and an infinity is out of range.
                const double number = value.As<v8::Number>()->Value();
                if (!(number >= -kInt64Limit && number < kInt64Limit) || std::trunc(number) != number) {
                    return std::nullopt;
                }
                return static_cast<int64_t>(number);
            }

            std::string StringOf(size_t slot) const override {
                const v8::Local<v8::Value> value = Local(slot);
                return value->IsString() ? ToUtf8(_isolate, value) : std::string();
            }

            // What a toString throws is not the function's to pass on: the value just has no spelling. A
            // termination is another matter, and v8::String::Utf8Value alone would swallow it.
            std::string DisplayStringOf(size_t slot) override {
                if (!CanRun()) {
                    return {};
                }
                v8::TryCatch tryCatch(_isolate);
                v8::Local<v8::String> spelled;
                if (!Local(slot)->ToString(_context).ToLocal(&spelled)) {
                    if (tryCatch.HasTerminated()) {
                        _thrown = Thrown::Terminated;
                    }
                    return {};
                }
                return ToUtf8(_isolate, spelled);
            }

            void *BoundOf(size_t slot, const Detail::LuaClass *definition) const override {
                return Unwrap(Local(slot), definition);
            }

            Value GetKey(size_t slot, std::string_view key) override {
                const v8::Local<v8::Value> object = Local(slot);
                v8::Local<v8::String> name;
                if (!HasKeys(object) || !CanRun() || !NewString(key).ToLocal(&name)) {
                    return At(0);
                }
                v8::TryCatch tryCatch(_isolate);
                v8::Local<v8::Value> value;
                if (!object.As<v8::Object>()->Get(_context, name).ToLocal(&value)) {
                    Caught(tryCatch);
                    return At(0);
                }
                return Keep(value);
            }

            void SetKey(size_t slot, std::string_view key, const Value &value) override {
                const v8::Local<v8::Value> object = Local(slot);
                v8::Local<v8::String> name;
                if (!HasKeys(object) || !CanRun() || !NewString(key).ToLocal(&name)) {
                    return;
                }
                v8::TryCatch tryCatch(_isolate);
                if (object.As<v8::Object>()->Set(_context, name, Local(Slot(value))).IsNothing()) {
                    Caught(tryCatch);
                }
            }

            std::vector<std::string> KeysOf(size_t slot) override {
                std::vector<std::string> keys;
                const v8::Local<v8::Value> object = Local(slot);
                v8::Local<v8::Array> names;
                if (!HasKeys(object) || !OwnKeys(object.As<v8::Object>()).ToLocal(&names)) {
                    return keys;
                }
                keys.reserve(names->Length());
                for (uint32_t i = 0; i < names->Length(); ++i) {
                    // An array of strings V8 has just made: reading it runs no script.
                    v8::Local<v8::Value> name;
                    if (!names->Get(_context, i).ToLocal(&name)) {
                        break;
                    }
                    keys.push_back(ToUtf8(_isolate, name));
                }
                return keys;
            }

            size_t LengthOf(size_t slot) const override {
                const v8::Local<v8::Value> array = Local(slot);
                return array->IsArray() ? array.As<v8::Array>()->Length() : 0;
            }

            Value ElementAt(size_t slot, size_t zeroBased) override {
                const v8::Local<v8::Value> array = Local(slot);
                if (!array->IsArray() || !CanRun() || zeroBased >= array.As<v8::Array>()->Length()) {
                    return At(0);
                }
                v8::TryCatch tryCatch(_isolate);
                v8::Local<v8::Value> element;
                if (!array.As<v8::Array>()->Get(_context, static_cast<uint32_t>(zeroBased)).ToLocal(&element)) {
                    Caught(tryCatch);
                    return At(0);
                }
                return Keep(element);
            }

            void AppendTo(size_t slot, const Value &value) override {
                const v8::Local<v8::Value> array = Local(slot);
                if (!array->IsArray() || !CanRun()) {
                    return;
                }
                v8::TryCatch tryCatch(_isolate);
                if (array.As<v8::Array>()->Set(_context, array.As<v8::Array>()->Length(), Local(Slot(value))).IsNothing()) {
                    Caught(tryCatch);
                }
            }

          protected:
            void *SelfAs(const Detail::LuaClass *definition) const override {
                return _method ? Unwrap(_info.This(), definition) : nullptr;
            }

            Value WrapBound(const Detail::LuaClass *definition, const char *name, Detail::BoundBuilder build, void *source) override {
                // Asked before the object is built, which may move out of `source`.
                if (definition == nullptr || definition->v8 == nullptr || !definition->v8->has(_isolate)) {
                    ThrowError(std::string(name) + " is not available in this JavaScript runtime");
                    return At(0);
                }
                if (!CanRun()) {
                    return At(0);
                }
                const v8::Local<v8::Value> wrapped = definition->v8->adopt(_isolate, build(nullptr, source));
                if (wrapped.IsEmpty()) {
                    ThrowError(std::string(name) + " could not be given to JavaScript");
                    return At(0);
                }
                return Keep(wrapped);
            }

          private:
            enum class Thrown : uint8_t {
                None,
                TypeError,
                Error,
                Caught,    // a script threw inside the function: _caught
                Terminated // the isolate is being terminated, and throws that itself
            };

            Value At(size_t slot) const {
                return Detail::ValueAccess::Make(*this, slot);
            }

            // undefined needs no slot.
            Value Keep(v8::Local<v8::Value> value) {
                if (value.IsEmpty() || value->IsUndefined()) {
                    return At(0);
                }
                _values.push_back(value);
                return At(_values.size());
            }

            v8::Local<v8::Value> Local(size_t slot) const {
                return slot != 0 && slot <= _values.size() ? _values[slot - 1] : v8::Undefined(_isolate).As<v8::Value>();
            }

            size_t Slot(const Value &value) const {
                return Detail::ValueAccess::SlotIn(*this, value);
            }

            // Whether `value` has keys for the function: an Object or an Array. A function and a bound object have
            // none, as in Lua, whatever a script reads on them.
            bool HasKeys(v8::Local<v8::Value> value) const {
                return value->IsObject() && !value->IsFunction() && !IsBound(value);
            }

            // The own enumerable string keys of `object`, integers first as V8 orders them. Empty when the trap
            // of a proxy threw, which the call keeps.
            v8::MaybeLocal<v8::Array> OwnKeys(v8::Local<v8::Object> object) {
                if (!CanRun()) {
                    return {};
                }
                v8::TryCatch tryCatch(_isolate);
                v8::MaybeLocal<v8::Array> names = object->GetOwnPropertyNames(_context, static_cast<v8::PropertyFilter>(v8::ONLY_ENUMERABLE | v8::SKIP_SYMBOLS), v8::KeyConversionMode::kConvertToString);
                if (names.IsEmpty()) {
                    Caught(tryCatch);
                }
                return names;
            }

            v8::MaybeLocal<v8::String> NewString(std::string_view text) const {
                if (text.size() > static_cast<size_t>(v8::String::kMaxLength)) {
                    return {};
                }
                return v8::String::NewFromUtf8(_isolate, text.data(), v8::NewStringType::kNormal, static_cast<int>(text.size()));
            }

            v8::Local<v8::String> Message() const {
                return NewString(_message).FromMaybe(v8::String::Empty(_isolate));
            }

            // Whether script may still run: not once the isolate is being terminated.
            bool CanRun() const {
                return _thrown != Thrown::Terminated;
            }

            void Throw(Thrown kind, std::string message) {
                if (_thrown == Thrown::None) {
                    _thrown  = kind;
                    _message = std::move(message);
                }
            }

            // Takes over what `tryCatch` caught, so that it is thrown when the function returns and not before.
            void Caught(v8::TryCatch &tryCatch) {
                if (!tryCatch.HasCaught()) {
                    return;
                }
                if (tryCatch.HasTerminated()) {
                    _thrown = Thrown::Terminated;
                    return;
                }
                if (_thrown == Thrown::None) {
                    _thrown = Thrown::Caught;
                    _caught = v8::Local<v8::Value>::New(_isolate, tryCatch.Exception());
                }
                tryCatch.Reset();
            }

            // Whether `value` wraps an object of a class bound in the isolate, itself or, as v8pp looks for one,
            // through its prototypes.
            bool IsBound(v8::Local<v8::Value> value) const {
                while (value->IsObject()) {
                    const v8::Local<v8::Object> object = value.As<v8::Object>();
                    if (object->InternalFieldCount() == 2 && v8pp::detail::classes::is_wrapper(_isolate, object)) {
                        return true;
                    }
#if V8_MAJOR_VERSION > 12 || (V8_MAJOR_VERSION == 12 && V8_MINOR_VERSION >= 9)
                    value = object->GetPrototypeV2();
#else
                    value = object->GetPrototype();
#endif
                }
                return false;
            }

            void *Unwrap(v8::Local<v8::Value> value, const Detail::LuaClass *definition) const {
                return definition != nullptr && definition->v8 != nullptr ? definition->v8->unwrap(_isolate, value) : nullptr;
            }

            const v8::FunctionCallbackInfo<v8::Value> &_info;
            v8::Isolate *_isolate;
            v8::Local<v8::Context> _context;
            bool _method;
            size_t _first; // the slot of the first argument
            size_t _count;

            std::vector<v8::Local<v8::Value>> _values;
            v8::Local<v8::Value> _result;
            bool _returned = false;

            Thrown _thrown = Thrown::None;
            std::string _message;
            v8::Local<v8::Value> _caught;
        };

        // The script is thrown at from here, outside the catch block and once the function has returned.
        void Invoke(const v8::FunctionCallbackInfo<v8::Value> &info, bool method) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope scope(isolate);
            // A copy: the script may drop the function while it runs.
            const FunctionHolder function = v8pp::detail::external_data::get<FunctionHolder>(info.Data());
            V8Call call(info, method);
            try {
                (*function)(call);
            }
            catch (const std::exception &exception) {
                call.ThrowError(exception.what());
            }
            call.Finish();
        }

        void CallFunction(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Invoke(info, false);
        }

        void CallMethod(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Invoke(info, true);
        }
    } // namespace

    v8::Local<v8::Function> MakeV8Function(v8::Isolate *isolate, v8::Local<v8::Context> context, NativeFunction function) {
        return Detail::V8NativeFunction(isolate, context, std::make_shared<NativeFunction>(std::move(function))).ToLocalChecked();
    }

    namespace Detail {
        // v8pp owns the holder of a function: it frees it when the function is collected, and v8pp::cleanup frees
        // what an isolate still holds when it goes.
        v8::MaybeLocal<v8::Function> V8NativeFunction(v8::Isolate *isolate, v8::Local<v8::Context> context, std::shared_ptr<NativeFunction> function) {
            return v8::Function::New(context, &CallFunction, v8pp::detail::external_data::set(isolate, FunctionHolder(std::move(function))));
        }

        v8::Local<v8::FunctionTemplate> V8NativeTemplate(v8::Isolate *isolate, std::shared_ptr<NativeFunction> function, bool method) {
            return v8::FunctionTemplate::New(isolate, method ? &CallMethod : &CallFunction, v8pp::detail::external_data::set(isolate, FunctionHolder(std::move(function))));
        }
    } // namespace Detail
} // namespace Framework::Scripting::Bind
