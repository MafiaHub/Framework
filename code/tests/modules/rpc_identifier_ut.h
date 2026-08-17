/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/rpc/chat_message.h"
#include "networking/rpc/client_identity.h"
#include "networking/rpc/rpc_identifier.h"
#include "networking/rpc/server_resources.h"
#include "networking/rpc/voice_settings.h"

#include <cstdint>
#include <cstring>
#include <string_view>

// RPC identifiers are derived at compile time from a salted hash of the readable name (see
// rpc_identifier.h): the wire and the binary carry only an opaque token, never "Framework::ChatMessage".
// These tests pin the properties the RPC layer depends on — opacity, per-name distinctness,
// determinism, and that the build salt actually participates so a bump rotates every identifier.
MODULE(rpc_identifier, {
    namespace RPC = Framework::Networking::RPC;

    IT("renders an opaque 16-char hex token that does not embed the readable name", {
        const char *id = RPC::ChatMessage::kIdentifier;
        EQUALS(strlen(id), static_cast<size_t>(16));
        EQUALS(strspn(id, "0123456789abcdef"), static_cast<size_t>(16)); // hex only
        EQUALS(strstr(id, "Framework") == nullptr, true);                // name does not leak
        EQUALS(strstr(id, "ChatMessage") == nullptr, true);
    });

    IT("assigns distinct identifiers to distinct payloads", {
        STRNEQUALS(RPC::ChatMessage::kIdentifier, RPC::ClientIdentity::kIdentifier);
        STRNEQUALS(RPC::ServerResources::kIdentifier, RPC::VoicePreference::kIdentifier);
        STRNEQUALS(RPC::ClientIdentity::kIdentifier, RPC::ServerResources::kIdentifier);
    });

    IT("derives the same identifier for a given name every time", {
        // Both peers must agree, so the mapping has to be a pure function of name + salt.
        EQUALS(RPC::HashIdentifier("Framework::ChatMessage"), RPC::HashIdentifier("Framework::ChatMessage"));
        NEQUALS(RPC::HashIdentifier("Framework::ChatMessage"), RPC::HashIdentifier("Framework::ClientIdentity"));
    });

    IT("renders the token as the lowercase hex of the identifier hash", {
        // Ties the on-wire token to the hash: the const char* both peers key on is exactly the hex of
        // HashIdentifier(name), so an independent implementation can reproduce it.
        const std::uint64_t hash = RPC::HashIdentifier("Framework::ChatMessage");
        char expected[17];
        static const char digits[] = "0123456789abcdef";
        std::uint64_t value = hash;
        for (int i = 15; i >= 0; --i) {
            expected[i] = digits[value & 0xFULL];
            value >>= 4;
        }
        expected[16] = '\0';
        STREQUALS(RPC::ChatMessage::kIdentifier, expected);
    });

    IT("mixes the build salt into the hash so a salt bump rotates every identifier", {
        // Plain (unsalted) FNV-1a of the same name. The salted hash must differ, proving the salt is
        // actually folded in — which is what makes a name table recovered from one build worthless
        // against the next.
        const auto unsalted = [](std::string_view name) {
            std::uint64_t hash = 14695981039346656037ULL;
            for (const char c : name) {
                hash ^= static_cast<std::uint8_t>(c);
                hash *= 1099511628211ULL;
            }
            return hash;
        };
        NEQUALS(RPC::kIdentifierSalt, static_cast<std::uint64_t>(0)); // a zero salt would be a no-op
        NEQUALS(RPC::HashIdentifier("Framework::ChatMessage"), unsalted("Framework::ChatMessage"));
    });
});
