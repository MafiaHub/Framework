/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "value_transfer.h"
#include "engine_helpers.h"

#include <v8pp/convert.hpp>

#include <cstring>

namespace Framework::Scripting {
    namespace {
        // Deep enough for any real payload, shallow enough that the copy cannot exhaust the native stack.
        constexpr size_t kMaxDepth = 128;

        struct HostType {
            std::string name;
            ValueTransfer::HostCopy copy;
            ValueTransfer::HostRebuild rebuild;
        };

        // Filled while the server starts, before any script runs, and only read afterwards.
        std::vector<HostType> &HostTypes() {
            static std::vector<HostType> types;
            return types;
        }

        const std::vector<HostType> kNoHostTypes;

        template <typename T>
        v8::Local<v8::ArrayBufferView> MakeView(v8::Local<v8::ArrayBuffer> buffer, size_t length) {
            return T::New(buffer, 0, length);
        }

        // Every view type that crosses, by name, with its element size. DataView counts in bytes.
        struct ViewType {
            const char *name;
            size_t elementSize;
            bool (v8::Value::*is)() const;
            v8::Local<v8::ArrayBufferView> (*make)(v8::Local<v8::ArrayBuffer>, size_t);
        };

        const ViewType kViewTypes[] = {
            {"Int8Array", 1, &v8::Value::IsInt8Array, &MakeView<v8::Int8Array>},
            {"Uint8Array", 1, &v8::Value::IsUint8Array, &MakeView<v8::Uint8Array>},
            {"Uint8ClampedArray", 1, &v8::Value::IsUint8ClampedArray, &MakeView<v8::Uint8ClampedArray>},
            {"Int16Array", 2, &v8::Value::IsInt16Array, &MakeView<v8::Int16Array>},
            {"Uint16Array", 2, &v8::Value::IsUint16Array, &MakeView<v8::Uint16Array>},
            {"Int32Array", 4, &v8::Value::IsInt32Array, &MakeView<v8::Int32Array>},
            {"Uint32Array", 4, &v8::Value::IsUint32Array, &MakeView<v8::Uint32Array>},
            {"Float32Array", 4, &v8::Value::IsFloat32Array, &MakeView<v8::Float32Array>},
            {"Float64Array", 8, &v8::Value::IsFloat64Array, &MakeView<v8::Float64Array>},
            {"BigInt64Array", 8, &v8::Value::IsBigInt64Array, &MakeView<v8::BigInt64Array>},
            {"BigUint64Array", 8, &v8::Value::IsBigUint64Array, &MakeView<v8::BigUint64Array>},
            {"DataView", 1, &v8::Value::IsDataView, &MakeView<v8::DataView>},
        };

        // Where the copy is inside the value, as a chain up to the root. Spelled out only when the copy fails, so a
        // successful copy builds no strings for it.
        struct Path {
            enum class Step : uint8_t { Root, Index, Property, MapKey, MapValue, Member };

            const Path *parent      = nullptr;
            Step step               = Step::Root;
            uint32_t index          = 0;
            const std::string *name = nullptr;

            Path At(Step next, uint32_t at) const {
                return {this, next, at, nullptr};
            }

            Path Property(const std::string &property) const {
                return {this, Step::Property, 0, &property};
            }

            std::string ToString() const {
                std::vector<const Path *> chain;
                for (const Path *step = this; step != nullptr; step = step->parent) {
                    chain.push_back(step);
                }
                std::string out;
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    const Path &step = **it;
                    switch (step.step) {
                    case Step::Root: out += "value"; break;
                    case Step::Index: out += "[" + std::to_string(step.index) + "]"; break;
                    case Step::Property: out += "." + *step.name; break;
                    case Step::MapKey: out += ".<key " + std::to_string(step.index) + ">"; break;
                    case Step::MapValue: out += ".<value " + std::to_string(step.index) + ">"; break;
                    case Step::Member: out += ".<member " + std::to_string(step.index) + ">"; break;
                    }
                }
                return out;
            }
        };

