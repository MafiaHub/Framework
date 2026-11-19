/*
 * MafiaHub source-available license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE.txt file in the source repository for information regarding licensing.
 */

#pragma once

#include "rpc/client_identity.h"

#include <utils/crypto.h>
#include <utils/token_bucket.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

// A player's account is a key pair the client keeps, not an identifier it reports. Every id in
// ClientIdentity (Steam, Discord, Epic, hardware) is public or editable, so a server that keys an
// account on one hands that account to whoever types it in. Instead the client signs its connection
// request with its account key, and the server derives the account id from the public key whose
// signature it checked. Nobody can produce that id without the private key.
//
// What this does not do: tie the account to a Steam, Discord or Epic account (they stay claims), or
// stop a server operator who relays a player's live connection to another server, which needs an
// encrypted transport to bind the signature to.
namespace Framework::Networking {
    // The account a public key stands for: 32 lowercase hex characters (the first 128 bits of its
    // SHA-256). Empty for a key of the wrong length.
    std::string AccountIdOf(std::string_view publicKey);

    // A key path for a message or a log line, in UTF-8 whatever the user's name: path::string()
    // throws on MSVC outside the ANSI code page.
    std::string AccountKeyPathText(const std::filesystem::path &path);

    // The client's half: the private key, kept in a file of its own.
    class AccountKey final {
      public:
        // Reads the key at path, creating it (and its folder) when the file does not exist. Nullopt
        // with outError set when the file exists but holds no key, or cannot be read or written: a
        // damaged key is the player's account, so it is never replaced on its own.
        static std::optional<AccountKey> LoadOrCreate(const std::filesystem::path &path, std::string &outError);

        // Nullopt when OpenSSL refuses the seed.
        static std::optional<AccountKey> FromSeed(const Utils::Crypto::SigningSeed &seed);

        AccountKey(const AccountKey &other)            = default;
        AccountKey &operator=(const AccountKey &other) = default;
        ~AccountKey();

        // Fills identity's publicKey, issuedAt, nonce and signature. Sign last: any field written
        // after it breaks the signature. False when no random nonce or no signature could be made.
        bool Sign(RPC::ClientIdentity &identity, uint64_t nowUnix) const;

        const std::string &AccountId() const {
            return _accountId;
        }

      private:
        AccountKey() = default;

        Utils::Crypto::SigningSeed _seed {};
        Utils::Crypto::PublicKey _publicKey {};
        std::string _accountId;
    };

    enum class AccountProofResult {
        Valid,
        Missing,      // no key or no signature: a client that cannot prove an account
        Malformed,    // fields of the wrong size
        BadSignature, // signed by someone else, or changed on the way
        ClockAhead,   // issued further in the future than the skew allows
        ClockBehind,  // issued longer ago than the skew allows
        Replayed,     // this exact request was already let through
        Busy,         // every remembered request is still live: taking another would forget one
        RateLimited,  // the request's address asked too often (AccountProofVerifier::Admit)
    };

    // The server's half. Thread-safe.
    class AccountProofVerifier final {
      public:
        // How far a client's clock may be from the server's. Also how long a request stays usable,
        // and so how long its nonce is remembered.
        static constexpr uint64_t kMaxClockSkewSeconds = 300;

        // Requests remembered by default. Only requests with a valid signature are remembered, each
        // until it could no longer pass the clock check. A request is never forgotten earlier, since
        // that would let it in again: a verifier full of live ones refuses new requests as Busy.
        static constexpr std::size_t kMaxRemembered = 65536;

        // Requests one address may start at once, then a second. A request is cheap to sign with a new
        // key and nonce, so without this one address could fill the remembered requests and leave
        // every other player Busy. At these rates one address holds at most ~140 live requests.
        static constexpr double kSourceBurst = 16.0;
        static constexpr double kSourceRate  = 0.2;

        // Addresses tracked at most. Past it, addresses idle long enough to be full again are dropped;
        // if that frees nothing, new addresses are refused until it does.
        static constexpr std::size_t kMaxSources = 16384;

        explicit AccountProofVerifier(std::size_t capacity = kMaxRemembered, double sourceRate = kSourceRate, double sourceBurst = kSourceBurst): _capacity(capacity), _sourceRate(sourceRate), _sourceBurst(sourceBurst) {}

        // Called with the request's address before Verify, which it spares: false refuses the request
        // as RateLimited, before its signature is checked or it is remembered.
        bool Admit(const std::string &source, Utils::TokenBucket::Clock::time_point now = Utils::TokenBucket::Clock::now());

        AccountProofResult Verify(const RPC::ClientIdentity &identity, uint64_t nowUnix);

        // What the player reads when result refuses them.
        static std::string RefusalFor(AccountProofResult result, const RPC::ClientIdentity &identity, uint64_t nowUnix);

      private:
        void Forget(uint64_t nowUnix);

        struct Source {
            Utils::TokenBucket bucket;
            Utils::TokenBucket::Clock::time_point lastSeen;
        };

        const std::size_t _capacity;
        const double _sourceRate;
        const double _sourceBurst;
        std::mutex _mutex;
        std::unordered_map<std::string, Source> _sources;
        std::unordered_set<std::string> _seen;
        std::multimap<uint64_t, std::string> _byExpiry; // the last second each request could pass
    };
} // namespace Framework::Networking
