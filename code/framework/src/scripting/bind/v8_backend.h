/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "descriptors.h"
#include "doc.h"
#include "types.h"

#include "../v8pp_glm.h"

#include <function2/function2.hpp>
#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/function.hpp>
#include <v8pp/metadata.hpp>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// JavaScript and Lua differences
//
// JavaScript gets a class through v8pp, unchanged, so that a binding ported to a definition behaves as it did. In
// four places that is not what the Lua backend does with the same definition:
//
// 1. A method that returns a reference or a pointer to another bound object, not its receiver. JavaScript gets
//    the object that already wraps it (`c.pick(other) === other`); Lua gets a copy.
// 2. A method of a base that returns a reference to its receiver, called on an object of a derived class whose
//    base part does not lie at its own address (a base that is not the first one). v8pp finds a wrapper by
//    address, finds none and returns a copy typed as the base, so the rest of a chain works on the copy
//    (`derived.add(1).add(1)` adds 1 to `derived`). Lua returns the receiver. With the base first, as in a single
//    inheritance, both return the receiver.
// 3. A parameter that is a pointer to a bound class. JavaScript passes nullptr for anything that is not such an
//    object, a number or null alike, so the function has to expect it; Lua refuses the call. A parameter that is
//    a reference is refused by both.
// 4. Errors. A wrong argument is reported in v8pp's words (`Argument count does not match function definition.
//    Expected 1 but got 0`, `expected Number, typeof=string`), which do not name the function; Lua's name it
//    (`bad argument #1 to 'Counter:add' (integer expected, got string)`).
//
// Besides, v8pp cannot return an object of a class that cannot be copied, by value or by reference: a class with
// such a member materialises into Lua only (V8CanReturn).

namespace Framework::Scripting::Bind {
    // What V8 needs of a bound class: Class<T> fills a V8Class, and materialising it builds the v8pp class a
    // hand-written binding would, with the same records in the catalog.
    namespace Detail {
        // What v8pp::metadata::docs(returns, {param(name, type, optional, description)...}, description,
        // returnsDescription) builds, from a description.
        inline v8pp::metadata::function_options V8FunctionOptions(const FunctionDesc &desc) {
            v8pp::metadata::function_options options = v8pp::metadata::docs(ToTypeScript(desc.returns), {}, desc.description, desc.returnsDescription);
            for (const ParamDesc &param : desc.params) {
                v8pp::metadata::parameter documented;
                documented.name                = param.name;
                documented.value_type.name     = ToTypeScript(param.type);
                documented.value_type.optional = param.optional;
                documented.description         = param.description;
                options.explicit_signature->parameters.push_back(std::move(documented));
            }
            return options;
        }

        // v8pp::metadata::property_docs(type, description) for a property with a setter, and the read-only form of
        // it for one without.
        inline v8pp::metadata::property_options V8PropertyOptions(const PropertyDesc &desc) {
            v8pp::metadata::property_options options;
            options.description = desc.description;
            options.type        = ToTypeScript(desc.type);
            options.readonly    = desc.readonly;
            options.static_     = desc.isStatic;
            return options;
        }

        // Whether v8pp can hand a value of type R to JavaScript. It copies an object it is given by value or by
        // reference, so an object of a class that cannot be copied only goes by pointer.
        template <typename R>
        constexpr bool V8CanReturn() {
            using Value = std::remove_cvref_t<R>;
            if constexpr (IsOptional<Value>::value || IsVector<Value>::value) {
                return V8CanReturn<typename Value::value_type>();
            }
            else {
                return !std::is_class_v<Value> || std::is_copy_constructible_v<Value>;
            }
        }

        // v8pp does not know a function type that is noexcept: its pointer goes as a pointer to the plain type.
        template <typename F>
        F V8Plain(F function) {
            return function;
        }
        template <typename R, typename... Args>
        auto V8Plain(R (*function)(Args...) noexcept) -> R (*)(Args...) {
            return function;
        }
        template <typename C, typename R, typename... Args>
        auto V8Plain(R (C::*function)(Args...) noexcept) -> R (C::*)(Args...) {
            return function;
        }
        template <typename C, typename R, typename... Args>
        auto V8Plain(R (C::*function)(Args...) const noexcept) -> R (C::*)(Args...) const {
            return function;
        }

        // v8pp takes what it binds as an rvalue, and a definition keeps its own.
        template <typename V>
        V V8Copy(const V &value) {
            return value;
        }

        // One member of a class: `bind` adds it to the v8pp class being built and, where v8pp does not, to the
        // symbol of the class in the catalog.
        template <typename T>
        struct V8Member final {
            Side side = Side::Shared;
            fu2::function<void(v8pp::class_<T> &cls, v8pp::metadata::symbol &symbol) const> bind;
            std::string refusal; // why JavaScript cannot have the member; empty when it can
        };

