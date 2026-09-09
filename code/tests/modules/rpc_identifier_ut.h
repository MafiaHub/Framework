/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "integrations/shared/rpc/emit_script_event.h"
#include "networking/rpc/chat_message.h"
#include "networking/rpc/client_identity.h"
#include "networking/rpc/resource_refresh.h"
#include "networking/rpc/rpc_identifier.h"
#include "networking/rpc/server_resources.h"
#include "networking/rpc/voice_settings.h"

#include <mafianet/BitStream.h>
#include <mafianet/StringCompressor.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

// RPC identifiers are derived at compile time from a keyed hash of the readable name (see
// rpc_identifier.h): the wire and the binary carry only an opaque token, never "Framework::ChatMessage".
// These tests pin the properties the RPC layer depends on -- a correct keyed hash, opacity,
// distinctness, determinism, non-recoverability of the salt, and that the token never costs more on
// the wire than the name it replaced.
MODULE(rpc_identifier, {
    namespace RPC = Framework::Networking::RPC;

    // Every readable name converted to FW_RPC_IDENTIFIER, paired with the token that ships instead.
    // Kept in sync by hand: a new framework RPC belongs here so the sweeps below cover it.
    struct Identifier {
        const char *name;
        const char *token;
    };
    static const Identifier kIdentifiers[] = {
        {"Framework::ClientIdentity", RPC::ClientIdentity::kIdentifier},
        {"Framework::ChatMessage", RPC::ChatMessage::kIdentifier},
        {"Framework::ServerResources", RPC::ServerResources::kIdentifier},
        {"Framework::ResourceRefresh", RPC::ResourceRefresh::kIdentifier},
        {"Framework::ResourceStop", RPC::ResourceStop::kIdentifier},
        {"Framework::VoiceSettings", RPC::VoiceSettings::kIdentifier},
        {"Framework::VoiceSpeakerRange", RPC::VoiceSpeakerRange::kIdentifier},
        {"Framework::VoicePreference", RPC::VoicePreference::kIdentifier},
        {"Framework::EmitScriptEvent", Framework::Integrations::Shared::RPC::EmitScriptEvent::kIdentifier},
        {"Framework::ForceState", FW_RPC_IDENTIFIER("Framework::ForceState")},
        {"Framework::SetOwner", FW_RPC_IDENTIFIER("Framework::SetOwner")},
    };
    static constexpr size_t kIdentifierCount = sizeof(kIdentifiers) / sizeof(kIdentifiers[0]);

    // Bits WriteCompressed spends on a C string, i.e. exactly what RPC4::Signal pays to name the slot.
    const auto encodedBits = [](const char *value) {
        MafiaNet::BitStream bs;
        bs.WriteCompressed(value);
        return static_cast<uint32_t>(bs.GetNumberOfBitsUsed());
    };

    IT("matches the published SipHash-2-4 test vectors", {
        // The identifier hash is only as good as the primitive under it, and this one is hand-rolled
        // for constexpr. Reference key 000102..0f over inputs 00..len-1, the standard vectors.
        static const uint64_t expected[16] = {
            0x726fdb47dd0e0e31ULL, 0x74f839c593dc67fdULL, 0x0d6c8009d9a94f5aULL, 0x85676696d7fb7e2dULL,
            0xcf2794e0277187b7ULL, 0x18765564cd99a68dULL, 0xcbc9466e58fee3ceULL, 0xab0200f58b01d137ULL,
            0x93f5f5799a932462ULL, 0x9e0082df0ba9e4b0ULL, 0x7a5dbbc594ddb9f3ULL, 0xf4b32f46226bada7ULL,
            0x751e8fbc860ee5fbULL, 0x14ea5627c0843d90ULL, 0xf723ca908e7af2eeULL, 0xa129ca6149be45e5ULL,
        };
        char input[16] = {0};
        for (size_t length = 0; length < 16; ++length) {
            input[length] = static_cast<char>(length);
            UEQUALS(RPC::detail::SipHash24(0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL, std::string_view(input, length)), expected[length]);
        }
    });

    IT("renders an opaque 16-character token that does not embed the readable name", {
        for (const auto &entry : kIdentifiers) {
            EQUALS(strlen(entry.token), static_cast<size_t>(16));
            EQUALS(strspn(entry.token, RPC::kIdentifierAlphabet), static_cast<size_t>(16));
            EQUALS(strstr(entry.token, "Framework") == nullptr, true);
            EQUALS(strstr(entry.name, entry.token) == nullptr, true);
        }
    });

    IT("draws tokens from an alphabet of sixteen distinct characters", {
        // Fewer than sixteen would silently fold hash nibbles together and lose entropy.
        EQUALS(strlen(RPC::kIdentifierAlphabet), static_cast<size_t>(16));
        for (size_t i = 0; i < 16; ++i) {
            for (size_t j = i + 1; j < 16; ++j) { NEQUALS(RPC::kIdentifierAlphabet[i], RPC::kIdentifierAlphabet[j]); }
        }
    });

    IT("assigns a distinct identifier to every framework RPC", {
        // A collision would make two payloads share an RPC4 slot, silently misrouting one of them.
        for (size_t i = 0; i < kIdentifierCount; ++i) {
            for (size_t j = i + 1; j < kIdentifierCount; ++j) { STRNEQUALS(kIdentifiers[i].token, kIdentifiers[j].token); }
        }
    });

    IT("derives the token as the alphabet rendering of the name's hash", {
        // Ties the on-wire token to the hash, so an independent implementation can reproduce it.
        for (const auto &entry : kIdentifiers) {
            char expected[17];
            uint64_t value = RPC::HashIdentifier(entry.name);
            for (int i = 15; i >= 0; --i) {
                expected[i] = RPC::kIdentifierAlphabet[value & 0xFULL];
                value >>= 4;
            }
            expected[16] = '\0';
            STREQUALS(entry.token, expected);
        }
    });

    IT("derives the same identifier for a given name every time", {
        // Both peers must agree, so the mapping has to be a pure function of name + salt.
        UEQUALS(RPC::HashIdentifier("Framework::ChatMessage"), RPC::HashIdentifier("Framework::ChatMessage"));
        UEQUALS(RPC::HashIdentifierWith(1234ULL, "Framework::ChatMessage"), RPC::HashIdentifierWith(1234ULL, "Framework::ChatMessage"));
        NEQUALS(RPC::HashIdentifier("Framework::ChatMessage"), RPC::HashIdentifier("Framework::ClientIdentity"));
    });

    IT("rotates every identifier when the build salt changes", {
        // The point of the salt: a table lifted from one build must not map onto the next.
        for (const auto &entry : kIdentifiers) {
            const uint64_t a = RPC::HashIdentifierWith(0x0123456789ABCDEFULL, entry.name);
            const uint64_t b = RPC::HashIdentifierWith(0x0123456789ABCDF0ULL, entry.name);
            NEQUALS(a, b);
            // A near-identical salt must still scatter the output, not nudge it.
            uint64_t differing = 0;
            for (uint64_t bit = a ^ b; bit; bit >>= 1) { differing += bit & 1ULL; }
            GREATER(differing, static_cast<uint64_t>(16));
        }
    });

    IT("scatters the hash when a single character of the name changes", {
        const uint64_t a = RPC::HashIdentifier("Framework::ChatMessage");
        const uint64_t b = RPC::HashIdentifier("Framework::ChatMessagf");
        uint64_t differing = 0;
        for (uint64_t bit = a ^ b; bit; bit >>= 1) { differing += bit & 1ULL; }
        GREATER(differing, static_cast<uint64_t>(16));
    });

    IT("does not leak the build salt to anyone holding a token and its public name", {
        // Regression guard. A salted FNV-1a token is invertible: multiply by the prime's modular
        // inverse and unwind the name backwards to recover the seed, hence the salt. Every framework
        // name is public, so that alone would have undone the rotation above. Run exactly that attack
        // against the current construction and require it to fail.
        const uint64_t salt         = 0xDEADBEEFCAFEF00DULL;
        const uint64_t fnvBasis     = 14695981039346656037ULL;
        const uint64_t fnvInvPrime  = 0xCE965057AFF6957BULL; // modular inverse of the FNV prime mod 2^64
        const std::string_view name = "Framework::ChatMessage";

        uint64_t state = RPC::HashIdentifierWith(salt, name);
        for (size_t i = name.size(); i-- > 0;) {
            state *= fnvInvPrime;
            state ^= static_cast<uint8_t>(name[i]);
        }
        const uint64_t recovered = state ^ fnvBasis;
        NEQUALS(recovered, salt);
    });

    IT("never costs more on the wire than the readable name it replaced", {
        // RPC4::Signal writes the identifier with WriteCompressed, Huffman-coded against RakNet's
        // fixed English table. Hex tokens cost ~151 bits there, above most of these names; the
        // alphabet in rpc_identifier.h is chosen to stay under every one of them.
        MafiaNet::StringCompressor::AddReference();
        for (const auto &entry : kIdentifiers) { LESSER(encodedBits(entry.token), encodedBits(entry.name)); }
        MafiaNet::StringCompressor::RemoveReference();
    });

    IT("resolves identifiers entirely at compile time", {
        // The readable name must never reach the binary, which only holds while it stays inside a
        // constant expression.
        static_assert(RPC::HashIdentifier("Framework::ChatMessage") == RPC::HashIdentifierWith(RPC::kIdentifierSalt, "Framework::ChatMessage"));
        static_assert(RPC::HashIdentifier("Framework::ChatMessage") != RPC::HashIdentifier("Framework::ClientIdentity"));
        static_assert(RPC::IdentifierToken<RPC::HashIdentifier("Framework::ChatMessage")>::value[16] == '\0');
        static_assert(RPC::detail::SipHash24(0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL, std::string_view("", 0)) == 0x726fdb47dd0e0e31ULL);
        EQUALS(true, true);
    });
});
