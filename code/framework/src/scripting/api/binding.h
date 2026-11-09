/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "native_abi.h"
#include "native_catalog.h"

#include "../engine_helpers.h"
#include "../scripting_catalog.h"

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>
#include <v8pp/metadata.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace Framework::Scripting::Api {
    namespace Detail {
        template <typename F>
        struct MemberFn;

        template <typename C, typename R, typename... A>
        struct MemberFn<R (C::*)(A...)> {
            using Class  = C;
            using Return = R;
            using Args   = std::tuple<A...>;
        };
        template <typename C, typename R, typename... A>
        struct MemberFn<R (C::*)(A...) const>: MemberFn<R (C::*)(A...)> {};
        template <typename C, typename R, typename... A>
        struct MemberFn<R (C::*)(A...) noexcept>: MemberFn<R (C::*)(A...)> {};
        template <typename C, typename R, typename... A>
        struct MemberFn<R (C::*)(A...) const noexcept>: MemberFn<R (C::*)(A...)> {};

        template <typename T>
        struct IsOptional: std::false_type {};
        template <typename T>
        struct IsOptional<std::optional<T>>: std::true_type {};

        // Whether every argument and the result of a member can cross the flat convention.
        template <auto Fn>
        constexpr bool NativeCallable() {
            using M               = MemberFn<decltype(Fn)>;
            constexpr bool result = std::is_void_v<typename M::Return> || NativeConvertible<typename M::Return>;
            return result && []<size_t... I>(std::index_sequence<I...>) {
                return (NativeConvertible<std::tuple_element_t<I, typename M::Args>> && ...);
            }(std::make_index_sequence<std::tuple_size_v<typename M::Args>>());
        }

        // What a V8 callback knows about the member it calls, for its errors.
        struct CallSite {
            std::string label; // "<Class>.<member>"
            std::vector<std::string> parameterNames;
            std::vector<std::string> parameterTypes;

            std::string ParameterName(size_t index) const {
                return index < parameterNames.size() && !parameterNames[index].empty() ? parameterNames[index] : "arg" + std::to_string(index);
            }
            std::string ParameterType(size_t index) const {
                return index < parameterTypes.size() && !parameterTypes[index].empty() ? parameterTypes[index] : "a valid value";
            }
        };

        template <typename A>
        std::remove_cvref_t<A> V8Argument(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &info, size_t index, const CallSite &site) {
            using D = std::remove_cvref_t<A>;
            if (static_cast<int>(index) >= info.Length() || info[static_cast<int>(index)]->IsUndefined()) {
                // A trailing string or optional may be left out; it reads as empty.
                if constexpr (std::is_same_v<D, std::string> || IsOptional<D>::value) {
                    return D {};
                }
                else {
                    ThrowArgumentError(site.label + ": missing argument '" + site.ParameterName(index) + "'");
                }
            }
            try {
                return v8pp::from_v8<D>(isolate, info[static_cast<int>(index)]);
            }
            catch (const ScriptError &) {
                throw;
            }
            catch (const std::exception &) {
                ThrowArgumentError(site.label + ": argument '" + site.ParameterName(index) + "' must be " + site.ParameterType(index));
            }
        }

        inline void ThrowIntoV8(v8::Isolate *isolate, ErrorKind kind, const char *message) {
            if (kind == ErrorKind::Argument) {
                ThrowTypeError(isolate, message);
                return;
            }
            isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, message).ToLocalChecked()));
        }

        // A member called from JavaScript. A stale receiver is a silent no-op; a ScriptError becomes its kind's
        // JavaScript error, anything else thrown an Error.
        template <typename T, auto Fn>
        void V8Invoke(const v8::FunctionCallbackInfo<v8::Value> &info) {
            using M              = MemberFn<decltype(Fn)>;
            v8::Isolate *isolate = info.GetIsolate();
            const auto *site     = static_cast<const CallSite *>(info.Data().As<v8::External>()->Value());

            T *self = v8pp::class_<T>::unwrap_object(isolate, info.This());
            if (!self) {
                return;
            }

            try {
                auto call = [&]<size_t... I>(std::index_sequence<I...>) -> decltype(auto) {
                    // Braced initialisation converts the arguments left to right, so the first bad one is reported.
                    std::tuple<std::remove_cvref_t<std::tuple_element_t<I, typename M::Args>>...> args {V8Argument<std::tuple_element_t<I, typename M::Args>>(isolate, info, I, *site)...};
                    return std::apply(
                        [self](auto &...values) -> decltype(auto) {
                            return (self->*Fn)(values...);
                        },
                        args);
                };
                constexpr auto indices = std::make_index_sequence<std::tuple_size_v<typename M::Args>>();
                if constexpr (std::is_void_v<typename M::Return>) {
                    call(indices);
                }
                else {
                    info.GetReturnValue().Set(v8pp::to_v8(isolate, call(indices)));
                }
            }
            catch (const ScriptError &error) {
                ThrowIntoV8(isolate, error.GetKind(), error.what());
            }
            catch (const std::exception &error) {
                ThrowIntoV8(isolate, ErrorKind::State, error.what());
            }
        }

        // A member called through the flat convention. The receiver is rebuilt from its id for each call.
        template <typename T, auto Fn>
        int32_t NativeInvoke(uint64_t self, const NativeValue *args, int32_t argc, NativeValue *result) {
            using M                 = MemberFn<decltype(Fn)>;
            constexpr size_t kCount = std::tuple_size_v<typename M::Args>;
            *result                 = NativeValue {};

            std::optional<T> receiver;
            try {
                receiver.emplace(self);
            }
            catch (const std::exception &) {
                return static_cast<int32_t>(NativeStatus::StaleReceiver);
            }

            try {
                if (argc != static_cast<int32_t>(kCount) || (kCount > 0 && args == nullptr)) {
                    ThrowArgumentError("expects " + std::to_string(kCount) + " argument(s), got " + std::to_string(argc));
                }
                auto call = [&]<size_t... I>(std::index_sequence<I...>) -> decltype(auto) {
                    std::tuple<std::remove_cvref_t<std::tuple_element_t<I, typename M::Args>>...> values {NativeTraits<std::remove_cvref_t<std::tuple_element_t<I, typename M::Args>>>::From(args[I], I)...};
                    return std::apply(
                        [&receiver](auto &...converted) -> decltype(auto) {
                            return ((*receiver).*Fn)(converted...);
                        },
                        values);
                };
                constexpr auto indices = std::make_index_sequence<kCount>();
                if constexpr (std::is_void_v<typename M::Return>) {
                    call(indices);
                }
                else {
                    NativeTraits<std::remove_cvref_t<typename M::Return>>::To(call(indices), *result);
                }
                return static_cast<int32_t>(NativeStatus::Ok);
            }
            catch (const ScriptError &error) {
                SetText(*result, error.what());
                return static_cast<int32_t>(error.GetKind());
            }
            catch (const std::exception &error) {
                SetText(*result, error.what());
                return static_cast<int32_t>(NativeStatus::StateError);
            }
        }

        template <auto Fn>
        std::vector<NativeParameter> NativeParameters(const v8pp::metadata::function_options &docs) {
            using M = MemberFn<decltype(Fn)>;
            std::vector<NativeParameter> result;
            [&]<size_t... I>(std::index_sequence<I...>) {
                (result.push_back({"arg" + std::to_string(I), NativeTraits<std::remove_cvref_t<std::tuple_element_t<I, typename M::Args>>>::CSharpType(), {}}), ...);
            }(std::make_index_sequence<std::tuple_size_v<typename M::Args>>());

            if (docs.explicit_signature) {
                const auto &documented = docs.explicit_signature->parameters;
                for (size_t i = 0; i < result.size() && i < documented.size(); ++i) {
                    result[i].name        = documented[i].name;
                    result[i].description = documented[i].description;
                }
            }
            for (size_t i = 0; i < result.size() && i < docs.parameters.size(); ++i) {
                if (!docs.parameters[i].name.empty()) {
                    result[i].name = docs.parameters[i].name;
                }
                if (!docs.parameters[i].description.empty()) {
                    result[i].description = docs.parameters[i].description;
                }
            }
            return result;
        }

        inline CallSite MakeCallSite(std::string label, const v8pp::metadata::function_options *docs) {
            CallSite site {std::move(label), {}, {}};
            if (docs && docs->explicit_signature) {
                for (const auto &parameter : docs->explicit_signature->parameters) {
                    site.parameterNames.push_back(parameter.name);
                    site.parameterTypes.push_back(parameter.value_type.name);
                }
            }
            return site;
        }
    } // namespace Detail

    /**
     * A scripting class declared once for every language (docs/scripting_csharp.md).
     *
     * Each member records its C++ function, its documentation, the side it exists on and the languages it is bound
     * for. JavaScript gets it through Build(), which makes the v8pp class and records the same metadata the
     * TypeScript declarations are generated from; every other language gets it through the NativeCatalog, which holds a
     * thunk per member and the description the C# API is generated from.
     *
     * A binding is a function-local static of its class (T::Binding()); members are bound in the order declared, and
     * JavaScript-only hooks (JavaScript()) run in that order too, so the generated declarations keep their order.
     */
    template <typename T>
    class ClassBinding final {
      public:
        using JavaScriptHook = std::function<void(v8::Isolate *, v8pp::class_<T> &)>;

        ClassBinding(std::string name, std::string description) {
            _native.name        = std::move(name);
            _native.description = std::move(description);
            if constexpr (NativeHandle<T>) {
                NativeCatalog::Get().AddClass(&_native);
            }
        }

        ClassBinding(const ClassBinding &)            = delete;
        ClassBinding &operator=(const ClassBinding &) = delete;

        const std::string &GetName() const {
            return _native.name;
        }

        const NativeClass &GetNative() const {
            return _native;
        }

        // The class this one extends; its binding is built first in each isolate, as v8pp requires.
        template <typename Base>
        ClassBinding &Inherits() {
            _native.base = Base::Binding().GetName();
            _entries.push_back({Side::Shared, [](v8::Isolate *isolate, v8pp::class_<T> &cls) {
                                    Base::GetClass(isolate);
                                    cls.template inherit<Base>();
                                }});
            return *this;
        }

        // The JavaScript constructor. Other languages make a handle from an id they were given.
        template <typename... Args>
        ClassBinding &Constructor(v8pp::metadata::function_options docs) {
            _entries.push_back({Side::Shared, [docs = std::move(docs)](v8::Isolate *, v8pp::class_<T> &cls) {
                                    cls.template ctor<Args...>(docs);
                                }});
            return *this;
        }

        template <auto Fn>
        ClassBinding &Method(const std::string &name, v8pp::metadata::function_options docs, Side side = Side::Shared, Target target = Target::All) {
            const std::string label = _native.name + "." + name;

            if (Includes(target, Target::JavaScript)) {
                const Detail::CallSite *site = &_sites.emplace_back(Detail::MakeCallSite(label, &docs));
                _entries.push_back({side, [name, docs, site](v8::Isolate *isolate, v8pp::class_<T> &cls) {
                                        GetScriptingCatalog(isolate).constructor(cls.metadata_symbol()->name).record(v8pp::metadata::function_of<v8::FunctionCallback>(name, docs, false));
                                        cls.class_function_template()->PrototypeTemplate()->Set(v8pp::to_v8(isolate, name), v8::FunctionTemplate::New(isolate, &Detail::V8Invoke<T, Fn>, v8::External::New(isolate, const_cast<Detail::CallSite *>(site))));
                                    }});
            }

            if (Includes(target, Target::Native)) {
                AddNativeMethod<Fn>(name, label, docs, side);
            }
            return *this;
        }

        template <auto Get>
        ClassBinding &Property(const std::string &name, v8pp::metadata::property_options docs, Side side = Side::Shared, Target target = Target::All) {
            return AddProperty<Get, nullptr>(name, std::move(docs), side, target);
        }

        template <auto Get, auto Set>
        ClassBinding &Property(const std::string &name, v8pp::metadata::property_options docs, Side side = Side::Shared, Target target = Target::All) {
            return AddProperty<Get, Set>(name, std::move(docs), side, target);
        }

        // A member written against V8 directly, run in declaration order. JavaScript only.
        ClassBinding &JavaScript(JavaScriptHook hook, Side side = Side::Shared) {
            _entries.push_back({side, std::move(hook)});
            return *this;
        }

        // The v8pp class for one isolate, with every member bound for JavaScript on this side.
        std::unique_ptr<v8pp::class_<T>> Build(v8::Isolate *isolate) const {
            auto cls = std::make_unique<v8pp::class_<T>>(isolate, GetScriptingCatalog(isolate), _native.name, _native.description);
            cls->auto_wrap_objects(true);
            const bool client = IsClientScripting(isolate);
            for (const auto &entry : _entries) {
                if (client && entry.side == Side::Server) {
                    continue;
                }
                entry.apply(isolate, *cls);
            }
            return cls;
        }

      private:
        struct Entry {
            Side side;
            JavaScriptHook apply;
        };

        template <auto Fn>
        void AddNativeMethod(const std::string &name, const std::string &label, const v8pp::metadata::function_options &docs, Side side) {
            if constexpr (NativeHandle<T> && Detail::NativeCallable<Fn>()) {
                using M = Detail::MemberFn<decltype(Fn)>;
                NativeMember member;
                member.kind        = NativeMember::Kind::Method;
                member.name        = name;
                member.description = docs.description;
                member.side        = side;
                member.parameters  = Detail::NativeParameters<Fn>(docs);
                if (docs.explicit_signature) {
                    member.returnDescription = docs.explicit_signature->return_description;
                }
                if (!docs.return_description.empty()) {
                    member.returnDescription = docs.return_description;
                }
                if constexpr (std::is_void_v<typename M::Return>) {
                    member.csharpType = "void";
                }
                else {
                    member.csharpType = NativeTraits<std::remove_cvref_t<typename M::Return>>::CSharpType();
                }
                member.id = NativeCatalog::IdOf(label);
                NativeCatalog::Get().AddThunk(member.id, label, &Detail::NativeInvoke<T, Fn>);
                _native.members.push_back(std::move(member));
            }
            else {
                throw std::logic_error("Scripting binding " + label + " takes or returns a type the flat convention cannot carry; bind it Target::JavaScript");
            }
        }

        template <auto Get, auto Set>
        ClassBinding &AddProperty(const std::string &name, v8pp::metadata::property_options docs, Side side, Target target) {
            constexpr bool kWritable = !std::is_same_v<decltype(Set), std::nullptr_t>;
            const std::string label  = _native.name + "." + name;

            if (Includes(target, Target::JavaScript)) {
                const Detail::CallSite *getSite = &_sites.emplace_back(Detail::MakeCallSite(label, nullptr));
                const Detail::CallSite *setSite = &_sites.emplace_back(Detail::CallSite {label, {name}, {docs.type}});
                _entries.push_back({side, [name, docs, getSite, setSite](v8::Isolate *isolate, v8pp::class_<T> &cls) {
                                        auto getter = v8::FunctionTemplate::New(isolate, &Detail::V8Invoke<T, Get>, v8::External::New(isolate, const_cast<Detail::CallSite *>(getSite)));
                                        v8::Local<v8::FunctionTemplate> setter;
                                        if constexpr (kWritable) {
                                            setter = v8::FunctionTemplate::New(isolate, &Detail::V8Invoke<T, Set>, v8::External::New(isolate, const_cast<Detail::CallSite *>(setSite)));
                                        }
                                        cls.accessor_property(name, getter, setter, docs);
                                    }});
            }

            if (Includes(target, Target::Native)) {
                if constexpr (NativeHandle<T> && Detail::NativeCallable<Get>()) {
                    using M = Detail::MemberFn<decltype(Get)>;
                    NativeMember member;
                    member.kind        = NativeMember::Kind::Property;
                    member.name        = name;
                    member.description = docs.description;
                    member.side        = side;
                    member.csharpType  = NativeTraits<std::remove_cvref_t<typename M::Return>>::CSharpType();
                    member.id          = NativeCatalog::IdOf(label);
                    NativeCatalog::Get().AddThunk(member.id, label, &Detail::NativeInvoke<T, Get>);
                    if constexpr (kWritable) {
                        static_assert(Detail::NativeCallable<Set>(), "a native property's setter must take what its getter returns");
                        member.setterId = NativeCatalog::IdOf(label + "=");
                        NativeCatalog::Get().AddThunk(member.setterId, label + "=", &Detail::NativeInvoke<T, Set>);
                    }
                    _native.members.push_back(std::move(member));
                }
                else {
                    throw std::logic_error("Scripting binding " + label + " has a type the flat convention cannot carry; bind it Target::JavaScript");
                }
            }
            return *this;
        }

        NativeClass _native;
        std::vector<Entry> _entries;
        std::deque<Detail::CallSite> _sites; // stable addresses: V8 callbacks hold them as External data
    };
} // namespace Framework::Scripting::Api
