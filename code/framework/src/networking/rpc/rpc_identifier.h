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

// RPC identifiers are built in two stages so that a prebuilt Framework and a mod can still agree on
// every slot key. At compile time the readable name is digested under a fixed key (FW_RPC_IDENTIFIER),
// so the name itself never reaches the binary. At runtime NetworkPeer mixes that digest with the
// per-deployment salt it was handed (InstanceOptions::rpcIdentifierSalt) and renders the token RPC4
// keys its slots on. Both peers must hold the same salt or no RPC ever dispatches; NetworkPeer::
// BuildToken folds IdentifierSaltTag(salt) into the TwoWayAuthentication challenge so a mismatch fails
// the connection gate instead of hanging silently. Changing any of this is netcode-breaking (MAJOR).
namespace Framework::Networking::RPC {
    // The salt every peer uses unless the mod sets one; keeps local development builds interoperable.
    inline constexpr std::uint64_t kDefaultIdentifierSalt = 0x9E3779B97F4A7C15ULL;

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
        constexpr std::uint64_t Rotl(std::uint64_t x, int b) {
            return (x << b) | (x >> (64 - b));
        }

        struct SipState {
            std::uint64_t v0, v1, v2, v3;

            constexpr void Round() {
                v0 += v1;
                v1 = Rotl(v1, 13);
                v1 ^= v0;
                v0 = Rotl(v0, 32);
                v2 += v3;
                v3 = Rotl(v3, 16);
                v3 ^= v2;
                v0 += v3;
                v3 = Rotl(v3, 21);
                v3 ^= v0;
                v2 += v1;
                v1 = Rotl(v1, 17);
                v1 ^= v2;
                v2 = Rotl(v2, 32);
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
                for (std::size_t i = 0; i < 8; ++i) {
                    block |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data[offset + i])) << (8 * i);
                }
                s.Absorb(block);
            }

            std::uint64_t tail = static_cast<std::uint64_t>(length & 0xFF) << 56;
            for (std::size_t i = 0; offset + i < length; ++i) {
                tail |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data[offset + i])) << (8 * i);
            }
            s.Absorb(tail);

            s.v2 ^= 0xFF;
            s.Round();
            s.Round();
            s.Round();
            s.Round();
            return s.v0 ^ s.v1 ^ s.v2 ^ s.v3;
        }

        // Fixed key for the compile-time stage. It hides nothing (the names are public in this
        // repository anyway); it only keeps the names out of the binary.
        inline constexpr std::uint64_t kNameKey0 = 0x4D6166696148756CULL;
        inline constexpr std::uint64_t kNameKey1 = 0x4672616D65776F72ULL;
    } // namespace detail

    // Compile-time digest of a readable RPC name. What a payload's kIdentifier holds.
    struct Identifier {
        std::uint64_t digest = 0;

        constexpr bool operator==(const Identifier &) const = default;
    };

    constexpr Identifier DigestName(std::string_view name) {
        return Identifier {detail::SipHash24(detail::kNameKey0, detail::kNameKey1, name)};
    }

    // Keyed PRF of the digest under the deployment salt. Deliberately not a salted FNV-1a: that is
    // invertible (multiply by the prime's modular inverse and unwind backwards), so one token plus its
    // digest would recover the salt and with it the whole table, which would leave the per-deployment
    // rotation worthless. A keyed PRF has no such shortcut.
    constexpr std::uint64_t HashIdentifierWith(std::uint64_t salt, Identifier identifier) {
        char bytes[8] = {};
        for (std::size_t i = 0; i < 8; ++i) {
            bytes[i] = static_cast<char>(static_cast<std::uint8_t>(identifier.digest >> (8 * i)));
        }
        return detail::SipHash24(SplitMix64(salt), SplitMix64(SplitMix64(salt)), std::string_view(bytes, 8));
    }

    // Stable, NUL-terminated 16-character rendering of a hashed identifier (4 bits per character).
    // RPC4 keys its slots by, and transmits, identifiers as C strings, so this drops in wherever a
    // readable name used to sit.
    struct Token {
        std::array<char, 17> value {};

        constexpr const char *c_str() const {
            return value.data();
        }
    };

    constexpr Token RenderToken(std::uint64_t hash) {
        Token out {};
        for (int i = 15; i >= 0; --i) {
            out.value[static_cast<std::size_t>(i)] = kIdentifierAlphabet[hash & 0xFULL];
            hash >>= 4;
        }
        out.value[16] = '\0';
        return out;
    }

    constexpr Token TokenFor(std::uint64_t salt, Identifier identifier) {
        return RenderToken(HashIdentifierWith(salt, identifier));
    }

    // The name reaches this exclusively through FW_RPC_IDENTIFIER's template argument (mandatory
    // constant evaluation), so it is never referenced at runtime and does not ship in the binary --
    // only the digest does. 64 bits keeps collisions across the identifier set negligible.
    template <std::uint64_t Digest>
    struct IdentifierOf {
        static constexpr Identifier value {Digest};
    };

    // Fingerprint of the salt, folded into NetworkPeer::BuildToken. A salt mismatch otherwise has no
    // error path: RPC4 Signal to an unregistered slot is a no-op, so the peers would connect and then
    // silently dispatch nothing.
    constexpr Token IdentifierSaltTag(std::uint64_t salt) {
        return TokenFor(salt, IdentifierOf<DigestName("Framework::IdentifierSalt").digest>::value);
    }
} // namespace Framework::Networking::RPC

// Turn a readable RPC name into its compile-time digest. Use in place of a string literal, e.g.
// `static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Framework::ChatMessage");`. The readable
// name stays here for developers; only the digest reaches the binary, and only the salted token
// reaches the wire.
#define FW_RPC_IDENTIFIER(name) (::Framework::Networking::RPC::IdentifierOf<::Framework::Networking::RPC::DigestName(name).digest>::value)
