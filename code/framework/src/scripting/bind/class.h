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
#include "lua_class.h"
#include "lua_convert.h"
#include "lua_glm.h"
#include "types.h"
#include "v8_backend.h"

#include "../lua/lua_transfer.h"
#include "../value_transfer.h"

#include <function2/function2.hpp>
#include <lua.h>
#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/metadata.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Framework::Scripting::Bind {
    namespace Detail {
        template <typename... Args>
        std::vector<LuaParam> LuaParamsOf() {
            return {LuaParam {&LuaConvertFor<Args>::Is, &LuaConvertFor<Args>::Expected, IsOptional<std::remove_cvref_t<Args>>::value}...};
        }

        // Whether `result`, from a callable declared to return R, designates the receiver `self`: R is a reference
        // or a pointer to a bound class and what it refers to is the receiver, or the base part of it.
        template <typename R, typename Receiver>
        bool LuaReturnsReceiver(const std::remove_reference_t<R> &result, const Receiver *self) {
            using Value = std::remove_cvref_t<R>;
            if constexpr (std::is_lvalue_reference_v<R> && HasScriptClassName<Value>) {
                if constexpr (std::is_convertible_v<const Receiver *, const Value *>) {
                    return static_cast<const Value *>(self) == std::addressof(result);
                }
            }
            else if constexpr (std::is_pointer_v<Value>) {
                using Pointee = std::remove_cv_t<std::remove_pointer_t<Value>>;
                if constexpr (HasScriptClassName<Pointee> && std::is_convertible_v<const Receiver *, const Pointee *>) {
                    return static_cast<const Pointee *>(self) == result;
                }
            }
            return false;
        }

        // Pushes `result`, which `function` returned as an R and which is not its receiver. An object goes into a
        // new userdata: moved when it was returned by value, copied when it was returned by reference or pointer.
        // A class that cannot be copied has no copy to give, so that is a Lua error, except for a null pointer,
        // which is nil as for any class.
        template <typename R>
        void LuaPushResult(lua_State *L, const LuaFunction &function, std::remove_reference_t<R> &result) {
            using Value   = std::remove_cvref_t<R>;
            using Pointee = std::remove_cv_t<std::remove_pointer_t<Value>>;
            if constexpr (std::is_lvalue_reference_v<R> && HasScriptClassName<Value> && !std::is_copy_constructible_v<Value>) {
                LuaRaiseNotCopyable(L, function, ScriptClassName<Value>::value);
            }
            else if constexpr (std::is_pointer_v<Value> && HasScriptClassName<Pointee> && !std::is_copy_constructible_v<Pointee>) {
                if (result == nullptr) {
                    lua_pushnil(L);
                }
                else {
                    LuaRaiseNotCopyable(L, function, ScriptClassName<Pointee>::value);
                }
            }
            else {
                LuaConvertFor<R>::Push(L, std::forward<R>(result));
            }
        }

        // Runs `call`, which returns the R of `function`, and pushes the result. The receiver, at index 1, is
        // pushed itself when the result designates it, so chaining keeps the identity of the object. `self` is
        // nullptr, as a const void *, for a call without a receiver.
        template <typename R, typename Receiver, typename Call>
        int LuaReturn(lua_State *L, const LuaFunction &function, const Receiver *self, Call &&call) {
            if constexpr (std::is_void_v<R>) {
                call();
                return 0;
            }
            else {
                R result = call();
                if (LuaReturnsReceiver<R>(result, self)) {
                    lua_pushvalue(L, 1);
                }
                else {
                    LuaPushResult<R>(L, function, result);
                }
                return 1;
            }
        }

        // The receiver as a callable takes it first: its address for a pointer, the object otherwise.
        template <typename First, typename T>
        decltype(auto) LuaReceiverAs(T &object) {
            if constexpr (std::is_pointer_v<First>) {
                return &object;
            }
            else {
                return (object);
            }
        }

        // Calls F on a receiver of class T as callable(receiver, arguments...), which is how std::invoke calls a
        // member function too. The arguments start at index 2.
        template <typename T, typename F, typename First, typename R, typename... Args>
        struct LuaMethodThunk final {
            static int Invoke(lua_State *L, const LuaFunction &function, void *self) {
                return Call(L, function, *static_cast<T *>(self), std::index_sequence_for<Args...> {});
            }

          private:
            template <size_t... I>
            static int Call(lua_State *L, const LuaFunction &function, T &object, std::index_sequence<I...>) {
                const F &callable = *static_cast<const F *>(function.thunk.target.get());
                return LuaReturn<R>(L, function, &object, [&]() -> R {
                    return std::invoke(callable, LuaReceiverAs<First>(object), LuaConvertFor<Args>::Get(L, 2 + static_cast<int>(I))...);
                });
            }
        };

        // Calls F with the arguments from index 1 on.
        template <typename F, typename R, typename... Args>
        struct LuaFunctionThunk final {
            static int Invoke(lua_State *L, const LuaFunction &function, void *) {
                return Call(L, function, std::index_sequence_for<Args...> {});
            }

          private:
            template <size_t... I>
            static int Call(lua_State *L, const LuaFunction &function, std::index_sequence<I...>) {
                const F &callable = *static_cast<const F *>(function.thunk.target.get());
                return LuaReturn<R>(L, function, static_cast<const void *>(nullptr), [&]() -> R {
                    return callable(LuaConvertFor<Args>::Get(L, 1 + static_cast<int>(I))...);
                });
            }
        };

        // Builds a T from the arguments from index 1 on, in a new userdata.
        template <typename T, typename... Args>
        struct LuaConstructorThunk final {
            static int Invoke(lua_State *L, const LuaFunction &, void *) {
                return Call(L, std::index_sequence_for<Args...> {});
            }

          private:
            template <size_t... I>
            static int Call(lua_State *L, std::index_sequence<I...>) {
                LuaNewObject<T>(L, LuaConvertFor<Args>::Get(L, 1 + static_cast<int>(I))...);
                return 1;
            }
        };

        // The class a callable that is not usable on a receiver is said to be called on.
        struct NoReceiver;

        // What Class knows of a callable bound as a method, a getter or a setter. A member function is called on
        // its class. A function or a lambda takes the receiver first, as a reference or a pointer, and that
        // parameter is not one a script passes.
        template <typename S>
        struct MethodShape;

        template <typename C, typename R, typename... Args>
        struct MethodShape<Signature<C, R, Args...>> final {
            using Receiver = C;
            using Return   = R;
            using Script   = Signature<C, R, Args...>;

            static constexpr size_t arity = sizeof...(Args);

            template <typename T, typename F>
            static LuaThunk Thunk(F function) {
                return {LuaParamsOf<Args...>(), &LuaMethodThunk<T, F, C &, R, Args...>::Invoke, std::make_shared<const F>(std::move(function))};
            }
        };

        template <typename R, typename First, typename... Args>
        struct MethodShape<Signature<void, R, First, Args...>> final {
            using Receiver = std::conditional_t<std::is_lvalue_reference_v<First> || std::is_pointer_v<First>, std::remove_cv_t<std::remove_pointer_t<std::remove_reference_t<First>>>, NoReceiver>;
            using Return   = R;
            using Script   = Signature<Receiver, R, Args...>;

            static constexpr size_t arity = sizeof...(Args);

            template <typename T, typename F>
            static LuaThunk Thunk(F function) {
                return {LuaParamsOf<Args...>(), &LuaMethodThunk<T, F, First, R, Args...>::Invoke, std::make_shared<const F>(std::move(function))};
            }
        };

        template <typename R>
        struct MethodShape<Signature<void, R>> final {
            using Receiver = NoReceiver;
            using Return   = R;
            using Script   = Signature<void, R>;

            static constexpr size_t arity = 0;

            template <typename T, typename F>
            static LuaThunk Thunk(F) {
                return {};
            }
        };

        template <typename F>
        using MethodShapeOf = MethodShape<typename SignatureOf<F>::Type>;

        // Whether a callable can be bound on a class T as a method, a getter or a setter.
        template <typename T, typename F>
        concept CallableOn = HasSignature<F> && std::is_convertible_v<T *, typename MethodShapeOf<F>::Receiver *>;

        template <typename S>
        struct IsFreeSignature final: std::false_type {};
        template <typename R, typename... Args>
        struct IsFreeSignature<Signature<void, R, Args...>> final: std::true_type {};

        // Whether a callable has no receiver: a function, a static member function or a lambda.
        template <typename F>
        concept FreeCallable = HasSignature<F> && IsFreeSignature<typename SignatureOf<F>::Type>::value;

        template <typename F, typename R, typename... Args>
        LuaThunk LuaFunctionThunkOf(Signature<void, R, Args...>, F function) {
            return {LuaParamsOf<Args...>(), &LuaFunctionThunk<F, R, Args...>::Invoke, std::make_shared<const F>(std::move(function))};
        }

        // What crosses of an object that crosses as a Payload: its bytes, as both sides are this process.
        template <typename Payload>
        void WritePayload(const Payload &payload, std::vector<uint8_t> &bytes) {
            bytes.resize(sizeof(Payload));
            std::memcpy(bytes.data(), &payload, sizeof(Payload));
        }

        template <typename Payload>
        Payload ReadPayload(const std::vector<uint8_t> &bytes) {
            Payload payload {};
            if (!bytes.empty()) {
                std::memcpy(&payload, bytes.data(), std::min(bytes.size(), sizeof(Payload)));
            }
            return payload;
        }

        // Registers T with ValueTransfer under `name`: an object crosses as the Payload `get` reads from it and is
        // built again from that Payload, wrapped the way Call::Wrap wraps one.
        template <typename T, typename Payload, typename G>
        void V8RegisterTransfer(const std::string &name, G get) {
            ValueTransfer::RegisterHostType(
                name,
                [get](v8::Isolate *isolate, v8::Local<v8::Object> object, std::vector<uint8_t> &bytes) {
                    V8EnsureClass<T>(isolate);
                    const T *wrapped = static_cast<const T *>(V8Unwrap<T>(isolate, object));
                    if (wrapped == nullptr) {
                        return false;
                    }
                    WritePayload<Payload>(std::invoke(get, *wrapped), bytes);
                    return true;
                },
                [name](v8::Isolate *isolate, v8::Local<v8::Context>, const std::vector<uint8_t> &bytes) -> v8::MaybeLocal<v8::Value> {
                    V8EnsureClass<T>(isolate);
                    // The constructor of a handle throws when its entity is gone by the time the value arrives.
                    std::string error;
                    try {
                        const v8::Local<v8::Value> wrapped = V8Adopt<T>(isolate, new T(ReadPayload<Payload>(bytes)));
                        if (!wrapped.IsEmpty()) {
                            return wrapped;
                        }
                        error = name + " is not available in this JavaScript runtime";
                    }
                    catch (const std::exception &exception) {
                        error = exception.what();
                    }
                    isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, error)));
                    return {};
                });
        }

        // The same for LuaTransfer. An object of a class derived from T is read through its T part.
        template <typename T, typename Payload, typename G>
        void LuaRegisterTransfer(const std::string &name, G get) {
            LuaTransfer::RegisterHostType(
                name,
                [get](lua_State *L, int index, std::vector<uint8_t> &bytes) {
                    const T *object = LuaObjectAt<T>(L, index);
                    if (object == nullptr) {
                        return false;
                    }
                    WritePayload<Payload>(std::invoke(get, *object), bytes);
                    return true;
                },
                [](lua_State *L, const std::vector<uint8_t> &bytes) {
                    LuaNewObject<T>(L, ReadPayload<Payload>(bytes));
                    return true;
                });
        }

        // The two above for a class that says itself what it crosses as: `write` fills the bytes from an object and
        // `read` builds one again from them.
        template <typename T, typename W, typename R>
        void V8RegisterTransferAs(const std::string &name, W write, R read) {
            ValueTransfer::RegisterHostType(
                name,
                [write](v8::Isolate *isolate, v8::Local<v8::Object> object, std::vector<uint8_t> &bytes) {
                    V8EnsureClass<T>(isolate);
                    const T *wrapped = static_cast<const T *>(V8Unwrap<T>(isolate, object));
                    if (wrapped == nullptr) {
                        return false;
                    }
                    std::invoke(write, *wrapped, bytes);
                    return true;
                },
                [name, read](v8::Isolate *isolate, v8::Local<v8::Context>, const std::vector<uint8_t> &bytes) -> v8::MaybeLocal<v8::Value> {
                    V8EnsureClass<T>(isolate);
                    std::string error;
                    try {
                        const v8::Local<v8::Value> wrapped = V8Adopt<T>(isolate, new T(std::invoke(read, bytes)));
                        if (!wrapped.IsEmpty()) {
                            return wrapped;
                        }
                        error = name + " is not available in this JavaScript runtime";
                    }
                    catch (const std::exception &exception) {
                        error = exception.what();
                    }
                    isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, error)));
                    return {};
                });
        }

        template <typename T, typename W, typename R>
        void LuaRegisterTransferAs(const std::string &name, W write, R read) {
            LuaTransfer::RegisterHostType(
                name,
                [write](lua_State *L, int index, std::vector<uint8_t> &bytes) {
                    const T *object = LuaObjectAt<T>(L, index);
                    if (object == nullptr) {
                        return false;
                    }
                    std::invoke(write, *object, bytes);
                    return true;
                },
                [read](lua_State *L, const std::vector<uint8_t> &bytes) {
                    // Built before anything is pushed: what `read` throws fails the copy.
                    T object = std::invoke(read, bytes);
                    LuaNewObject<T>(L, std::move(object));
                    return true;
                });
        }
    } // namespace Detail

    // The definition of a script class bound to the C++ class T: declared once in C++, described for the catalog
    // and materialised into each runtime. T is named with FW_SCRIPT_CLASS before it is defined, and defined once.
    //
    // A Class is a handle: a copy is the same definition. The definition itself is never destroyed, because the
    // catalog and every Lua state it was materialised into keep pointing into it.
    //
    // JavaScript gets the class through v8pp, built the way a binding written by hand is: v8pp converts the
    // arguments and words the errors. Lua gets it through LuaConvert.
    //
    // A member is implemented by a typed callable. A method, a getter or a setter takes a member function of T or
    // of one of its bases, or a function or a captureless lambda that takes the receiver first, as T &, const T &
    // or T *. A static or an operator takes a function, a static member function or a captureless lambda.
    // Parameter and return types follow the type model (spec 1.2); a trailing std::optional parameter may be
    // omitted by a script.
    //
    // A method or a static may be a context function instead, void(Bind::Call &), for what a signature cannot
    // say: one body, called from both languages (call.h).
    template <typename T>
    class Class final {
        static_assert(std::is_class_v<T> && std::is_same_v<T, std::remove_cv_t<T>>, "Bind::Class: T is a class type, without const or volatile");
        static_assert(Detail::HasScriptClassName<T>, "Bind::Class: give the class its script name with FW_SCRIPT_CLASS(Type, \"Name\") before defining it");
        static_assert(alignof(T) <= alignof(Detail::LuaMaxAlign), "Bind::Class: the class asks for more alignment than Lua gives a userdata");

      public:
        // Throws std::logic_error when T already has a definition, or when `name` is not the name FW_SCRIPT_CLASS
        // gave T. `side` is where the class exists at all: Shared in every runtime, with the members of each side;
        // Server or Client in the runtimes of that side only, where MaterialiseLua and RegisterLuaGlobal do
        // nothing anywhere else and MaterialiseV8 gives nullptr.
        Class(std::string name, std::string description, Side side = Side::Shared) {
            if (_registered != nullptr) {
                throw std::logic_error("Bind: '" + name + "' is a second definition of the C++ type bound as '" + _registered->Describe().name + "'");
            }
            if (name != ScriptClassName<T>::value) {
                throw std::logic_error("Bind: '" + name + "' is defined for the C++ type FW_SCRIPT_CLASS names '" + ScriptClassName<T>::value + "'");
            }

            _record                   = std::make_shared<Detail::LuaClass>();
            _record->desc.name        = std::move(name);
            _record->desc.description = std::move(description);
            _record->desc.side        = side;
            if constexpr (!std::is_trivially_destructible_v<T>) {
                _record->destroy = &Destroy;
            }
            _record->size = sizeof(T);
            _record->v8   = &Detail::V8AccessOf<T>;
            _v8           = std::make_shared<Detail::V8Class<T>>();

            Detail::LuaClassOf<T>::definition = _record.get();
            // Never deleted: this handle is what keeps the definition alive.
            _registered = new Class(*this);
            ApiCatalog::Get().Record(&_record->desc);
        }

        // Gives the class the methods, properties and operators of `Base`, and lets an instance pass where a
        // `Base` is expected. Statics, the constructor and hooks are not inherited. Throws std::logic_error when
        // `Base` has no definition yet.
        template <typename Base>
        Class &Inherit() {
            static_assert(!std::is_same_v<Base, T> && std::is_convertible_v<T *, Base *>, "Bind::Class::Inherit: Base is a public, unambiguous base class of T");
            const Class<Base> *base = Class<Base>::Find();
            if (base == nullptr) {
                throw std::logic_error("Bind: '" + _record->desc.name + "' inherits '" + ScriptClassName<Base>::value + "', which has no definition yet");
            }

            _record->bases.push_back({base->_record.get(), &Upcast<Base>});
            _record->desc.bases.push_back(base->Describe().name);
            _v8->bases.push_back(&Detail::V8Inherit<T, Base>);
            return *this;
        }

        // Runs `ensure` at the start of each MaterialiseV8. v8pp inherits from the class of the base in the
        // isolate, which has to exist by then: `ensure` is what makes it exist, the GetClass(isolate) of the base.
        Class &V8EnsureBase(fu2::function<void(v8::Isolate *) const> ensure) {
            _v8->ensureBases.push_back(std::move(ensure));
            return *this;
        }

        // Declares the constructor T(Args...), reached as Name.new(...) and Name(...) in Lua and as new Name(...)
        // and Name(...) in JavaScript. A trailing std::optional argument may be omitted, in both languages. A class
        // without a constructor cannot be constructed by a script: every one of these forms raises. Throws
        // std::logic_error when the class already has a constructor.
        template <typename... Args>
        Class &Constructor(Doc doc) {
            static_assert(std::is_constructible_v<T, Args...>, "Bind::Class::Constructor: T has no constructor taking Args");
            FunctionDesc desc = DescribeMember(Detail::Signature<void, void, Args...> {}, "constructor", doc, false, Side::Shared);
            Detail::LuaSetConstructor(*_record, {Detail::LuaParamsOf<Args...>(), &Detail::LuaConstructorThunk<T, Args...>::Invoke, nullptr});
            _v8->constructor          = Detail::V8ConstructorOf<T, Args...>(Detail::V8FunctionOptions(desc));
            _record->desc.constructor = std::move(desc);
            return *this;
        }

        // Builds the object of a JavaScript `new Name(...)` with `factory` instead of converting the arguments to
        // those of the declared constructor, for a constructor more lenient than its signature, such as one whose
        // arguments default. The object is made with new and JavaScript owns it. The constructor stays documented
        // as declared, and Lua keeps the declared one. Throws std::logic_error when the class declares no
        // constructor yet, or has a V8 constructor already.
        Class &V8Constructor(fu2::function<T *(const v8::FunctionCallbackInfo<v8::Value> &) const> factory) {
            if (_v8->constructor.empty()) {
                throw std::logic_error("Bind: '" + _record->desc.name + "' is given a V8 constructor before it declares a constructor");
            }
            if (!_v8->factory.empty()) {
                throw std::logic_error("Bind: '" + _record->desc.name + "' is given a second V8 constructor");
            }
            _v8->factory = std::move(factory);
            return *this;
        }

        // A method named `toString` is also what tostring() calls. A member cannot be overloaded: Method and
        // Property throw std::logic_error when the class already declares a method or a property of that name,
        // Static when it declares a static of that name or the name is `new`. A class may declare again what a
        // base declares.
        template <typename F>
            requires(!Detail::ContextCallable<F>)
        Class &Method(std::string name, F function, Doc doc, Side side = Side::Shared) {
            static_assert(Detail::CallableOn<T, F>, "Bind::Class::Method: F is a member function of T or of one of its bases, or a function or a lambda that takes the receiver first, as T &, const T & or T *");
            using Shape       = Detail::MethodShapeOf<F>;
            FunctionDesc desc = DescribeMember(typename Shape::Script {}, name, doc, false, side);
            Detail::LuaAddMethod(*_record, name, side, Shape::template Thunk<T, F>(function));
            _v8->members.push_back(Detail::V8Method<T>(typename Detail::SignatureOf<F>::Type {}, side, _record->desc.name, std::move(name), std::move(function), Detail::V8FunctionOptions(desc)));
            _record->desc.functions.push_back(std::move(desc));
            return *this;
        }

        template <typename F>
            requires(!Detail::ContextCallable<F>)
        Class &Static(std::string name, F function, Doc doc, Side side = Side::Shared) {
            static_assert(Detail::FreeCallable<F>, "Bind::Class::Static: F is a function, a static member function or a lambda");
            FunctionDesc desc = DescribeMember(typename Detail::SignatureOf<F>::Type {}, name, doc, true, side);
            Detail::LuaAddStatic(*_record, name, side, Detail::LuaFunctionThunkOf(typename Detail::SignatureOf<F>::Type {}, function));
            _v8->members.push_back(Detail::V8Static<T>(typename Detail::SignatureOf<F>::Type {}, side, _record->desc.name, std::move(name), std::move(function), Detail::V8FunctionOptions(desc)));
            _record->desc.functions.push_back(std::move(desc));
            return *this;
        }

        // A method or a static written as a context function: it is called with whatever the script passed and
        // checks it itself, and its Doc gives the types. A method finds its receiver with Call::Self<T>(); the
        // receiver is not one of its arguments. In Lua it is only ever called on an object of the class: called on
        // anything else, as a method called with `.` is, it raises as a typed method does. In JavaScript Self is
        // null for any other `this`, and the method returns without effect.
        Class &Method(std::string name, NativeFunction function, Doc doc, Side side = Side::Shared) {
            FunctionDesc desc = DescribeContextMember(name, doc, false, side);
            auto shared       = std::make_shared<NativeFunction>(std::move(function));
            Detail::LuaAddMethod(*_record, name, side, Detail::LuaNativeThunk(shared));
            _v8->members.push_back(Detail::V8NativeMethod<T>(side, std::move(name), std::move(shared), Detail::V8FunctionOptions(desc)));
            _record->desc.functions.push_back(std::move(desc));
            return *this;
        }

        Class &Static(std::string name, NativeFunction function, Doc doc, Side side = Side::Shared) {
            FunctionDesc desc = DescribeContextMember(name, doc, true, side);
            auto shared       = std::make_shared<NativeFunction>(std::move(function));
            Detail::LuaAddStatic(*_record, name, side, Detail::LuaNativeThunk(shared));
            _v8->members.push_back(Detail::V8NativeStatic<T>(side, std::move(name), std::move(shared), Detail::V8FunctionOptions(desc)));
            _record->desc.functions.push_back(std::move(desc));
            return *this;
        }

        // A read-only property.
        template <typename G>
        Class &Property(std::string name, G getter, std::string description, Side side = Side::Shared) {
            PropertyDesc desc = DescribeProperty<G>(name, std::move(description), true, side);
            Detail::LuaAddProperty(*_record, name, side, GetterThunk(getter), std::nullopt);
            _v8->members.push_back(Detail::V8Property<T, typename Detail::MethodShapeOf<G>::Return>(side, _record->desc.name, std::move(name), std::move(getter), Detail::V8PropertyOptions(desc)));
            _record->desc.properties.push_back(std::move(desc));
            return *this;
        }

        template <typename G, typename S>
        Class &Property(std::string name, G getter, S setter, std::string description, Side side = Side::Shared) {
            static_assert(Detail::CallableOn<T, S>, "Bind::Class::Property: the setter is a member function of T or of one of its bases, or a function or a lambda that takes the receiver first, as T & or T *");
            static_assert(Detail::MethodShapeOf<S>::arity == 1, "Bind::Class::Property: a setter takes the new value and nothing else");
            PropertyDesc desc = DescribeProperty<G>(name, std::move(description), false, side);
            Detail::LuaAddProperty(*_record, name, side, GetterThunk(getter), Detail::MethodShapeOf<S>::template Thunk<T, S>(setter));
            _v8->members.push_back(Detail::V8Property<T, typename Detail::MethodShapeOf<G>::Return>(side, _record->desc.name, std::move(name), std::move(getter), std::move(setter), Detail::V8PropertyOptions(desc)));
            _record->desc.properties.push_back(std::move(desc));
            return *this;
        }

        // A property written as context functions, for what a signature cannot say, such as a setter that takes one
        // of several types: `type` is what the property is described as. Each finds its receiver with
        // Call::Self<T>(), as a method does. The getter answers with Call::Return, and nothing is nil; the setter
        // reads the new value as Call::Arg(0). An empty setter makes the property read-only.
        Class &Property(std::string name, TypeRef type, NativeFunction getter, NativeFunction setter, std::string description, Side side = Side::Shared) {
            PropertyDesc desc;
            desc.name        = name;
            desc.type        = std::move(type);
            desc.description = std::move(description);
            desc.readonly    = setter.empty();
            desc.side        = side;

            auto read  = std::make_shared<NativeFunction>(std::move(getter));
            auto write = desc.readonly ? nullptr : std::make_shared<NativeFunction>(std::move(setter));
            Detail::LuaAddProperty(*_record, name, side, Detail::LuaNativeThunk(read), write != nullptr ? std::optional<Detail::LuaThunk>(Detail::LuaNativeThunk(write)) : std::nullopt);
            _v8->members.push_back(Detail::V8NativeProperty<T>(side, std::move(name), std::move(read), std::move(write), Detail::V8PropertyOptions(desc)));
            _record->desc.properties.push_back(std::move(desc));
            return *this;
        }

        // Gives the class a Lua operator. `function` takes every operand: two, or one for Unm and Len. Several
        // functions declared for one event are tried in order and the first that takes the operands is called,
        // which is how `vector * number` and `number * vector` coexist. Eq is false, not an error, when none
        // does. Nothing is described and JavaScript gets nothing: an operator exists in Lua only.
        template <typename F>
        Class &LuaMeta(LuaMetaEvent event, F function) {
            static_assert(Detail::FreeCallable<F>, "Bind::Class::LuaMeta: F is a function or a lambda that takes every operand");
            Detail::LuaAddMeta(*_record, event, Detail::LuaFunctionThunkOf(typename Detail::SignatureOf<F>::Type {}, std::move(function)));
            return *this;
        }

        // Runs `hook` each time the class is materialised into a Lua state, after its declared members are in place.
        Class &LuaHook(LuaClassHook hook) {
            _record->hooks.push_back(std::move(hook));
            return *this;
        }

        // Runs `hook` each time the class is materialised into an isolate, after its declared members are in place:
        // for what exists in JavaScript only, such as toJSON. What the hook adds, it records in the catalog itself;
        // v8pp::class_::prototype_function does both.
        Class &V8Hook(fu2::function<void(v8pp::class_<T> &, v8::Isolate *) const> hook) {
            _v8->hooks.push_back(std::move(hook));
            return *this;
        }

        // Names the properties an object of the class is written to JSON with from Lua, in the order they are
        // written (LuaJson::Encode): the keys and values its toJSON gives in JavaScript, where toJSON stays what a
        // V8Hook adds. Each is a property the class or one of its bases declared before this call. A class that
        // names none is the first of its bases that does, and an object of a class without any has no JSON form in
        // Lua. Throws std::logic_error for a name that is not such a property, for no name, and when called twice.
        Class &JsonProperties(const std::vector<std::string> &names) {
            Detail::LuaSetJsonProperties(*_record, names);
            return *this;
        }

        // Lets an object of the class cross between runtimes as itself, in both languages: it crosses as the bytes
        // of the Payload `get` reads from it, and is built again as T(payload) in the runtime it arrives in. A
        // value type crosses as its value (`&Vector3::vec`), a handle as its id (`&Player::GetId`); a constructor
        // that throws, as that of a handle whose entity is gone, fails the copy with what(). `get` is a member
        // function of T or of one of its bases, or anything else callable with a const T &.
        //
        // This only declares it. RegisterTransfer registers, at the time the caller chooses. Throws
        // std::logic_error when called twice.
        template <typename G>
            requires std::is_invocable_v<G, const T &>
        Class &Transfer(G get) {
            using Payload = std::remove_cvref_t<std::invoke_result_t<G, const T &>>;
            static_assert(std::is_trivially_copyable_v<Payload> && std::is_default_constructible_v<Payload>, "Bind::Class::Transfer: what the object crosses as is copied byte for byte");
            static_assert(std::is_constructible_v<T, const Payload &> && std::is_constructible_v<T, Payload>, "Bind::Class::Transfer: T is built again from what it crosses as");
            if (!_record->registerTransfer.empty()) {
                throw std::logic_error("Bind: '" + _record->desc.name + "' declares twice how it crosses between runtimes");
            }
            _record->registerTransfer = [name = _record->desc.name, get]() {
                Detail::V8RegisterTransfer<T, Payload>(name, get);
                Detail::LuaRegisterTransfer<T, Payload>(name, get);
            };
            return *this;
        }

        // Transfer for an object that crosses as more than the bytes of one value, as a handle that carries text
        // does: `write` puts what crosses into the bytes it is given, empty at first, and `read` builds the object
        // again from them, in the runtime it arrives in. Both are callable as write(const T &, std::vector<uint8_t>
        // &) and read(const std::vector<uint8_t> &); what `read` throws fails the copy with what().
        template <typename W, typename R>
            requires std::is_invocable_v<W, const T &, std::vector<uint8_t> &> && std::is_invocable_r_v<T, R, const std::vector<uint8_t> &>
        Class &Transfer(W write, R read) {
            if (!_record->registerTransfer.empty()) {
                throw std::logic_error("Bind: '" + _record->desc.name + "' declares twice how it crosses between runtimes");
            }
            _record->registerTransfer = [name = _record->desc.name, write, read]() {
                Detail::V8RegisterTransferAs<T>(name, write, read);
                Detail::LuaRegisterTransferAs<T>(name, write, read);
            };
            return *this;
        }

        // Registers the class as a host type of ValueTransfer and of LuaTransfer, under its name, the way Transfer
        // declared it. Process-wide, and for the start of the process: ValueTransfer tries the newest registration
        // first, so a class is registered after its bases, and calling this again moves the class to the front.
        // Throws std::logic_error when the class declared no Transfer.
        void RegisterTransfer() const {
            if (_record->registerTransfer.empty()) {
                throw std::logic_error("Bind: '" + _record->desc.name + "' is registered to cross between runtimes without a Transfer");
            }
            _record->registerTransfer();
        }

        // Undoes RegisterTransfer in both transfers: for a test whose class no later copy should be asked about.
        void UnregisterTransfer() const {
            ValueTransfer::UnregisterHostType(_record->desc.name);
            LuaTransfer::UnregisterHostType(_record->desc.name);
        }

        const ClassDesc &Describe() const {
            return _record->desc;
        }

        // Creates the metatable and the class table in the state of `L`, with the members that exist in a runtime
        // of side `runtime`, materialising the bases first. Does nothing in a state that already has the class,
        // whatever the side. Runs unprotected: a Lua error here is one of the state, such as running out of memory.
        void MaterialiseLua(lua_State *L, Side runtime) const {
            if (!ExistsIn(_record->desc, runtime)) {
                return;
            }
            Detail::LuaMaterialiseClass(L, *_record, runtime);
        }

        // target[name] = class table, without metamethods. Throws std::logic_error when the class is not
        // materialised in the state.
        void PublishLua(lua_State *L, int targetIndex) const {
            Detail::LuaPublishClass(L, *_record, targetIndex);
        }

        // MaterialiseLua, then the class table as a global of the state under the name of the class: what the
        // RegisterLua of a builtin does. In a state that already has the class, whatever the side, it only puts it
        // back under its name. The global is set raw, as PublishLua sets it. Runs unprotected, as MaterialiseLua: a
        // Lua error here panics unless the caller is in a protected call.
        void RegisterLuaGlobal(lua_State *L, Side runtime) const {
            if (!ExistsIn(_record->desc, runtime)) {
                return;
            }
            Detail::LuaRegisterClassGlobal(L, *_record, runtime);
        }

        // Builds the v8pp class in `isolate` the way a binding written by hand does, with the members that exist in
        // a runtime of side `runtime`, and records it in `catalog`. Every call builds a class, and an isolate takes
        // one per C++ type: the caller keeps what it gets for the isolate (ClassCache). Call with the isolate
        // entered. Throws std::logic_error when a member returns what JavaScript cannot receive, and what v8pp
        // throws when the isolate has the class already or lacks the class of a base.
        std::unique_ptr<v8pp::class_<T>> MaterialiseV8(v8::Isolate *isolate, v8pp::metadata::registry &catalog, Side runtime) const {
            if (!ExistsIn(_record->desc, runtime)) {
                return nullptr;
            }
            return Detail::V8MaterialiseClass(*_v8, _record->desc, isolate, catalog, runtime);
        }

        // The definition for T, or nullptr.
        static const Class *Find() {
            return _registered;
        }

      private:
        template <typename U>
        friend class Class;

        static void Destroy(void *object) {
            static_cast<T *>(object)->~T();
        }

        template <typename Base>
        static void *Upcast(void *object) {
            return static_cast<Base *>(static_cast<T *>(object));
        }

        // Described under its qualified name, so that a Doc that disagrees with the signature is reported as
        // <Class>.<member>.
        template <typename S>
        FunctionDesc DescribeMember(S signature, const std::string &name, const Doc &doc, bool isStatic, Side side) const {
            FunctionDesc desc = Detail::DescribeSignature(signature, _record->desc.name + "." + name, doc, isStatic, side);
            desc.name         = name;
            return desc;
        }

        FunctionDesc DescribeContextMember(const std::string &name, const Doc &doc, bool isStatic, Side side) const {
            FunctionDesc desc = Detail::DescribeContextFunction(_record->desc.name + "." + name, doc, isStatic, side);
            desc.name         = name;
            return desc;
        }

        template <typename G>
        static PropertyDesc DescribeProperty(const std::string &name, std::string description, bool readonly, Side side) {
            static_assert(Detail::CallableOn<T, G>, "Bind::Class::Property: the getter is a member function of T or of one of its bases, or a function or a lambda that takes the receiver first, as T &, const T & or T *");
            PropertyDesc desc;
            desc.name        = name;
            desc.type        = TypeOf<typename Detail::MethodShapeOf<G>::Return>();
            desc.description = std::move(description);
            desc.readonly    = readonly;
            desc.side        = side;
            return desc;
        }

        template <typename G>
        static Detail::LuaThunk GetterThunk(G getter) {
            static_assert(Detail::MethodShapeOf<G>::arity == 0 && !std::is_void_v<typename Detail::MethodShapeOf<G>::Return>, "Bind::Class::Property: a getter takes nothing and returns the value");
            return Detail::MethodShapeOf<G>::template Thunk<T, G>(std::move(getter));
        }

        std::shared_ptr<Detail::LuaClass> _record;
        std::shared_ptr<Detail::V8Class<T>> _v8;

        static inline const Class *_registered = nullptr;
    };
} // namespace Framework::Scripting::Bind
