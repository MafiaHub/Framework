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
    class Doc final {
      public:
        struct ParamDoc {
            std::string name;
            std::string description;
            bool optional = false;
            std::optional<TypeRef> type; // set by ParamType; otherwise the signature decides
        };

        explicit Doc(std::string summary);

        Doc &Param(std::string name, std::string description);
        // Documents the parameter as optional whatever its C++ type is.
        Doc &OptionalParam(std::string name, std::string description);
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
        std::string _summary;
        std::vector<ParamDoc> _params;
        std::string _returnsDescription;
        std::optional<TypeRef> _returnType;
    };
} // namespace Framework::Scripting::Bind
