/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <core_modules.h>
#include <networking/replication/network_entity.h>
#include <networking/replication/replication_manager.h>

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

namespace Framework::Scripting::Builtins {

    // EntityCollection's secondary Filter policies.
    struct AcceptAll {
        bool operator()(const Networking::Replication::NetworkEntity *) const {
            return true;
        }
    };
    struct ViewersOnly {
        bool operator()(const Networking::Replication::NetworkEntity *e) const {
            return e && e->streaming.isViewer; // a connection's avatar, i.e. an actual player
        }
    };

    // Array-like JS view (length/forEach/filter/find/map/some/every) over the replicated entities of a
    // category — e.g. World.players. JsType is the scripting wrapper, needing a static
    // GetClass(isolate) and a JsType(uint64_t) ctor; NativeType the replicated entity. Instantiated in
    // mod code where NativeType is complete, so this header pulls in only the NetworkEntity base.
    template <typename JsType, typename NativeType, typename Predicate = AcceptAll>
    class EntityCollection final {
      public:
        EntityCollection() = default;

        template <typename Fn>
        static void ForEachNative(Fn &&fn) {
            auto *repl = CoreModules::GetReplication();
            if (!repl) {
                return;
            }
            Predicate filter;
            repl->ForEachEntity([&](Networking::Replication::NetworkEntity *e) {
                auto *typed = dynamic_cast<NativeType *>(e);
                if (!typed) {
                    return;
                }
                if (!filter(typed)) {
                    return;
                }
                fn(typed);
            });
        }

        int GetLength(v8::Isolate *) const {
            int count = 0;
            ForEachNative([&count](NativeType *) {
                count++;
            });
            return count;
        }

        // Per-iteration HandleScope so wrapper handles don't pile up across the sweep; retained results
        // go into the output array (heap-rooted) before the scope closes, Find escapes its match.
        void ForEach(v8::Isolate *isolate, v8::Local<v8::Function> callback) {
            auto ctx          = isolate->GetCurrentContext();
            bool hadException = false;
            ForEachNative([&](NativeType *e) {
                if (hadException) return;
                v8::HandleScope iterationScope(isolate);
                v8::Local<v8::Object> jsEntity = JsType::GetClass(isolate).import_external(isolate, new JsType(e->GetNetworkID()));
                v8::Local<v8::Value> argv[1]   = {jsEntity};
                v8::TryCatch try_catch(isolate);
                v8::Local<v8::Value> result;
                if (!callback->Call(ctx, v8::Undefined(isolate), 1, argv).ToLocal(&result)) {
                    if (try_catch.HasCaught()) isolate->ThrowException(try_catch.Exception());
                    hadException = true;
                }
            });
        }

        v8::Local<v8::Array> Filter(v8::Isolate *isolate, v8::Local<v8::Function> callback) {
            auto ctx                 = isolate->GetCurrentContext();
            bool hadException        = false;
            v8::Local<v8::Array> arr = v8::Array::New(isolate, 0);
            uint32_t outIndex        = 0;
            ForEachNative([&](NativeType *e) {
                if (hadException) return;
                v8::HandleScope iterationScope(isolate);
                v8::Local<v8::Object> jsEntity = JsType::GetClass(isolate).import_external(isolate, new JsType(e->GetNetworkID()));
                v8::Local<v8::Value> argv[1]   = {jsEntity};
                v8::TryCatch try_catch(isolate);
                v8::Local<v8::Value> result;
                if (!callback->Call(ctx, v8::Undefined(isolate), 1, argv).ToLocal(&result)) {
                    if (try_catch.HasCaught()) isolate->ThrowException(try_catch.Exception());
                    hadException = true;
                    return;
                }
                if (result->BooleanValue(isolate)) arr->Set(ctx, outIndex++, jsEntity).Check();
            });
            return hadException ? v8::Array::New(isolate, 0) : arr;
        }

        v8::Local<v8::Value> Find(v8::Isolate *isolate, v8::Local<v8::Function> callback) {
            auto ctx                   = isolate->GetCurrentContext();
            v8::Local<v8::Value> found = v8::Undefined(isolate);
            bool foundOne = false, hadException = false;
            ForEachNative([&](NativeType *e) {
                if (foundOne || hadException) return;
                v8::EscapableHandleScope iterationScope(isolate);
                v8::Local<v8::Object> jsEntity = JsType::GetClass(isolate).import_external(isolate, new JsType(e->GetNetworkID()));
                v8::Local<v8::Value> argv[1]   = {jsEntity};
                v8::TryCatch try_catch(isolate);
                v8::Local<v8::Value> result;
                if (!callback->Call(ctx, v8::Undefined(isolate), 1, argv).ToLocal(&result)) {
                    if (try_catch.HasCaught()) isolate->ThrowException(try_catch.Exception());
                    hadException = true;
                    return;
                }
                if (result->BooleanValue(isolate)) {
                    found    = iterationScope.Escape(jsEntity);
                    foundOne = true;
                }
            });
            return hadException ? v8::Undefined(isolate) : found;
        }