        // The member `name` of the class `owner`, which JavaScript cannot have.
        template <typename T>
        V8Member<T> V8Refuse(Side side, const std::string &owner, const std::string &name) {
            return {side, {}, "Bind: '" + owner + "." + name + "' returns an object of a class that cannot be copied, which JavaScript cannot receive"};
        }

        template <typename T>
        struct V8Class final {
            using Factory = fu2::function<T *(const v8::FunctionCallbackInfo<v8::Value> &) const>;

            std::vector<fu2::function<void(v8::Isolate *) const>> ensureBases;
            std::vector<void (*)(v8pp::class_<T> &cls)> bases;
            fu2::function<void(v8pp::class_<T> &cls, const Factory &factory) const> constructor; // empty without one
            Factory factory;                                                                     // empty for the one v8pp makes
            std::vector<V8Member<T>> members;                                                    // in the order they were declared
            std::vector<fu2::function<void(v8pp::class_<T> &, v8::Isolate *) const>> hooks;
        };

        template <typename T, typename Base>
        void V8Inherit(v8pp::class_<T> &cls) {
            cls.template inherit<Base>();
        }

        // Declares the constructor T(Args...) with its documentation; `factory` builds the object when there is one.
        // Otherwise the arguments are converted here and not by the factory v8pp makes: that one takes them as
        // Args &&, in which v8pp no longer sees a std::optional, so a trailing one could not be omitted.
        template <typename T, typename... Args>
        auto V8ConstructorOf(v8pp::metadata::function_options options) {
            return [options = std::move(options)](v8pp::class_<T> &cls, const typename V8Class<T>::Factory &factory) {
                if (factory.empty()) {
                    cls.template ctor<Args...>(options, [](const v8::FunctionCallbackInfo<v8::Value> &info) -> T * {
                        return v8pp::detail::call_from_v8<v8pp::raw_ptr_traits>(
                            [](Args... args) -> T * {
                                return new T(std::forward<Args>(args)...);
                            },
                            info);
                    });
                }
                else {
                    cls.template ctor<Args...>(options, factory);
                }
            };
        }

        // A member function goes to v8pp as it is.
        template <typename T, typename F, typename C, typename R, typename... Args>
            requires(!std::is_void_v<C>)
        V8Member<T> V8Method(Signature<C, R, Args...>, Side side, const std::string &owner, std::string name, F function, v8pp::metadata::function_options options) {
            if constexpr (!V8CanReturn<R>()) {
                return V8Refuse<T>(side, owner, name);
            }
            else {
                return {side,
                    [name = std::move(name), function = V8Plain(function), options = std::move(options)](v8pp::class_<T> &cls, v8pp::metadata::symbol &) {
                        cls.function(name, function, options);
                    },
                    {}};
            }
        }

        // A function that takes the receiver first is called on `this`, with its other parameters converted by
        // v8pp. v8pp has no such form: it binds anything but a member function as a static of the class too, and
        // calls it without the object. So the function goes on the prototype here, and is recorded the way
        // v8pp::class_::prototype_function records one.
        template <typename T, typename F, typename R, typename First, typename... Args>
        V8Member<T> V8Method(Signature<void, R, First, Args...>, Side side, const std::string &owner, std::string name, F function, v8pp::metadata::function_options options) {
            if constexpr (!V8CanReturn<R>()) {
                return V8Refuse<T>(side, owner, name);
            }
            else {
                return {side,
                    [name = std::move(name), function = std::move(function), options = std::move(options)](v8pp::class_<T> &cls, v8pp::metadata::symbol &symbol) {
                        v8::Isolate *isolate = cls.isolate();
                        v8::HandleScope scope(isolate);
                        auto onReceiver = [function](const v8::FunctionCallbackInfo<v8::Value> &info) -> R {
                            T *self = v8pp::class_<T>::unwrap_object(info.GetIsolate(), info.This());
                            if (self == nullptr) {
                                throw std::runtime_error("method called on null instance");
                            }
                            return v8pp::detail::call_from_v8<v8pp::raw_ptr_traits>(
                                [&function, self](Args... args) -> R {
                                    if constexpr (std::is_pointer_v<First>) {
                                        return std::invoke(function, self, std::forward<Args>(args)...);
                                    }
                                    else {
                                        return std::invoke(function, *self, std::forward<Args>(args)...);
                                    }
                                },
                                info);
                        };
                        cls.class_function_template()->PrototypeTemplate()->Set(v8pp::to_v8(isolate, name), v8pp::wrap_function_template(isolate, std::move(onReceiver)));
                        symbol.record(v8pp::metadata::function_of<v8::FunctionCallback>(name, options, false));
                    },
                    {}};
            }
        }