        class Copier final {
          public:
            Copier(v8::Isolate *isolate, v8::Local<v8::Context> context, const TransferFunctions &functions): _isolate(isolate), _context(context), _functions(functions) {}

            bool Copy(v8::Local<v8::Value> value, TransferredValue &out, const Path &where) {
                if (_valuesLeft == 0) {
                    return Fail(where, "it is too large to copy");
                }
                --_valuesLeft;

                if (value->IsUndefined()) {
                    out.kind = TransferredValue::Kind::Undefined;
                    return true;
                }
                if (value->IsNull()) {
                    out.kind = TransferredValue::Kind::Null;
                    return true;
                }
                if (value->IsBoolean()) {
                    out.kind    = TransferredValue::Kind::Boolean;
                    out.boolean = value->BooleanValue(_isolate);
                    return true;
                }
                if (value->IsNumber()) {
                    out.kind   = TransferredValue::Kind::Number;
                    out.number = value.As<v8::Number>()->Value();
                    return true;
                }
                if (value->IsBigInt()) {
                    return CopyBigInt(value.As<v8::BigInt>(), out, where);
                }
                if (value->IsString()) {
                    out.kind = TransferredValue::Kind::String;
                    out.text = ToUtf8(_isolate, value);
                    return Spend(where, out.text.size());
                }
                if (value->IsSymbol()) {
                    return Fail(where, "a symbol cannot be copied");
                }
                if (value->IsFunction()) {
                    if (!_functions.exportFunction) {
                        return Fail(where, "a function cannot be copied here");
                    }
                    out.kind = TransferredValue::Kind::Function;
                    _functions.exportFunction(_isolate, value.As<v8::Function>(), out);
                    return true;
                }

                v8::Local<v8::Object> object = value.As<v8::Object>();
                for (const auto &ancestor : _ancestors) {
                    if (ancestor->StrictEquals(object)) {
                        return Fail(where, "it refers back to itself");
                    }
                }
                if (_ancestors.size() >= kMaxDepth) {
                    return Fail(where, "it is nested too deeply");
                }

                _ancestors.push_back(object);
                const bool copied = CopyObject(object, out, where);
                _ancestors.pop_back();
                return copied;
            }

            const std::string &GetError() const {
                return _error;
            }

          private:
            bool CopyBigInt(v8::Local<v8::BigInt> value, TransferredValue &out, const Path &where) {
                int count = value->WordCount();
                if (!Spend(where, static_cast<size_t>(count) * sizeof(uint64_t))) {
                    return false;
                }
                std::vector<uint64_t> words(static_cast<size_t>(count));
                int negative = 0;
                value->ToWordsArray(&negative, &count, words.data());
                out.kind    = TransferredValue::Kind::BigInt;
                out.boolean = negative != 0;
                out.bytes.resize(static_cast<size_t>(count) * sizeof(uint64_t));
                if (!out.bytes.empty()) {
                    std::memcpy(out.bytes.data(), words.data(), out.bytes.size());
                }
                return true;
            }

