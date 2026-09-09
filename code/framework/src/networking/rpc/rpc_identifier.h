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

// Per-build key for every RPC identifier. Both peers derive the token they key RPC4 slots on from it,
// so a mismatch means no RPC ever dispatches -- NetworkPeer::BuildToken folds IdentifierSaltTag() into
// the TwoWayAuthentication challenge so that fails the connection gate instead of hanging silently.
// Changing it is netcode-breaking (MAJOR). Release CI overrides it per shipped build with
// `cmake -DFW_RPC_IDENTIFIER_SALT=<uint64>`, which code/framework/CMakeLists.txt turns into this
// define (a bare cache variable would never reach the preprocessor); the default keeps local dev
// builds deterministic and interoperable.
#ifndef FW_RPC_IDENTIFIER_SALT
#define FW_RPC_IDENTIFIER_SALT 0x9E3779B97F4A7C15ULL
#endif

namespace Framework::Networking::RPC {
    inline constexpr std::uint64_t kIdentifierSalt = FW_RPC_IDENTIFIER_SALT;

    // Token alphabet, in ascending RakNet StringCompressor cost. RPC4 writes the identifier with
    // WriteCompressed, i.e. Huffman-coded against RakNet's fixed English table, where digits cost
    // 10-16 bits and these letters 3-6. A hex rendering would put the token at ~151 bits, above the
    // readable names it replaces (110-158); these 16 cap it at 96.
    inline constexpr char kIdentifierAlphabet[] = "eaintoshlrudbcfg";

    constexpr std::uint64_t SplitMix64(std::uint64_t x) {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    namespace detail {
        constexpr std::uint64_t Rotl(std::uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }

        struct SipState {
            std::uint64_t v0, v1, v2, v3;

            constexpr void Round() {
                v0 += v1; v1 = Rotl(v1, 13); v1 ^= v0; v0 = Rotl(v0, 32);
                v2 += v3; v3 = Rotl(v3, 16); v3 ^= v2;
                v0 += v3; v3 = Rotl(v3, 21); v3 ^= v0;
                v2 += v1; v1 = Rotl(v1, 17); v1 ^= v2; v2 = Rotl(v2, 32);
            }

            constexpr void Absorb(std::uint64_t m) {
                v3 ^= m;
                Round();
                Round();
                v0 ^= m;
            }
        };

        // SipHash-2-4, matching the reference implementation (pinned against its published test
        // vectors in code/tests/modules/rpc_identifier_ut.h).
        constexpr std::uint64_t SipHash24(std::uint64_t k0, std::uint64_t k1, std::string_view data) {
            SipState s {k0 ^ 0x736F6D6570736575ULL, k1 ^ 0x646F72616E646F6DULL, k0 ^ 0x6C7967656E657261ULL, k1 ^ 0x7465646279746573ULL};

            const std::size_t length = data.size();
            std::size_t offset       = 0;
            for (; offset + 8 <= length; offset += 8) {
                std::uint64_t block = 0;
                for (std::size_t i = 0; i < 8; ++i) { block |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data[offset + i])) << (8 * i); }
                s.Absorb(block);
            }

            std::uint64_t tail = static_cast<std::uint64_t>(length & 0xFF) << 56;
            for (std::size_t i = 0; offset + i < length; ++i) { tail |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data[offset + i])) << (8 * i); }
            s.Absorb(tail);

            s.v2 ^= 0xFF;
            s.Round();
            s.Round();
            s.Round();
            s.Round();
            return s.v0 ^ s.v1 ^ s.v2 ^ s.v3;
        }
    } // namespace detail

    // SipHash-2-4 of the readable name under a key derived from the build salt. Deliberately not a
    // salted FNV-1a: that is invertible (multiply by the prime's modular inverse and unwind the name
    // backwards), so one token plus its name -- and every framework name is public in this repo --
    // recovers the salt and with it the whole table, which would leave the per-build rotation above
    // worthless. A keyed PRF has no such shortcut.
    constexpr std::uint64_t HashIdentifierWith(std::uint64_t salt, std::string_view name) { return detail::SipHash24(SplitMix64(salt), SplitMix64(SplitMix64(salt)), name); }

    // The name reaches this exclusively through FW_RPC_IDENTIFIER's template argument (mandatory
    // constant evaluation), so it is never referenced at runtime and does not ship in the binary --
    // only the token below does. 64 bits keeps collisions across the identifier set negligible.
    constexpr std::uint64_t HashIdentifier(std::string_view name) { return HashIdentifierWith(kIdentifierSalt, name); }

    // Stable, NUL-terminated 16-character rendering of a hashed identifier (4 bits per character),
    // held in static storage so it can back a plain const char*. RPC4 keys its slots by, and
    // transmits, identifiers as C strings, so this drops in wherever a readable name used to sit.
    template <std::uint64_t Hash>
    struct IdentifierToken {
        static constexpr std::array<char, 17> Render() {
            std::array<char, 17> out {};
            std::uint64_t value = Hash;
            for (int i = 15; i >= 0; --i) {
                out[static_cast<std::size_t>(i)] = kIdentifierAlphabet[value & 0xFULL];
                value >>= 4;
            }
            out[16] = '\0';
            return out;
        }
        static constexpr std::array<char, 17> value = Render();
    };

    // Fingerprint of the build salt, folded into NetworkPeer::BuildToken. A salt mismatch otherwise
    // has no error path: RPC4 Signal to an unregistered slot is a no-op, so the peers would connect
    // and then silently dispatch nothing.
    inline constexpr const char *IdentifierSaltTag() { return IdentifierToken<HashIdentifier("Framework::IdentifierSalt")>::value.data(); }
} // namespace Framework::Networking::RPC

// Turn a readable RPC name into an opaque, per-build wire identifier. Use in place of a string
// literal, e.g. `static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Framework::ChatMessage");`.
// The readable name stays here for developers; only the token reaches the binary and the wire.
#define FW_RPC_IDENTIFIER(name) (::Framework::Networking::RPC::IdentifierToken<::Framework::Networking::RPC::HashIdentifier(name)>::value.data())
