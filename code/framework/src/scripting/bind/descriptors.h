/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "call.h"
#include "doc.h"
#include "types.h"

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Framework::Scripting::Bind {
    struct ParamDesc {
        std::string name;
        TypeRef type;
        bool optional = false;
        std::string description;
        bool rest = false; // the last parameter of a context function, taking every remaining argument
    };

    struct FunctionDesc {
        std::string name;
        std::string description;
        std::vector<ParamDesc> params;
        TypeRef returns;
        std::string returnsDescription;
        bool isStatic = false;
        Side side     = Side::Shared;
    };

    struct PropertyDesc {
        std::string name;
        TypeRef type;
        std::string description;
        bool readonly = false;
        bool isStatic = false;
        Side side     = Side::Shared;
    };

    // A bound class, or a namespace of functions (isNamespace) such as a service object.
    struct ClassDesc {
        std::string name;
        std::string description;
        std::vector<std::string> bases;
        bool isNamespace = false;
        // Which runtimes have the definition at all. A Shared one is everywhere, with the members of each side; one
        // of a side is nowhere else, whatever the sides of its members.
        Side side = Side::Shared;
        std::optional<FunctionDesc> constructor;
        std::vector<FunctionDesc> functions;
        std::vector<PropertyDesc> properties;
    };

    namespace Detail {
        // What a callable takes and returns. Receiver is the class of a member function, void for anything else.
        template <typename Receiver, typename Return, typename... Args>
        struct Signature {};

        template <typename F, typename = void>
        struct SignatureOf;

        template <typename R, typename... Args>
        struct SignatureOf<R (*)(Args...), void> {
            using Type = Signature<void, R, Args...>;
        };
        template <typename R, typename... Args>
        struct SignatureOf<R (*)(Args...) noexcept, void> {
            using Type = Signature<void, R, Args...>;
        };
        template <typename C, typename R, typename... Args>
        struct SignatureOf<R (C::*)(Args...), void> {
            using Type = Signature<C, R, Args...>;
        };
        template <typename C, typename R, typename... Args>
        struct SignatureOf<R (C::*)(Args...) const, void> {
            using Type = Signature<C, R, Args...>;
        };
        template <typename C, typename R, typename... Args>
        struct SignatureOf<R (C::*)(Args...) noexcept, void> {
            using Type = Signature<C, R, Args...>;
        };
        template <typename C, typename R, typename... Args>
        struct SignatureOf<R (C::*)(Args...) const noexcept, void> {
            using Type = Signature<C, R, Args...>;
        };

        // A lambda or another function object is described by its call operator, which has no script receiver.
        template <typename Receiver, typename Return, typename... Args>
        Signature<void, Return, Args...> WithoutReceiver(Signature<Receiver, Return, Args...>);

        template <typename F>
        struct SignatureOf<F, std::void_t<decltype(&F::operator())>> {
            using Type = decltype(WithoutReceiver(typename SignatureOf<decltype(&F::operator())>::Type {}));
        };

        template <typename F>
        concept HasSignature = requires { typename SignatureOf<F>::Type; };

        template <typename Arg>
        ParamDesc DescribeParam() {
            using Value = std::remove_cvref_t<Arg>;
            ParamDesc param;
            if constexpr (IsOptional<Value>::value) {
                param.optional = true;
                param.type     = TypeOf<typename Value::value_type>();
            }
            else {
                param.type = TypeOf<Arg>();
            }
            return param;
        }

        template <typename Receiver, typename Return>
        TypeRef DescribeReturn() {
            if constexpr (!std::is_void_v<Receiver> && std::is_same_v<Return, std::add_lvalue_reference_t<Receiver>>) {
                return Type::This();
            }
            else {
                return TypeOf<Return>();
            }
        }

        // Merges what the signature says (types, std::optional) with what the Doc says (names, descriptions,
        // overrides). Throws std::logic_error when the two disagree on the number of parameters, and when the Doc
        // has a rest parameter, which a signature cannot take.
        FunctionDesc BuildFunctionDesc(std::string name, const Doc &doc, std::vector<ParamDesc> params, TypeRef returns, bool isStatic, Side side);

        // Describes a context function, from its Doc alone: the parameters with the types the Doc gives them, and
        // the return type it gives, void without one. Throws std::logic_error when a parameter has no type.
        FunctionDesc DescribeContextFunction(std::string name, const Doc &doc, bool isStatic, Side side);

        template <typename Receiver, typename Return, typename... Args>
        FunctionDesc DescribeSignature(Signature<Receiver, Return, Args...>, std::string name, const Doc &doc, bool isStatic, Side side) {
            return BuildFunctionDesc(std::move(name), doc, {DescribeParam<Args>()...}, DescribeReturn<Receiver, Return>(), isStatic, side);
        }
    } // namespace Detail

    // Describes a typed callable `F`: a member function pointer (the receiver is not a parameter), a free function
    // or a lambda (every parameter counts). Types come from the signature, everything else from `doc`. A context
    // function, a NativeFunction or what makes one, is described from `doc` alone.
    template <typename F>
    FunctionDesc DescribeFunction(std::string name, const Doc &doc, bool isStatic, Side side) {
        if constexpr (Detail::ContextCallable<std::decay_t<F>>) {
            return Detail::DescribeContextFunction(std::move(name), doc, isStatic, side);
        }
        else if constexpr (Detail::HasSignature<std::decay_t<F>>) {
            return Detail::DescribeSignature(typename Detail::SignatureOf<std::decay_t<F>>::Type {}, std::move(name), doc, isStatic, side);
        }
        else {
            static_assert(Detail::HasSignature<std::decay_t<F>>, "Bind::DescribeFunction: F must be a function, a member function pointer or a function object with a single call operator");
            return {};
        }
    }

    // Describes a constructor taking `Args`. It is named "constructor" and returns Void.
    template <typename... Args>
    FunctionDesc DescribeConstructor(const Doc &doc) {
        return Detail::DescribeSignature(Detail::Signature<void, void, Args...> {}, "constructor", doc, false, Side::Shared);
    }

    // "shared", "server" or "client", for a message.
    std::string_view SideName(Side side);

    // Whether a runtime of side `runtime` has the class or namespace: the side the definition declares is Shared or
    // is that one. The sides of its members, its constructor and its bases say nothing of it.
    bool ExistsIn(const ClassDesc &desc, Side runtime);

    // Every description built in this process, in the order it was recorded. Not thread-safe: definitions are built
    // and read on the thread that sets a runtime up.
    class ApiCatalog final {
      public:
        // The process-wide catalog. Safe to reach during static initialisation.
        static ApiCatalog &Get();

        // Keeps `desc`, which must outlive the catalog. Recording the same description again does nothing.
        void Record(const ClassDesc *desc);

        const std::vector<const ClassDesc *> &Entries() const {
            return _entries;
        }

        // The first recorded description named `name`, or nullptr.
        const ClassDesc *Find(std::string_view name) const;

        // The first recorded description named `name` that a runtime of side `runtime` has (ExistsIn), or nullptr.
        // Two builtins may share a global name when no runtime has both, as the Chat of the server and the Chat
        // of the client do: each is its own definition and declares its side, and registering it in a runtime
        // of the other side does nothing.
        const ClassDesc *Find(std::string_view name, Side runtime) const;

      private:
        std::vector<const ClassDesc *> _entries;
    };
} // namespace Framework::Scripting::Bind