        // A static goes to v8pp as it is, on the constructor, with the record a hand-written binding adds for it.
        template <typename T, typename F, typename R, typename... Args>
        V8Member<T> V8Static(Signature<void, R, Args...>, Side side, const std::string &owner, std::string name, F function, v8pp::metadata::function_options options) {
            if constexpr (!V8CanReturn<R>()) {
                return V8Refuse<T>(side, owner, name);
            }
            else {
                using Plain = decltype(V8Plain(function));
                return {side,
                    [name = std::move(name), function = V8Plain(function), options = std::move(options)](v8pp::class_<T> &cls, v8pp::metadata::symbol &symbol) {
                        v8::Isolate *isolate = cls.isolate();
                        v8::HandleScope scope(isolate);
                        cls.js_function_template()->Set(isolate, name.c_str(), v8pp::wrap_function_template(isolate, V8Copy(function)));
                        symbol.record(v8pp::metadata::function_of<Plain>(name, options, true));
                    },
                    {}};
            }
        }

        // A getter or a setter as v8pp calls one: a member function as it is. v8pp gives the receiver to a function
        // only when that takes a reference to the class itself first, so a function that takes a pointer, or a base
        // of the class, is called through one that does.
        template <typename T, typename F, typename C, typename R, typename... Args>
            requires(!std::is_void_v<C>)
        auto V8Accessor(Signature<C, R, Args...>, F function) {
            return V8Plain(function);
        }

        template <typename T, typename F, typename R, typename First, typename... Args>
        auto V8Accessor(Signature<void, R, First, Args...>, F function) {
            return [function = std::move(function)](T &self, Args... args) -> R {
                if constexpr (std::is_pointer_v<First>) {
                    return std::invoke(function, &self, std::forward<Args>(args)...);
                }
                else {
                    return std::invoke(function, self, std::forward<Args>(args)...);
                }
            };
        }

        template <typename T, typename F>
        auto V8AccessorOf(F function) {
            return V8Accessor<T>(typename SignatureOf<F>::Type {}, std::move(function));
        }

        // A property without a setter. `Return` is what the getter returns.
        template <typename T, typename Return, typename G>
        V8Member<T> V8Property(Side side, const std::string &owner, std::string name, G getter, v8pp::metadata::property_options options) {
            if constexpr (!V8CanReturn<Return>()) {
                return V8Refuse<T>(side, owner, name);
            }
            else {
                return {side,
                    [name = std::move(name), getter = V8AccessorOf<T>(std::move(getter)), options = std::move(options)](v8pp::class_<T> &cls, v8pp::metadata::symbol &) {
                        cls.property(name, V8Copy(getter), V8Copy(options));
                    },
                    {}};
            }
        }

        // A property with a setter.
        template <typename T, typename Return, typename G, typename S>
        V8Member<T> V8Property(Side side, const std::string &owner, std::string name, G getter, S setter, v8pp::metadata::property_options options) {
            if constexpr (!V8CanReturn<Return>()) {
                return V8Refuse<T>(side, owner, name);
            }
            else {
                return {side,
                    [name = std::move(name), getter = V8AccessorOf<T>(std::move(getter)), setter = V8AccessorOf<T>(std::move(setter)), options = std::move(options)](v8pp::class_<T> &cls, v8pp::metadata::symbol &) {
                        cls.property(name, V8Copy(getter), V8Copy(setter), V8Copy(options));
                    },
                    {}};
            }
        }

        // Builds the v8pp class of `definition` in `isolate` the way a hand-written binding does, with the members
        // that exist in a runtime of side `runtime`.
        template <typename T>
        std::unique_ptr<v8pp::class_<T>> V8MaterialiseClass(const V8Class<T> &definition, const ClassDesc &desc, v8::Isolate *isolate, v8pp::metadata::registry &catalog, Side runtime) {
            for (const V8Member<T> &member : definition.members) {
                if (Materialises(member.side, runtime) && !member.refusal.empty()) {
                    throw std::logic_error(member.refusal);
                }
            }
            for (const auto &ensure : definition.ensureBases) {
                ensure(isolate);
            }

            v8::HandleScope scope(isolate);
            auto cls = std::make_unique<v8pp::class_<T>>(isolate, catalog, desc.name, desc.description);
            cls->auto_wrap_objects(true);
            for (const auto &inherit : definition.bases) {
                inherit(*cls);
            }
            if (!definition.constructor.empty()) {
                definition.constructor(*cls, definition.factory);
            }

            v8pp::metadata::symbol &symbol = catalog.constructor(desc.name);
            for (const V8Member<T> &member : definition.members) {
                if (Materialises(member.side, runtime)) {
                    member.bind(*cls, symbol);
                }
            }
            for (const auto &hook : definition.hooks) {
                hook(*cls, isolate);
            }
            return cls;
        }
    } // namespace Detail
} // namespace Framework::Scripting::Bind
