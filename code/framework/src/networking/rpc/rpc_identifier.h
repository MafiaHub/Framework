/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Per-build salt mixed into every RPC identifier hash. Bumping it re-derives every identifier, so a
// captured trace or a reverse-engineered name table from one build no longer maps onto the next.
// Client and server MUST agree on the value (they exchange the derived token, see below), so a change
// here is a netcode-breaking (MAJOR) change. Release CI can override it per shipped build with
// -DFW_RPC_IDENTIFIER_SALT=<uint64> so the mapping rotates without editing source; the default keeps
// local dev builds deterministic and interoperable.
#ifndef FW_RPC_IDENTIFIER_SALT
    #define FW_RPC_IDENTIFIER_SALT 0x9E3779B97F4A7C15ULL
#endif

namespace Framework::Networking::RPC {
    inline constexpr std::uint64_t kIdentifierSalt = FW_RPC_IDENTIFIER_SALT;

    // 64-bit FNV-1a over the readable name, salted. The name is only ever an input to this constant
    // evaluation (it reaches this function exclusively through FW_RPC_IDENTIFIER's template argument),
    // so the human-readable string is not referenced at runtime and does not ship in release binaries
    // — only the opaque token below does. 64 bits keeps collisions across the identifier set
    // negligible.
    constexpr std::uint64_t HashIdentifier(std::string_view name) {
        std::uint64_t hash = 14695981039346656037ULL ^ kIdentifierSalt;
        for (const char c : name) {
            hash ^= static_cast<std::uint8_t>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    // A stable, NUL-terminated 16-char lowercase-hex rendering of a hashed identifier, held in static
    // storage so it can back a plain const char*. RPC4 keys its slots by, and transmits, identifiers
    // as C strings; rendering the hash as hex lets this drop in wherever a readable name used to sit
    // while only the opaque value travels on the wire and lives in the binary.
    template <std::uint64_t Hash>
    struct IdentifierToken {
        static constexpr std::array<char, 17> Render() {
            constexpr char digits[] = "0123456789abcdef";
            std::array<char, 17> out {};
            std::uint64_t value = Hash;
            for (int i = 15; i >= 0; --i) {
                out[static_cast<std::size_t>(i)] = digits[value & 0xFULL];
                value >>= 4;
            }
            out[16] = '\0';
            return out;
        }
        static constexpr std::array<char, 17> value = Render();
    };
} // namespace Framework::Networking::RPC

// Turn a readable RPC name into an opaque, per-build wire identifier. Use in place of a string
// literal, e.g. `static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Framework::ChatMessage");`.
// The readable name stays here for developers; only the hash token reaches the binary and the wire.
#define FW_RPC_IDENTIFIER(name)                                                                                        \
    (::Framework::Networking::RPC::IdentifierToken<::Framework::Networking::RPC::HashIdentifier(name)>::value.data())