            bool CopyObject(v8::Local<v8::Object> object, TransferredValue &out, const Path &where) {
                if (object->IsPromise()) {
                    return Fail(where, "a promise cannot be copied");
                }
                if (object->IsProxy() || object->IsWeakMap() || object->IsWeakSet() || object->IsWeakRef() || object->IsGeneratorObject() || object->IsModuleNamespaceObject()) {
                    return Fail(where, "this kind of object cannot be copied");
                }

                if (object->IsNativeError()) {
                    out.kind = TransferredValue::Kind::Error;
                    return CopyProperties(object, out, where, true);
                }
                if (object->IsArray()) {
                    // Read once: an element's getter can grow or shrink the array while it is being copied.
                    auto array            = object.As<v8::Array>();
                    const uint32_t length = array->Length();
                    if (length > _valuesLeft) {
                        return Fail(where, "it is too large to copy");
                    }
                    out.kind = TransferredValue::Kind::Array;
                    out.items.resize(length);
                    for (uint32_t i = 0; i < length; ++i) {
                        v8::Local<v8::Value> element;
                        if (!array->Get(_context, i).ToLocal(&element) || !Copy(element, out.items[i], where.At(Path::Step::Index, i))) {
                            return Fail(where, "an element could not be read");
                        }
                    }
                    return true;
                }
                if (object->IsDate()) {
                    out.kind   = TransferredValue::Kind::Date;
                    out.number = object.As<v8::Date>()->ValueOf();
                    return true;
                }
                if (object->IsRegExp()) {
                    auto regexp = object.As<v8::RegExp>();
                    out.kind    = TransferredValue::Kind::RegExp;
                    out.text    = ToUtf8(_isolate, regexp->GetSource());
                    out.number  = static_cast<double>(regexp->GetFlags());
                    return Spend(where, out.text.size());
                }
                if (object->IsMap() || object->IsSet()) {
                    const bool isMap          = object->IsMap();
                    v8::Local<v8::Array> flat = isMap ? object.As<v8::Map>()->AsArray() : object.As<v8::Set>()->AsArray();
                    const uint32_t length     = flat->Length();
                    if (length > _valuesLeft) {
                        return Fail(where, "it is too large to copy");
                    }
                    out.kind = isMap ? TransferredValue::Kind::Map : TransferredValue::Kind::Set;
                    out.items.resize(length);
                    for (uint32_t i = 0; i < length; ++i) {
                        v8::Local<v8::Value> entry;
                        const Path at = isMap ? where.At(i % 2 == 0 ? Path::Step::MapKey : Path::Step::MapValue, i / 2) : where.At(Path::Step::Member, i);
                        if (!flat->Get(_context, i).ToLocal(&entry) || !Copy(entry, out.items[i], at)) {
                            return Fail(where, "an entry could not be read");
                        }
                    }
                    return true;
                }
                if (object->IsSharedArrayBuffer() || (object->IsArrayBufferView() && object.As<v8::ArrayBufferView>()->Buffer()->IsSharedArrayBuffer())) {
                    // Its memory is freed through the allocator of the isolate that made it, which dies with that
                    // resource; a share that outlived it would crash the server when the last holder let go.
                    return Fail(where, "shared memory cannot cross between resources");
                }
                if (object->IsArrayBuffer()) {
                    auto buffer = object.As<v8::ArrayBuffer>();
                    if (!Spend(where, buffer->ByteLength())) {
                        return false;
                    }
                    out.kind = TransferredValue::Kind::ArrayBuffer;
                    out.bytes.resize(buffer->ByteLength());
                    if (!out.bytes.empty()) {
                        std::memcpy(out.bytes.data(), buffer->GetBackingStore()->Data(), out.bytes.size());
                    }
                    return true;
                }
                if (object->IsArrayBufferView()) {
                    auto view            = object.As<v8::ArrayBufferView>();
                    const ViewType *type = nullptr;
                    for (const auto &candidate : kViewTypes) {
                        if (((*view)->*candidate.is)()) {
                            type = &candidate;
                            break;
                        }
                    }
                    if (type == nullptr) {
                        return Fail(where, "this kind of view cannot be copied");
                    }
                    if (!Spend(where, view->ByteLength())) {
                        return false;
                    }
                    out.kind = TransferredValue::Kind::TypedArray;
                    // Node's Buffer is a Uint8Array subclass; keep it a Buffer on the other side.
                    out.text = ToUtf8(_isolate, object->GetConstructorName()) == "Buffer" ? "Buffer" : type->name;
                    out.bytes.resize(view->ByteLength());
                    if (!out.bytes.empty()) {
                        view->CopyContents(out.bytes.data(), out.bytes.size());
                    }
                    return true;
                }

                // Native-backed objects carry internal fields; a plain object has none and skips the lookups. Checked last:
                // array buffers and views carry them too.
                const auto &types = object->InternalFieldCount() > 0 ? HostTypes() : kNoHostTypes;
                for (auto it = types.rbegin(); it != types.rend(); ++it) {
                    if (it->copy(_isolate, object, out.bytes)) {
                        out.kind = TransferredValue::Kind::HostObject;
                        out.text = it->name;
                        return Spend(where, out.bytes.size());
                    }
                }

                out.kind = TransferredValue::Kind::Object;
                return CopyProperties(object, out, where, false);
            }

