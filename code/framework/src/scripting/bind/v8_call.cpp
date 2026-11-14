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
#include "v8_callable.h"
#include "value.h"

#include "../engine_helpers.h"
#include "../output_level.h"
#include "../resource/resource_manager.h"
#include "../scripting_catalog.h"
#include "../value_transfer.h"

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/function.hpp>

#include <cmath>
#include <exception>
#include <limits>
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

        // What a function made with a value runs (Call::NewFunction): its data is that value and its holder.
        void CallCarrying(const v8::FunctionCallbackInfo<v8::Value> &info);

        // The private keys of what such a function carries: no script can name them.
        v8::Local<v8::Private> CarrierHolderKey(v8::Isolate *isolate) {
            return v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.carrier.holder"));
        }

        v8::Local<v8::Private> CarrierValueKey(v8::Isolate *isolate) {
            return v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.carrier.value"));
        }

        class V8Call final
            : public Call
            , public Detail::ValueBackend {
          public:
            // `carried` is what the function was made with (NewFunction with a value), or empty.
            V8Call(const v8::FunctionCallbackInfo<v8::Value> &info, bool method, v8::Local<v8::Value> carried = {})
                : _info(info)
                , _isolate(info.GetIsolate())
                , _context(_isolate->GetCurrentContext())
                , _method(method)
                , _first(method ? 2 : 1)
                , _count(static_cast<size_t>(info.Length())) {
                _values.reserve(_count + 10);
                if (method) {
                    _values.push_back(info.This());
                }
                for (int i = 0; i < info.Length(); ++i) {
                    _values.push_back(info[i]);
                }
                if (!carried.IsEmpty()) {
                    _values.push_back(carried);
                    _data = _values.size();
                }
            }

            // Gives the script what the function returned, or the promise of what it deferred, or throws what it
            // threw.
            void Finish() {
                // A promise the script never sees must not be rejected behind its back.
                if (_deferred != nullptr && _thrown != Thrown::None) {
                    _deferred->Drop();
                }
                switch (_thrown) {
                case Thrown::None:
                    if (_deferred != nullptr) {
                        _deferred->Returned(_isolate, _context);
                        _info.GetReturnValue().Set(_promise);
                    }
                    else if (_returned) {
                        _info.GetReturnValue().Set(_result);
                    }
                    break;
                case Thrown::TypeError: _isolate->ThrowException(v8::Exception::TypeError(Message())); break;
                case Thrown::RangeError: _isolate->ThrowException(v8::Exception::RangeError(Message())); break;
                case Thrown::Error: _isolate->ThrowException(v8::Exception::Error(Message())); break;
                case Thrown::Caught: _isolate->ThrowException(_caught); break;
                case Thrown::Terminated: break;
                }
            }

            // For V8ValueOf and V8ValueIn.
            v8::Local<v8::Value> Handle(const Value &value) const {
                return Local(Slot(value));
            }

            Value Adopt(v8::Local<v8::Value> value) {
                return Keep(value);
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

            std::string OwnerOf(const Value &function) const override {
                const ResourceManager *manager = GetResourceManager();
                if (manager == nullptr) {
                    return {};
                }
                const v8::Local<v8::Value> value = Local(Slot(function));
                return manager->ResolveResourceContext(_isolate, value->IsFunction() ? value.As<v8::Function>() : v8::Local<v8::Function>());
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

            // The data of the function is an object no script ever reaches, with no prototype, that keeps what
            // holds the NativeFunction and the value under private keys: V8 keeps it alive with the function and
            // traces into it, and neither storing nor reading asks a prototype, an accessor or a proxy, which a
            // script could have put in the way of an ordinary property.
            Value NewFunction(NativeFunction function, Value data) override {
                if (!CanRun()) {
                    return At(0);
                }
                v8::Local<v8::Object> carrier = v8::Object::New(_isolate, v8::Null(_isolate), nullptr, nullptr, 0);
                v8::Local<v8::Function> made;
                if (!carrier->SetPrivate(_context, CarrierHolderKey(_isolate), v8pp::detail::external_data::set(_isolate, FunctionHolder(std::make_shared<NativeFunction>(std::move(function))))).FromMaybe(false)
                    || !carrier->SetPrivate(_context, CarrierValueKey(_isolate), Local(Slot(data))).FromMaybe(false) || !v8::Function::New(_context, &CallCarrying, carrier).ToLocal(&made)) {
                    return At(0);
                }
                return Keep(made);
            }

            Value Data() const override {
                return At(_data);
            }

            // What the rebuild throws, an unknown host type or what the constructor of a handle threw, is told to
            // the function and not thrown.
            Utils::Result<Value, std::string> FromTransfer(const TransferredValue &value) override {
                using Result = Utils::Result<Value, std::string>;
                if (!CanRun()) {
                    return Result::Err(kTerminating);
                }
                v8::TryCatch tryCatch(_isolate);
                v8::Local<v8::Value> rebuilt;
                if (!ValueTransfer::Rebuild(_isolate, _context, value).ToLocal(&rebuilt)) {
                    if (tryCatch.HasTerminated()) {
                        _thrown = Thrown::Terminated;
                        return Result::Err(kTerminating);
                    }
                    return Result::Err(Told(tryCatch.Exception(), "the value could not be rebuilt"));
                }
                return Result::Ok(Keep(rebuilt));
            }

            // So is the SyntaxError of JSON.parse.
            Utils::Result<Value, std::string> ParseJson(std::string_view text) override {
                using Result = Utils::Result<Value, std::string>;
                v8::Local<v8::String> source;
                if (!CanRun()) {
                    return Result::Err(kTerminating);
                }
                if (!NewString(text).ToLocal(&source)) {
                    return Result::Err("the text is too long for JavaScript");
                }
                v8::TryCatch tryCatch(_isolate);
                v8::Local<v8::Value> parsed;
                if (!v8::JSON::Parse(_context, source).ToLocal(&parsed)) {
                    if (tryCatch.HasTerminated()) {
                        _thrown = Thrown::Terminated;
                        return Result::Err(kTerminating);
                    }
                    return Result::Err(Told(tryCatch.Exception(), "the text is not JSON"));
                }
                return Result::Ok(Keep(parsed));
            }

            void Return(Value value) override {
                _result   = Local(Slot(value));
                _returned = true;
            }

            void ThrowTypeError(std::string message) override {
                Throw(Thrown::TypeError, std::move(message));
            }

            void ThrowRangeError(std::string message) override {
                Throw(Thrown::RangeError, std::move(message));
            }

            void ThrowError(std::string message) override {
                Throw(Thrown::Error, std::move(message));
            }

            bool HasThrown() const override {
                return _thrown != Thrown::None;
            }

            void WriteOutput(OutputLevel level, std::string_view line) override {
                const auto logger          = Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING);
                const std::string resource = ResourceName();
                if (!resource.empty()) {
                    logger->log(LogLevelOf(level), "[{}] {}", resource, line);
                }
                else {
                    logger->log(LogLevelOf(level), "{}", line);
                }
            }

            size_t OutputLineLimit() const override {
                return std::numeric_limits<size_t>::max();
            }

            // In an isolate that is being terminated there is no promise to make, and nobody to give one to.
            Deferred Defer() override {
                if (_deferred == nullptr && CanRun()) {
                    _deferred = Detail::V8Defer(_isolate, _context, _promise);
                }
                return Deferred(_deferred);
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

            std::string DisplayStringOf(size_t slot) override {
                return Spell(Local(slot));
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

            Value MemberOf(size_t slot, std::string_view key) override {
                const v8::Local<v8::Value> object = Local(slot);
                if (HasKeys(object)) {
                    return GetKey(slot, key);
                }
                v8::Local<v8::String> name;
                if (!IsBound(object) || !CanRun() || !NewString(key).ToLocal(&name)) {
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

            bool IsNullAt(size_t slot) const override {
                return Local(slot)->IsNull();
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

            std::shared_ptr<ScriptCallable> CallableOf(size_t slot) override {
                const v8::Local<v8::Value> value = Local(slot);
                return value->IsFunction() ? V8ToCallable(_isolate, _context, value.As<v8::Function>()) : nullptr;
            }

            // A getter that throws fails the copy, which keeps what it threw in its error. The script is not
            // thrown at. A function is copied as its callable.
            Utils::Result<TransferredValue, std::string> TransferOf(size_t slot) override {
                using Result = Utils::Result<TransferredValue, std::string>;
                if (!CanRun()) {
                    return Result::Err(kTerminating);
                }
                Result copied = ValueTransfer::Copy(_isolate, _context, Local(slot), V8CallableTransfer());
                if (!copied && _isolate->IsExecutionTerminating()) {
                    _thrown = Thrown::Terminated;
                }
                return copied;
            }

            Utils::Result<std::string, std::string> JsonOf(size_t slot, bool *omitted) override {
                using Result                     = Utils::Result<std::string, std::string>;
                const v8::Local<v8::Value> value = Local(slot);
                if (value->IsNullOrUndefined()) {
                    return Result::Ok("null");
                }
                if (!CanRun()) {
                    return Result::Err(kTerminating);
                }
                v8::TryCatch tryCatch(_isolate);
                v8::Local<v8::String> json;
                if (!v8::JSON::Stringify(_context, value).ToLocal(&json)) {
                    if (tryCatch.HasTerminated()) {
                        _thrown = Thrown::Terminated;
                        return Result::Err(kTerminating);
                    }
                    return Result::Err(Told(tryCatch.Exception(), kNoJsonForm));
                }
                // JSON.stringify gives undefined, which V8 spells here, for a function, a symbol and an object
                // whose toJSON gives one of them. A string would be in quotes.
                std::string text = ToUtf8(_isolate, json);
                if (text == "undefined") {
                    if (omitted != nullptr) {
                        *omitted = true;
                    }
                    return Result::Err(kNoJsonForm);
                }
                return Result::Ok(std::move(text));
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
                RangeError,
                Error,
                Caught,    // a script threw inside the function: _caught
                Terminated // the isolate is being terminated, and throws that itself
            };

            static constexpr const char *kTerminating = "the runtime is being terminated";
            static constexpr const char *kNoJsonForm  = "a value of this kind has no JSON form";

            Value At(size_t slot) const {
                return Detail::ValueAccess::Make(*this, slot);
            }

            // String(value). What a toString throws is not the function's to pass on: the value just has no
            // spelling. A termination is another matter, and v8::String::Utf8Value alone would swallow it.
            std::string Spell(v8::Local<v8::Value> value) {
                if (value.IsEmpty() || !CanRun()) {
                    return {};
                }
                v8::TryCatch tryCatch(_isolate);
                v8::Local<v8::String> spelled;
                if (!value->ToString(_context).ToLocal(&spelled)) {
                    if (tryCatch.HasTerminated()) {
                        _thrown = Thrown::Terminated;
                    }
                    return {};
                }
                return ToUtf8(_isolate, spelled);
            }

            // What was thrown, as the function is told of it and may put its own name before: the message of an
            // Error, the spelling of anything else, `fallback` for what has neither.
            std::string Told(v8::Local<v8::Value> exception, const char *fallback) {
                if (!exception.IsEmpty() && exception->IsNativeError() && CanRun()) {
                    v8::TryCatch tryCatch(_isolate);
                    v8::Local<v8::Value> message;
                    if (exception.As<v8::Object>()->Get(_context, v8::String::NewFromUtf8Literal(_isolate, "message")).ToLocal(&message)) {
                        if (message->IsString() && message.As<v8::String>()->Length() > 0) {
                            return ToUtf8(_isolate, message);
                        }
                    }
                    else if (tryCatch.HasTerminated()) {
                        _thrown = Thrown::Terminated;
                        return kTerminating;
                    }
                }
                std::string text = Spell(exception);
                return text.empty() ? std::string(fallback) : text;
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
            size_t _data = 0; // the slot of what the function carries, 0 without

            std::vector<v8::Local<v8::Value>> _values;
            v8::Local<v8::Value> _result;
            bool _returned = false;

            Thrown _thrown = Thrown::None;
            std::string _message;
            v8::Local<v8::Value> _caught;

            // What Defer made, if the function called it, and the promise the script gets for it.
            std::shared_ptr<Detail::V8Deferred> _deferred;
            v8::Local<v8::Promise> _promise;
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

        // What a function made with a value runs: its data is the carrier NewFunction made. What it reads there
        // is checked before it is taken for what was stored: a data that is not the carrier, or a carrier without
        // its holder, calls nothing.
        void CallCarrying(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope scope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            if (!info.Data()->IsObject()) {
                return;
            }
            v8::Local<v8::Object> carrier = info.Data().As<v8::Object>();
            v8::Local<v8::Value> holder;
            v8::Local<v8::Value> carried;
            if (!carrier->GetPrivate(context, CarrierHolderKey(isolate)).ToLocal(&holder) || !holder->IsExternal() || !carrier->GetPrivate(context, CarrierValueKey(isolate)).ToLocal(&carried)) {
                return;
            }
            const FunctionHolder function = v8pp::detail::external_data::get<FunctionHolder>(holder);
            V8Call call(info, false, carried);
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

    // Every JavaScript call is a V8Call.
    v8::Local<v8::Value> V8ValueOf(const Call &call, const Value &value) {
        return call.GetLanguage() == Language::JavaScript ? static_cast<const V8Call &>(call).Handle(value) : v8::Local<v8::Value>();
    }

    Value V8ValueIn(Call &call, v8::Local<v8::Value> value) {
        return call.GetLanguage() == Language::JavaScript ? static_cast<V8Call &>(call).Adopt(value) : call.Nil();
    }

    v8::Local<v8::Function> MakeV8Function(v8::Isolate *isolate, v8::Local<v8::Context> context, NativeFunction function) {
        return Detail::V8NativeFunction(isolate, context, std::make_shared<NativeFunction>(std::move(function))).ToLocalChecked();
    }

    void RecordV8DataType(v8pp::metadata::registry &catalog, const ClassDesc &desc) {
        auto &type = catalog.data_type(desc.name, desc.description);
        for (const PropertyDesc &property : desc.properties) {
            type.add_property(property.name, ToTypeScript(property.type), property.description, property.readonly);
        }
        for (const FunctionDesc &function : desc.functions) {
            type.record(v8pp::metadata::function_of<v8::FunctionCallback>(function.name, Detail::V8FunctionOptions(function)));
        }
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
