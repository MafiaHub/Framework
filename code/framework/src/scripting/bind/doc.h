/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Framework::Scripting::Bind {
    // Which runtime a member exists in.
    enum class Side : uint8_t {
        Shared,
        Server,
        Client
    };

    // Whether a member declared for `member` exists in a runtime of side `runtime`: a Shared member everywhere, any
    // other only on its own side.
    bool Materialises(Side member, Side runtime);

    // The documentation of one function: what the signature cannot say. Parameters are named in signature order.
    //
    // A context function has no signature to read, so its Doc says it all: every parameter is given with its type,
    // and the return type with ReturnType, void without it.
    class Doc final {
      public:
        struct ParamDoc {
            std::string name;
            std::string description;
            bool optional = false;
            std::optional<TypeRef> type; // given with the parameter or by ParamType; otherwise the signature decides
            bool rest = false;           // takes every remaining argument
        };

        explicit Doc(std::string summary);

        // Each of these throws std::logic_error after a rest parameter, which is the last one.
        Doc &Param(std::string name, std::string description);
        Doc &Param(std::string name, TypeRef type, std::string description);
        // Documents the parameter as optional whatever its C++ type is.
        Doc &OptionalParam(std::string name, std::string description);
        Doc &OptionalParam(std::string name, TypeRef type, std::string description);
        // The parameter that takes every remaining argument of a context function. `type` is the array they
        // arrive as in TypeScript ("unknown[]"); a type spelled by hand names one of them for Lua ("any").
        Doc &RestParam(std::string name, TypeRef type, std::string description);
        // Replaces the type derived from the signature for the parameter added last.
        Doc &ParamType(TypeRef type);
        Doc &Returns(std::string description);
        // Replaces the return type derived from the signature.
        Doc &ReturnType(TypeRef type);

        const std::string &Summary() const {
            return _summary;
        }

        const std::vector<ParamDoc> &Params() const {
            return _params;
        }

        const std::string &ReturnsDescription() const {
            return _returnsDescription;
        }

        const std::optional<TypeRef> &ReturnTypeOverride() const {
            return _returnType;
        }

      private:
        Doc &Add(ParamDoc param);

        std::string _summary;
        std::vector<ParamDoc> _params;
        std::string _returnsDescription;
        std::optional<TypeRef> _returnType;
    };
} // namespace Framework::Scripting::Bind
