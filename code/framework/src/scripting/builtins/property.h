/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Framework::Scripting::Builtins {
    // Helpers to register JS accessor properties on a v8pp-wrapped class from plain getter/setter
    // member functions. V8 accessor callbacks must be non-capturing function pointers, so the
    // getter/setter are passed as template parameters (compile-time constants) rather than captured.
    //
    //   RegisterProperty<Class, &Class::GetX, &Class::SetX>(isolate, proto, "x");        // scalar/string
    //   RegisterReadonlyProperty<Class, &Class::GetX>(isolate, proto, "x");
    //   RegisterObjectProperty<Class, &Class::GetColor, &Class::SetColor>(...);            // wrapped value
    //   RegisterReadonlyObjectProperty<Class, &Class::GetPos>(...);

    namespace detail {
        template <typename T>
        struct MemberReturn;
        template <typename C, typename R>
        struct MemberReturn<R (C::*)()> {
            using type = R;
        };
        template <typename C, typename R>
        struct MemberReturn<R (C::*)() const> {
            using type = R;
        };

        template <typename T>
        struct MemberArg;
        template <typename C, typename A>
        struct MemberArg<void (C::*)(A)> {
            using type = A;
        };

        // Push a scalar/string value onto the V8 return slot. Generic over the callback-info type so
        // it serves both property accessors and FunctionTemplate-based accessors.
        template <typename Info, typename T>
        inline void Return(const Info &info, T &&value) {
            using V = std::decay_t<T>;
            if constexpr (std::is_same_v<V, std::string>) {
                info.GetReturnValue().Set(v8pp::to_v8(info.GetIsolate(), value));
            }
            else if constexpr (std::is_integral_v<V> && sizeof(V) > 4) {
                // V8's ReturnValue has no 64-bit overload; values up to 2^53 (e.g. NetworkIDs) are
                // exact as JS numbers.
                info.GetReturnValue().Set(static_cast<double>(value));
            }
            else if constexpr (std::is_arithmetic_v<V>) {
                info.GetReturnValue().Set(value);
            }
            else {
                // Containers and other composites go through the v8pp converter (vector -> JS array).
                info.GetReturnValue().Set(v8pp::to_v8(info.GetIsolate(), std::forward<T>(value)));
            }
        }

        // Apply a JS value to a scalar/string setter, ignoring mismatched types (as before).
        template <typename Arg, typename Fn>
        inline void Apply(v8::Isolate *isolate, v8::Local<v8::Value> value, Fn &&apply) {
            using A  = std::decay_t<Arg>;
            auto ctx = isolate->GetCurrentContext();
            if constexpr (std::is_same_v<A, bool>) {
                if (value->IsBoolean())
                    apply(value->BooleanValue(isolate));
            }
            else if constexpr (std::is_same_v<A, std::string>) {
                if (value->IsString()) {
                    v8::String::Utf8Value str(isolate, value);
                    apply(std::string(*str ? *str : ""));
                }
            }
            else if constexpr (std::is_floating_point_v<A>) {
                if (value->IsNumber())
                    apply(static_cast<A>(value->NumberValue(ctx).FromMaybe(0.0)));
            }
            else if constexpr (std::is_integral_v<A>) {
                // Any JS number, not just int32, so values up to 2^53 (e.g. NetworkIDs) round-trip.
                if (value->IsNumber())
                    apply(static_cast<A>(value->IntegerValue(ctx).FromMaybe(0)));
            }
        }

        template <typename Class, auto Getter>
        v8::Local<v8::FunctionTemplate> ScalarGetter(v8::Isolate *isolate) {
            return v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Class>::unwrap_object(info.GetIsolate(), info.This());
                if (self)
                    Return(info, (self->*Getter)());
            });
        }

        template <typename Class, auto Setter>
        v8::Local<v8::FunctionTemplate> ScalarSetter(v8::Isolate *isolate) {
            return v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Class>::unwrap_object(info.GetIsolate(), info.This());
                if (!self || info.Length() < 1)
                    return;
                using Arg = typename MemberArg<decltype(Setter)>::type;
                Apply<Arg>(info.GetIsolate(), info[0], [&](auto &&v) {
                    (self->*Setter)(std::forward<decltype(v)>(v));
                });
            });
        }

        template <typename Class, auto Getter>
        v8::Local<v8::FunctionTemplate> ObjectGetter(v8::Isolate *isolate) {
            using Value = std::decay_t<typename MemberReturn<decltype(Getter)>::type>;
            return v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Class>::unwrap_object(info.GetIsolate(), info.This());
                if (self) {
                    auto &cls = Value::GetClass(info.GetIsolate());
                    info.GetReturnValue().Set(cls.import_external(info.GetIsolate(), new Value((self->*Getter)())));
                }
            });
        }

        template <typename Class, auto Setter, typename Value>
        v8::Local<v8::FunctionTemplate> ObjectSetter(v8::Isolate *isolate) {
            return v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Class>::unwrap_object(info.GetIsolate(), info.This());
                if (!self || info.Length() < 1)
                    return;
                auto *val = v8pp::class_<Value>::unwrap_object(info.GetIsolate(), info[0]);
                if (val)
                    (self->*Setter)(*val);
            });
        }
    } // namespace detail

    // Read/write scalar or string property. Installed as an accessor pair so the setter fires when a
    // script assigns to the property through the prototype chain.
    template <typename Class, auto Getter, auto Setter>
    void RegisterProperty(v8::Isolate *isolate, v8::Local<v8::ObjectTemplate> proto, std::string_view name) {
        auto getter = detail::ScalarGetter<Class, Getter>(isolate);
        auto setter = detail::ScalarSetter<Class, Setter>(isolate);
        proto->SetAccessorProperty(v8pp::to_v8(isolate, name).As<v8::Name>(), getter, setter);
    }

    template <typename Class, auto Getter, auto Setter>
    void RegisterProperty(v8pp::class_<Class> &binding, std::string_view name, v8pp::metadata::property_options docs) {
        auto *isolate = binding.isolate();
        binding.accessor_property(name, detail::ScalarGetter<Class, Getter>(isolate), detail::ScalarSetter<Class, Setter>(isolate), std::move(docs));
    }

    // Read-only scalar or string property.
    template <typename Class, auto Getter>
    void RegisterReadonlyProperty(v8::Isolate *isolate, v8::Local<v8::ObjectTemplate> proto, std::string_view name) {
        proto->SetAccessorProperty(v8pp::to_v8(isolate, name).As<v8::Name>(), detail::ScalarGetter<Class, Getter>(isolate), {}, v8::ReadOnly);
    }

    template <typename Class, auto Getter>
    void RegisterReadonlyProperty(v8pp::class_<Class> &binding, std::string_view name, v8pp::metadata::property_options docs) {
        auto *isolate = binding.isolate();
        binding.accessor_property(name, detail::ScalarGetter<Class, Getter>(isolate), {}, std::move(docs));
    }

    // Read/write property whose value is itself a v8pp-wrapped class returned/taken by value
    // (e.g. Color, Vector3). The value type is deduced from the getter; its result is copied into a
    // fresh JS-owned object.
    template <typename Class, auto Getter, auto Setter>
    void RegisterObjectProperty(v8::Isolate *isolate, v8::Local<v8::ObjectTemplate> proto, std::string_view name) {
        using Value = std::decay_t<typename detail::MemberReturn<decltype(Getter)>::type>;
        auto getter = detail::ObjectGetter<Class, Getter>(isolate);
        auto setter = detail::ObjectSetter<Class, Setter, Value>(isolate);
        proto->SetAccessorProperty(v8pp::to_v8(isolate, name).As<v8::Name>(), getter, setter);
    }

    template <typename Class, auto Getter, auto Setter>
    void RegisterObjectProperty(v8pp::class_<Class> &binding, std::string_view name, v8pp::metadata::property_options docs) {
        using Value   = std::decay_t<typename detail::MemberReturn<decltype(Getter)>::type>;
        auto *isolate = binding.isolate();
        binding.accessor_property(name, detail::ObjectGetter<Class, Getter>(isolate), detail::ObjectSetter<Class, Setter, Value>(isolate), std::move(docs));
    }

    // Read-only wrapped-value property (e.g. Vector3). The value type is deduced from the getter.
    template <typename Class, auto Getter>
    void RegisterReadonlyObjectProperty(v8::Isolate *isolate, v8::Local<v8::ObjectTemplate> proto, std::string_view name) {
        proto->SetAccessorProperty(v8pp::to_v8(isolate, name).As<v8::Name>(), detail::ObjectGetter<Class, Getter>(isolate), {}, v8::ReadOnly);
    }

    template <typename Class, auto Getter>
    void RegisterReadonlyObjectProperty(v8pp::class_<Class> &binding, std::string_view name, v8pp::metadata::property_options docs) {
        auto *isolate = binding.isolate();
        binding.accessor_property(name, detail::ObjectGetter<Class, Getter>(isolate), {}, std::move(docs));
    }
} // namespace Framework::Scripting::Builtins
