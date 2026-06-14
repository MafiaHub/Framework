/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <string>

#include <utils/hashing.h>
#include <utils/type_name.h>

namespace Framework::Networking::RPC {
    // Per-type RPC identity derived from a compiler-independent type name (see
    // Utils::TypeName — NOT typeid().name(), which differs MSVC vs GCC and breaks
    // cross-platform RPC routing). Both values are cached in function-local
    // statics so the name string and its CRC32 are computed once per type rather
    // than on every RPC construction, and the logic lives in one place shared by
    // IRPC and IGameRPC.
    template <typename T>
    inline uint32_t RPCHash() {
        static const uint32_t hash = Utils::Hashing::CalculateCRC32(Utils::TypeName<T>().data(), Utils::TypeName<T>().size());
        return hash;
    }

    template <typename T>
    inline const std::string &RPCName() {
        static const std::string name {Utils::TypeName<T>()};
        return name;
    }
} // namespace Framework::Networking::RPC