        v8::Local<v8::Array> Map(v8::Isolate *isolate, v8::Local<v8::Function> callback) {
            auto ctx                 = isolate->GetCurrentContext();
            bool hadException        = false;
            v8::Local<v8::Array> arr = v8::Array::New(isolate, 0);
            uint32_t outIndex        = 0;
            ForEachNative([&](NativeType *e) {
                if (hadException) return;
                v8::HandleScope iterationScope(isolate);
                v8::Local<v8::Object> jsEntity = JsType::GetClass(isolate).import_external(isolate, new JsType(e->GetNetworkID()));
                v8::Local<v8::Value> argv[1]   = {jsEntity};
                v8::TryCatch try_catch(isolate);
                v8::Local<v8::Value> result;
                if (!callback->Call(ctx, v8::Undefined(isolate), 1, argv).ToLocal(&result)) {
                    if (try_catch.HasCaught()) isolate->ThrowException(try_catch.Exception());
                    hadException = true;
                    return;
                }
                arr->Set(ctx, outIndex++, result).Check();
            });
            return hadException ? v8::Array::New(isolate, 0) : arr;
        }

        bool Some(v8::Isolate *isolate, v8::Local<v8::Function> callback) {
            auto ctx = isolate->GetCurrentContext();
            bool found = false, hadException = false;
            ForEachNative([&](NativeType *e) {
                if (found || hadException) return;
                v8::HandleScope iterationScope(isolate);
                v8::Local<v8::Object> jsEntity = JsType::GetClass(isolate).import_external(isolate, new JsType(e->GetNetworkID()));
                v8::Local<v8::Value> argv[1]   = {jsEntity};
                v8::TryCatch try_catch(isolate);
                v8::Local<v8::Value> result;
                if (!callback->Call(ctx, v8::Undefined(isolate), 1, argv).ToLocal(&result)) {
                    if (try_catch.HasCaught()) isolate->ThrowException(try_catch.Exception());
                    hadException = true;
                    return;
                }
                if (result->BooleanValue(isolate)) found = true;
            });
            return hadException ? false : found;
        }

        bool Every(v8::Isolate *isolate, v8::Local<v8::Function> callback) {
            auto ctx = isolate->GetCurrentContext();
            bool allMatch = true, hadException = false;
            ForEachNative([&](NativeType *e) {
                if (!allMatch || hadException) return;
                v8::HandleScope iterationScope(isolate);
                v8::Local<v8::Object> jsEntity = JsType::GetClass(isolate).import_external(isolate, new JsType(e->GetNetworkID()));
                v8::Local<v8::Value> argv[1]   = {jsEntity};
                v8::TryCatch try_catch(isolate);
                v8::Local<v8::Value> result;
                if (!callback->Call(ctx, v8::Undefined(isolate), 1, argv).ToLocal(&result)) {
                    if (try_catch.HasCaught()) isolate->ThrowException(try_catch.Exception());
                    hadException = true;
                    return;
                }
                if (!result->BooleanValue(isolate)) allMatch = false;
            });
            return hadException ? false : allMatch;
        }
    };

    // The JS object exposing CollectionType's array-like surface.
    template <typename CollectionType>
    v8::Local<v8::Object> CreateCollectionObject(v8::Isolate *isolate) {
        auto ctx  = isolate->GetCurrentContext();
        auto tmpl = v8::ObjectTemplate::New(isolate);

        tmpl->SetNativeDataProperty(v8pp::to_v8(isolate, "length").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                CollectionType collection;
                info.GetReturnValue().Set(collection.GetLength(info.GetIsolate()));
            });

        auto obj = tmpl->NewInstance(ctx).ToLocalChecked();

        auto method = [&](const char *name, void (*fn)(const v8::FunctionCallbackInfo<v8::Value> &)) {
            obj->Set(ctx, v8pp::to_v8(isolate, name), v8::Function::New(ctx, fn).ToLocalChecked()).Check();
        };

        method("forEach", [](const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto isolate = info.GetIsolate();
            if (info.Length() < 1 || !info[0]->IsFunction()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "forEach requires a callback function")));
                return;
            }
            CollectionType collection;
            collection.ForEach(isolate, info[0].As<v8::Function>());
        });
        method("filter", [](const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto isolate = info.GetIsolate();
            if (info.Length() < 1 || !info[0]->IsFunction()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "filter requires a callback function")));
                return;
            }
            CollectionType collection;
            info.GetReturnValue().Set(collection.Filter(isolate, info[0].As<v8::Function>()));
        });
        method("find", [](const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto isolate = info.GetIsolate();
            if (info.Length() < 1 || !info[0]->IsFunction()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "find requires a callback function")));
                return;
            }
            CollectionType collection;
            info.GetReturnValue().Set(collection.Find(isolate, info[0].As<v8::Function>()));
        });
        method("map", [](const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto isolate = info.GetIsolate();
            if (info.Length() < 1 || !info[0]->IsFunction()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "map requires a callback function")));
                return;
            }
            CollectionType collection;
            info.GetReturnValue().Set(collection.Map(isolate, info[0].As<v8::Function>()));
        });
        method("some", [](const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto isolate = info.GetIsolate();
            if (info.Length() < 1 || !info[0]->IsFunction()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "some requires a callback function")));
                return;
            }
            CollectionType collection;
            info.GetReturnValue().Set(collection.Some(isolate, info[0].As<v8::Function>()));
        });
        method("every", [](const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto isolate = info.GetIsolate();
            if (info.Length() < 1 || !info[0]->IsFunction()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "every requires a callback function")));
                return;
            }
            CollectionType collection;
            info.GetReturnValue().Set(collection.Every(isolate, info[0].As<v8::Function>()));
        });

        return obj;
    }

} // namespace Framework::Scripting::Builtins
