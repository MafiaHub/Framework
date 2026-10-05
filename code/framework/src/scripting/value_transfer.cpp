/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "value_transfer.h"

#include <cstring>

namespace Framework::Scripting {
    namespace {
        // Deep enough for any real payload, shallow enough that the copy cannot exhaust the native stack.
        constexpr int kMaxDepth = 128;

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

        std::string ToUtf8(v8::Isolate *isolate, v8::Local<v8::Value> value) {
            v8::String::Utf8Value text(isolate, value);
            return *text ? std::string(*text, text.length()) : std::string();
        }

        v8::Local<v8::String> FromUtf8(v8::Isolate *isolate, const std::string &text) {
            return v8::String::NewFromUtf8(isolate, text.data(), v8::NewStringType::kNormal, static_cast<int>(text.size())).ToLocalChecked();
        }

        // Typed array constructors by name, with their element size. DataView counts in bytes.
        struct ViewType {
            const char *name;
            size_t elementSize;
        };

        constexpr ViewType kViewTypes[] = {
            {"Int8Array", 1},
            {"Uint8Array", 1},
            {"Uint8ClampedArray", 1},
            {"Int16Array", 2},
            {"Uint16Array", 2},
            {"Int32Array", 4},
            {"Uint32Array", 4},
            {"Float32Array", 4},
            {"Float64Array", 8},
            {"BigInt64Array", 8},
            {"BigUint64Array", 8},
            {"DataView", 1},
        };

        const char *ViewTypeName(v8::Local<v8::ArrayBufferView> view) {
            if (view->IsInt8Array()) {
                return "Int8Array";
            }
            if (view->IsUint8Array()) {
                return "Uint8Array";
            }
            if (view->IsUint8ClampedArray()) {
                return "Uint8ClampedArray";
            }
            if (view->IsInt16Array()) {
                return "Int16Array";
            }
            if (view->IsUint16Array()) {
                return "Uint16Array";
            }
            if (view->IsInt32Array()) {
                return "Int32Array";
            }
            if (view->IsUint32Array()) {
                return "Uint32Array";
            }
            if (view->IsFloat32Array()) {
                return "Float32Array";
            }
            if (view->IsFloat64Array()) {
                return "Float64Array";
            }
            if (view->IsBigInt64Array()) {
                return "BigInt64Array";
            }
            if (view->IsBigUint64Array()) {
                return "BigUint64Array";
            }
            if (view->IsDataView()) {
                return "DataView";
            }
            return nullptr;
        }

        class Copier final {
          public:
            Copier(v8::Isolate *isolate, v8::Local<v8::Context> context, const TransferFunctions &functions): _isolate(isolate), _context(context), _functions(functions) {}