            // An error's name, message and stack are not enumerable, so they are read by name.
            bool CopyProperties(v8::Local<v8::Object> object, TransferredValue &out, const Path &where, bool isError) {
                if (isError) {
                    for (const char *key : {"name", "message", "stack"}) {
                        v8::Local<v8::Value> field;
                        if (object->Get(_context, v8pp::to_v8(_isolate, key)).ToLocal(&field) && field->IsString()) {
                            TransferredValue text;
                            text.kind = TransferredValue::Kind::String;
                            text.text = ToUtf8(_isolate, field);
                            if (!Spend(where, text.text.size())) {
                                return false;
                            }
                            out.properties.emplace_back(key, std::move(text));
                        }
                    }
                }

                v8::Local<v8::Array> keys;
                if (!object->GetOwnPropertyNames(_context, static_cast<v8::PropertyFilter>(v8::ONLY_ENUMERABLE | v8::SKIP_SYMBOLS), v8::KeyConversionMode::kConvertToString).ToLocal(&keys)) {
                    return Fail(where, "its properties could not be listed");
                }
                const uint32_t count = keys->Length();
                for (uint32_t i = 0; i < count; ++i) {
                    v8::Local<v8::Value> key;
                    v8::Local<v8::Value> field;
                    if (!keys->Get(_context, i).ToLocal(&key) || !object->Get(_context, key).ToLocal(&field)) {
                        return Fail(where, "a property could not be read");
                    }
                    std::string name = ToUtf8(_isolate, key);
                    if (isError && (name == "name" || name == "message" || name == "stack")) {
                        continue;
                    }
                    if (!Spend(where, name.size())) {
                        return false;
                    }
                    TransferredValue copied;
                    if (!Copy(field, copied, where.Property(name))) {
                        return false;
                    }
                    out.properties.emplace_back(std::move(name), std::move(copied));
                }
                return true;
            }

            // Counts `bytes` of strings or binary data against the copy's bound.
            bool Spend(const Path &where, size_t bytes) {
                if (bytes > _bytesLeft) {
                    return Fail(where, "it is too large to copy");
                }
                _bytesLeft -= bytes;
                return true;
            }

            // Keeps the innermost reason: an outer level only adds its own when nothing deeper said why.
            bool Fail(const Path &where, const char *why) {
                if (_error.empty()) {
                    _error = "Cannot copy " + where.ToString() + ": " + why;
                }
                return false;
            }

            v8::Isolate *_isolate;
            v8::Local<v8::Context> _context;
            const TransferFunctions &_functions;
            std::vector<v8::Local<v8::Object>> _ancestors;
            size_t _valuesLeft = ValueTransfer::kMaxValues;
            size_t _bytesLeft  = ValueTransfer::kMaxBytes;
            std::string _error;
        };

        class Builder final {
          public:
            Builder(v8::Isolate *isolate, v8::Local<v8::Context> context, const TransferFunctions &functions): _isolate(isolate), _context(context), _functions(functions) {}

            v8::MaybeLocal<v8::Value> Build(const TransferredValue &value) {
                switch (value.kind) {
                case TransferredValue::Kind::Undefined: return v8::Undefined(_isolate);
                case TransferredValue::Kind::Null: return v8::Null(_isolate);
                case TransferredValue::Kind::Boolean: return v8::Boolean::New(_isolate, value.boolean);
                case TransferredValue::Kind::Number: return v8::Number::New(_isolate, value.number);
                case TransferredValue::Kind::String: return v8pp::to_v8(_isolate, value.text);
                case TransferredValue::Kind::BigInt: return BuildBigInt(value);
                case TransferredValue::Kind::Date: return v8::Date::New(_context, value.number);
                case TransferredValue::Kind::RegExp: return v8::RegExp::New(_context, v8pp::to_v8(_isolate, value.text), static_cast<v8::RegExp::Flags>(static_cast<int>(value.number)));
                case TransferredValue::Kind::Array: return BuildArray(value);
                case TransferredValue::Kind::Object: return BuildProperties(v8::Object::New(_isolate), value, nullptr);
                case TransferredValue::Kind::Map: return BuildMap(value);
                case TransferredValue::Kind::Set: return BuildSet(value);
                case TransferredValue::Kind::ArrayBuffer: return BuildArrayBuffer(value.bytes);
                case TransferredValue::Kind::TypedArray: return BuildView(value);
                case TransferredValue::Kind::Error: return BuildError(value);
                case TransferredValue::Kind::HostObject: return BuildHostObject(value);
                case TransferredValue::Kind::Function: return BuildFunction(value.reference);
                }
                return Throw("Unknown value kind");
            }