            bool Copy(v8::Local<v8::Value> value, TransferredValue &out, const std::string &where) {
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
                    out.kind = TransferredValue::Kind::BigInt;
                    out.text = ToUtf8(_isolate, value);
                    return true;
                }
                if (value->IsString()) {
                    out.kind = TransferredValue::Kind::String;
                    out.text = ToUtf8(_isolate, value);
                    return true;
                }
                if (value->IsSymbol()) {
                    return Fail(where, "a symbol cannot be copied");
                }
                if (value->IsFunction()) {
                    if (!_functions.exportFunction) {
                        return Fail(where, "a function cannot be copied here");
                    }
                    out.kind      = TransferredValue::Kind::Function;
                    out.reference = _functions.exportFunction(_isolate, value.As<v8::Function>());
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
            bool CopyObject(v8::Local<v8::Object> object, TransferredValue &out, const std::string &where) {
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
                    auto array = object.As<v8::Array>();
                    out.kind   = TransferredValue::Kind::Array;
                    out.items.resize(array->Length());
                    for (uint32_t i = 0; i < array->Length(); ++i) {
                        v8::Local<v8::Value> element;
                        if (!array->Get(_context, i).ToLocal(&element) || !Copy(element, out.items[i], where + "[" + std::to_string(i) + "]")) {
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
                    return true;
                }
                if (object->IsMap() || object->IsSet()) {
                    const bool isMap        = object->IsMap();
                    v8::Local<v8::Array> flat = isMap ? object.As<v8::Map>()->AsArray() : object.As<v8::Set>()->AsArray();
                    out.kind                = isMap ? TransferredValue::Kind::Map : TransferredValue::Kind::Set;
                    out.items.resize(flat->Length());
                    for (uint32_t i = 0; i < flat->Length(); ++i) {
                        v8::Local<v8::Value> entry;
                        const std::string at = where + (isMap ? (i % 2 == 0 ? ".<key " : ".<value ") : ".<member ") + std::to_string(isMap ? i / 2 : i) + ">";
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
                    out.kind    = TransferredValue::Kind::ArrayBuffer;
                    out.bytes.resize(buffer->ByteLength());
                    if (!out.bytes.empty()) {
                        std::memcpy(out.bytes.data(), buffer->GetBackingStore()->Data(), out.bytes.size());
                    }
                    return true;
                }
                if (object->IsArrayBufferView()) {
                    auto view            = object.As<v8::ArrayBufferView>();
                    const char *typeName = ViewTypeName(view);
                    if (typeName == nullptr) {
                        return Fail(where, "this kind of view cannot be copied");
                    }
                    out.kind = TransferredValue::Kind::TypedArray;
                    // Node's Buffer is a Uint8Array subclass; keep it a Buffer on the other side.
                    out.text = ToUtf8(_isolate, object->GetConstructorName()) == "Buffer" ? "Buffer" : typeName;
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
                        return true;
                    }
                }

                out.kind = TransferredValue::Kind::Object;
                return CopyProperties(object, out, where, false);
            }

            // An error's name, message and stack are not enumerable, so they are read by name.
            bool CopyProperties(v8::Local<v8::Object> object, TransferredValue &out, const std::string &where, bool isError) {
                if (isError) {
                    for (const char *key : {"name", "message", "stack"}) {
                        v8::Local<v8::Value> field;
                        if (object->Get(_context, v8::String::NewFromUtf8(_isolate, key).ToLocalChecked()).ToLocal(&field) && field->IsString()) {
                            TransferredValue text;
                            text.kind = TransferredValue::Kind::String;
                            text.text = ToUtf8(_isolate, field);
                            out.properties.emplace_back(key, std::move(text));
                        }
                    }
                }

                v8::Local<v8::Array> keys;
                if (!object->GetOwnPropertyNames(_context, static_cast<v8::PropertyFilter>(v8::ONLY_ENUMERABLE | v8::SKIP_SYMBOLS), v8::KeyConversionMode::kConvertToString).ToLocal(&keys)) {
                    return Fail(where, "its properties could not be listed");
                }
                for (uint32_t i = 0; i < keys->Length(); ++i) {
                    v8::Local<v8::Value> key;
                    v8::Local<v8::Value> field;
                    if (!keys->Get(_context, i).ToLocal(&key) || !object->Get(_context, key).ToLocal(&field)) {
                        return Fail(where, "a property could not be read");
                    }
                    std::string name = ToUtf8(_isolate, key);
                    if (isError && (name == "name" || name == "message" || name == "stack")) {
                        continue;
                    }
                    TransferredValue copied;
                    if (!Copy(field, copied, where + "." + name)) {
                        return false;
                    }
                    out.properties.emplace_back(std::move(name), std::move(copied));
                }
                return true;
            }

            // Keeps the innermost reason: an outer level only adds its own when nothing deeper said why.
            bool Fail(const std::string &where, const char *why) {
                if (_error.empty()) {
                    _error = "Cannot copy " + where + ": " + why;
                }
                return false;
            }

            v8::Isolate *_isolate;
            v8::Local<v8::Context> _context;
            const TransferFunctions &_functions;
            std::vector<v8::Local<v8::Object>> _ancestors;
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
                case TransferredValue::Kind::String: return FromUtf8(_isolate, value.text);
                case TransferredValue::Kind::BigInt: return BuildBigInt(value.text);
                case TransferredValue::Kind::Date: return v8::Date::New(_context, value.number);
                case TransferredValue::Kind::RegExp: return v8::RegExp::New(_context, FromUtf8(_isolate, value.text), static_cast<v8::RegExp::Flags>(static_cast<int>(value.number)));
                case TransferredValue::Kind::Array: return BuildArray(value);
                case TransferredValue::Kind::Object: return BuildObject(v8::Object::New(_isolate), value);
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
                _isolate->ThrowException(v8::Exception::Error(FromUtf8(_isolate, message)));
                return {};
            }

            // V8 has no BigInt-from-decimal constructor; the script one does exactly that.
            v8::MaybeLocal<v8::Value> BuildBigInt(const std::string &decimal) {
                v8::Local<v8::Value> constructor;
                if (!_context->Global()->Get(_context, v8::String::NewFromUtf8Literal(_isolate, "BigInt")).ToLocal(&constructor) || !constructor->IsFunction()) {
                    return Throw("BigInt is unavailable");
                }
                v8::Local<v8::Value> argument = FromUtf8(_isolate, decimal);
                return constructor.As<v8::Function>()->Call(_context, v8::Undefined(_isolate), 1, &argument);
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

            v8::MaybeLocal<v8::Value> BuildObject(v8::Local<v8::Object> object, const TransferredValue &value) {
                for (const auto &[name, field] : value.properties) {
                    v8::Local<v8::Value> built;
                    if (!Build(field).ToLocal(&built) || object->Set(_context, FromUtf8(_isolate, name), built).IsNothing()) {
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

                size_t elementSize = 0;
                for (const auto &type : kViewTypes) {
                    if (value.text == type.name) {
                        elementSize = type.elementSize;
                    }
                }
                if (elementSize == 0) {
                    return Throw("Unknown view type " + value.text);
                }

                const size_t length = size / elementSize;
                if (value.text == "Int8Array") {
                    return v8::Int8Array::New(buffer, 0, length);
                }
                if (value.text == "Uint8Array") {
                    return v8::Uint8Array::New(buffer, 0, length);
                }
                if (value.text == "Uint8ClampedArray") {
                    return v8::Uint8ClampedArray::New(buffer, 0, length);
                }
                if (value.text == "Int16Array") {
                    return v8::Int16Array::New(buffer, 0, length);
                }
                if (value.text == "Uint16Array") {
                    return v8::Uint16Array::New(buffer, 0, length);
                }
                if (value.text == "Int32Array") {
                    return v8::Int32Array::New(buffer, 0, length);
                }
                if (value.text == "Uint32Array") {
                    return v8::Uint32Array::New(buffer, 0, length);
                }
                if (value.text == "Float32Array") {
                    return v8::Float32Array::New(buffer, 0, length);
                }
                if (value.text == "Float64Array") {
                    return v8::Float64Array::New(buffer, 0, length);
                }
                if (value.text == "BigInt64Array") {
                    return v8::BigInt64Array::New(buffer, 0, length);
                }
                if (value.text == "BigUint64Array") {
                    return v8::BigUint64Array::New(buffer, 0, length);
                }
                return v8::DataView::New(buffer, 0, length);
            }

            v8::MaybeLocal<v8::Value> BuildError(const TransferredValue &value) {
                std::string message;
                for (const auto &[name, field] : value.properties) {
                    if (name == "message") {
                        message = field.text;
                    }
                }
                v8::Local<v8::Value> error = v8::Exception::Error(FromUtf8(_isolate, message));
                // message is already set; the rest (name, stack, own fields) is written over the fresh error's.
                TransferredValue rest = value;
                std::erase_if(rest.properties, [](const auto &property) { return property.first == "message"; });
                return BuildObject(error.As<v8::Object>(), rest);
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
        if (!copier.Copy(value, out, "value")) {
            std::string error = copier.GetError();
            if (tryCatch.HasCaught()) {
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