          private:
            v8::MaybeLocal<v8::Value> Throw(const std::string &message) {
                _isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(_isolate, message)));
                return {};
            }

            v8::MaybeLocal<v8::Value> BuildBigInt(const TransferredValue &value) {
                std::vector<uint64_t> words(value.bytes.size() / sizeof(uint64_t));
                if (!words.empty()) {
                    std::memcpy(words.data(), value.bytes.data(), words.size() * sizeof(uint64_t));
                }
                v8::Local<v8::BigInt> built;
                if (!v8::BigInt::NewFromWords(_context, value.boolean ? 1 : 0, static_cast<int>(words.size()), words.data()).ToLocal(&built)) {
                    return {};
                }
                return built;
            }

            v8::MaybeLocal<v8::Value> BuildArray(const TransferredValue &value) {
                v8::Local<v8::Array> array = v8::Array::New(_isolate, static_cast<int>(value.items.size()));
                for (size_t i = 0; i < value.items.size(); ++i) {
                    v8::Local<v8::Value> element;
                    if (!Build(value.items[i]).ToLocal(&element) || array->Set(_context, static_cast<uint32_t>(i), element).IsNothing()) {
                        return {};
                    }
                }
                return array;
            }

            // Writes the copied properties onto `object`, leaving out the one named `skip`.
            v8::MaybeLocal<v8::Value> BuildProperties(v8::Local<v8::Object> object, const TransferredValue &value, const char *skip) {
                for (const auto &[name, field] : value.properties) {
                    if (skip != nullptr && name == skip) {
                        continue;
                    }
                    v8::Local<v8::Value> built;
                    if (!Build(field).ToLocal(&built) || object->Set(_context, v8pp::to_v8(_isolate, name), built).IsNothing()) {
                        return {};
                    }
                }
                return object;
            }

            v8::MaybeLocal<v8::Value> BuildMap(const TransferredValue &value) {
                v8::Local<v8::Map> map = v8::Map::New(_isolate);
                for (size_t i = 0; i + 1 < value.items.size(); i += 2) {
                    v8::Local<v8::Value> key;
                    v8::Local<v8::Value> entry;
                    if (!Build(value.items[i]).ToLocal(&key) || !Build(value.items[i + 1]).ToLocal(&entry) || map->Set(_context, key, entry).IsEmpty()) {
                        return {};
                    }
                }
                return map;
            }

            v8::MaybeLocal<v8::Value> BuildSet(const TransferredValue &value) {
                v8::Local<v8::Set> set = v8::Set::New(_isolate);
                for (const auto &item : value.items) {
                    v8::Local<v8::Value> member;
                    if (!Build(item).ToLocal(&member) || set->Add(_context, member).IsEmpty()) {
                        return {};
                    }
                }
                return set;
            }

            v8::Local<v8::ArrayBuffer> BuildArrayBuffer(const std::vector<uint8_t> &bytes) {
                std::shared_ptr<v8::BackingStore> store = v8::ArrayBuffer::NewBackingStore(_isolate, bytes.size());
                if (!bytes.empty()) {
                    std::memcpy(store->Data(), bytes.data(), bytes.size());
                }
                return v8::ArrayBuffer::New(_isolate, store);
            }

            v8::MaybeLocal<v8::Value> BuildView(const TransferredValue &value) {
                v8::Local<v8::ArrayBuffer> buffer = BuildArrayBuffer(value.bytes);
                const size_t size                 = value.bytes.size();

                if (value.text == "Buffer") {
                    // Buffer.from(arrayBuffer) shares the memory rather than copying it again.
                    v8::Local<v8::Value> bufferClass;
                    v8::Local<v8::Value> from;
                    if (_context->Global()->Get(_context, v8::String::NewFromUtf8Literal(_isolate, "Buffer")).ToLocal(&bufferClass) && bufferClass->IsObject() && bufferClass.As<v8::Object>()->Get(_context, v8::String::NewFromUtf8Literal(_isolate, "from")).ToLocal(&from) && from->IsFunction()) {
                        v8::Local<v8::Value> argument = buffer;
                        return from.As<v8::Function>()->Call(_context, bufferClass, 1, &argument);
                    }
                    return v8::Uint8Array::New(buffer, 0, size);
                }

                for (const auto &type : kViewTypes) {
                    if (value.text == type.name) {
                        return type.make(buffer, size / type.elementSize);
                    }
                }
                return Throw("Unknown view type " + value.text);
            }

            v8::MaybeLocal<v8::Value> BuildError(const TransferredValue &value) {
                std::string message;
                for (const auto &[name, field] : value.properties) {
                    if (name == "message") {
                        message = field.text;
                    }
                }
                // The message is the constructor's; the rest (name, stack, own fields) is written over the fresh error's.
                v8::Local<v8::Value> error = v8::Exception::Error(v8pp::to_v8(_isolate, message));
                return BuildProperties(error.As<v8::Object>(), value, "message");
            }

            v8::MaybeLocal<v8::Value> BuildHostObject(const TransferredValue &value) {
                for (const auto &type : HostTypes()) {
                    if (type.name == value.text) {
                        return type.rebuild(_isolate, _context, value.bytes);
                    }
                }
                return Throw("Unknown host type " + value.text);
            }

            v8::MaybeLocal<v8::Value> BuildFunction(uint64_t reference) {
                if (!_functions.importFunction) {
                    return Throw("A function cannot be rebuilt here");
                }
                v8::Local<v8::Function> function;
                if (!_functions.importFunction(_isolate, _context, reference).ToLocal(&function)) {
                    return {};
                }
                return function;
            }

            v8::Isolate *_isolate;
            v8::Local<v8::Context> _context;
            const TransferFunctions &_functions;
        };
    } // namespace

    void ValueTransfer::RegisterHostType(std::string name, HostCopy copy, HostRebuild rebuild) {
        auto &types = HostTypes();
        // Registering a name again replaces it, so a project can override how a framework type crosses.
        std::erase_if(types, [&](const HostType &type) { return type.name == name; });
        types.push_back({std::move(name), std::move(copy), std::move(rebuild)});
    }

    Utils::Result<TransferredValue, std::string> ValueTransfer::Copy(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value, const TransferFunctions &functions) {
        // Getters run during the copy; one that throws fails the copy rather than leaking the exception to the caller.
        v8::TryCatch tryCatch(isolate);
        Copier copier(isolate, context, functions);
        TransferredValue out;
        if (!copier.Copy(value, out, Path {})) {
            std::string error = copier.GetError();
            if (tryCatch.HasCaught() && !tryCatch.HasTerminated()) {
                error += " (" + ToUtf8(isolate, tryCatch.Exception()) + ")";
            }
            return Utils::Result<TransferredValue, std::string>::Err(error.empty() ? std::string("Cannot copy value") : error);
        }
        return Utils::Result<TransferredValue, std::string>::Ok(std::move(out));
    }

    v8::MaybeLocal<v8::Value> ValueTransfer::Rebuild(v8::Isolate *isolate, v8::Local<v8::Context> context, const TransferredValue &value, const TransferFunctions &functions) {
        Builder builder(isolate, context, functions);
        return builder.Build(value);
    }

} // namespace Framework::Scripting
