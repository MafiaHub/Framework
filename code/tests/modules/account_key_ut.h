/*
 * MafiaHub source-available license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE.txt file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/account_key.h"
#include "networking/rpc/client_identity.h"
#include "utils/crypto.h"

#ifdef _WIN32
#include <utils/safe_win32.h>

#include <aclapi.h>
#endif

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

// A player's account is the key that signs their connection request. What has to hold: only that
// key's holder produces a request the server takes, any field changed on the way is caught, a
// request is good once and only for a few minutes, and the key on disk is never lost or replaced.
class AccountKeyTestRig {
  public:
    static Framework::Utils::Crypto::SigningSeed Seed(uint8_t fill) {
        Framework::Utils::Crypto::SigningSeed seed {};
        seed.fill(fill);
        return seed;
    }

    static Framework::Networking::RPC::ClientIdentity Identity() {
        Framework::Networking::RPC::ClientIdentity identity;
        identity.name      = "Zander";
        identity.discordId = "123456789012345678";
        identity.ticket    = "tkt";
        return identity;
    }

    // A folder of its own under the system temp directory, emptied on construction and removal.
    struct TempDir {
        std::filesystem::path path;
        TempDir() {
            path = std::filesystem::temp_directory_path() / ("fw_account_key_ut_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::remove_all(path);
        }
        ~TempDir() {
            std::error_code code;
            std::filesystem::remove_all(path, code);
        }
    };

    // Created owner-only: on Windows a protected DACL (nothing the folder grants is inherited) whose
    // every grant is to this user or SYSTEM; mode 0600 elsewhere, whatever the umask.
    static bool OwnerOnly(const std::filesystem::path &path) {
#ifdef _WIN32
        std::vector<uint8_t> user;
        {
            HANDLE token = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
                return false;
            }
            DWORD size = 0;
            GetTokenInformation(token, TokenUser, nullptr, 0, &size);
            user.resize(size);
            const bool ok = size > 0 && GetTokenInformation(token, TokenUser, user.data(), size, &size);
            CloseHandle(token);
            if (!ok) {
                return false;
            }
        }
        const PSID userSid = reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid;
        uint8_t system[SECURITY_MAX_SID_SIZE] {};
        DWORD systemSize = sizeof(system);
        if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &systemSize)) {
            return false;
        }

        PSECURITY_DESCRIPTOR descriptor = nullptr;
        PACL dacl                       = nullptr;
        if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS) {
            return false;
        }
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision                      = 0;
        bool ownerOnly                      = dacl != nullptr && GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED) != 0;
        bool userGranted                    = false;
        for (DWORD i = 0; ownerOnly && i < dacl->AceCount; ++i) {
            ACE_HEADER *ace = nullptr;
            if (!GetAce(dacl, i, reinterpret_cast<void **>(&ace))) {
                ownerOnly = false;
                break;
            }
            if (ace->AceType != ACCESS_ALLOWED_ACE_TYPE) {
                continue;
            }
            const PSID grantee = &reinterpret_cast<ACCESS_ALLOWED_ACE *>(ace)->SidStart;
            userGranted        = userGranted || EqualSid(grantee, userSid);
            ownerOnly          = EqualSid(grantee, userSid) || EqualSid(grantee, system);
        }
        LocalFree(descriptor);
        return ownerOnly && userGranted;
#else
        return std::filesystem::status(path).permissions() == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
#endif
    }

    static std::string ReadFile(const std::filesystem::path &path) {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
};

MODULE(account_key, {
    using Rig = AccountKeyTestRig;
    using Framework::Networking::AccountIdOf;
    using Framework::Networking::AccountKey;
    using Framework::Networking::AccountProofResult;
    using Framework::Networking::AccountProofVerifier;
    namespace Crypto = Framework::Utils::Crypto;

    constexpr uint64_t kNow = 1763500000;

    IT("signs and verifies Ed25519 as RFC 8032 specifies", {
        // RFC 8032 section 7.1, test 1: the empty message.
        Crypto::SigningSeed seed {};
        EQUALS(Crypto::FromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed.data(), seed.size()), true);
        Crypto::PublicKey publicKey {};
        EQUALS(Crypto::DerivePublicKey(seed, publicKey), true);
        STREQUALS(Crypto::ToHex(publicKey.data(), publicKey.size()).c_str(), "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");

        Crypto::Signature signature {};
        EQUALS(Crypto::Sign(seed, "", 0, signature), true);
        STREQUALS(Crypto::ToHex(signature.data(), signature.size()).c_str(),
            "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
        EQUALS(Crypto::Verify(publicKey, "", 0, signature), true);
        EQUALS(Crypto::Verify(publicKey, "x", 1, signature), false);
        signature[0] ^= 1;
        EQUALS(Crypto::Verify(publicKey, "", 0, signature), false);
    });

    IT("names an account by 32 hex characters of its public key, and nothing else", {
        const std::string key(32, '\x01');
        STREQUALS(AccountIdOf(key).c_str(), "72cd6e8422c407fb6d098690f1130b7d");
        EQUALS(AccountIdOf("").empty(), true);
        EQUALS(AccountIdOf(std::string(31, '\x01')).empty(), true);
        EQUALS(AccountIdOf(std::string(33, '\x01')).empty(), true);
    });

    IT("lets in a request its key signed, under that key's account", {
        const auto key = AccountKey::FromSeed(Rig::Seed(7));
        EQUALS(key.has_value(), true);
        auto identity = Rig::Identity();
        EQUALS(key->Sign(identity, kNow), true);
        EQUALS(identity.publicKey.size(), Framework::Networking::RPC::ClientIdentity::kPublicKeyLength);
        EQUALS(identity.signature.size(), Framework::Networking::RPC::ClientIdentity::kSignatureLength);

        // Through the wire format, as the server receives it.
        const auto received = Framework::Networking::RPC::ClientIdentity::Decode(identity.Encode());
        EQUALS(received.has_value(), true);
        AccountProofVerifier verifier;
        EQUALS(verifier.Verify(*received, kNow) == AccountProofResult::Valid, true);
        STREQUALS(AccountIdOf(received->publicKey).c_str(), key->AccountId().c_str());
    });

    IT("refuses a request whose claims were changed after signing", {
        const auto key = AccountKey::FromSeed(Rig::Seed(7));
        auto identity  = Rig::Identity();
        EQUALS(key->Sign(identity, kNow), true);

        // Exactly the reported exploit: someone else's Discord id written into a signed request.
        auto spoofed      = identity;
        spoofed.discordId = "999999999999999999";
        AccountProofVerifier verifier;
        EQUALS(verifier.Verify(spoofed, kNow) == AccountProofResult::BadSignature, true);

        auto renamed = identity;
        renamed.name = "Admin";
        EQUALS(verifier.Verify(renamed, kNow) == AccountProofResult::BadSignature, true);

        auto backdated = identity;
        backdated.issuedAt -= 1;
        EQUALS(verifier.Verify(backdated, kNow) == AccountProofResult::BadSignature, true);
    });

    IT("refuses a victim's public key carrying the attacker's signature", {
        const auto victim   = AccountKey::FromSeed(Rig::Seed(1));
        const auto attacker = AccountKey::FromSeed(Rig::Seed(2));
        auto victims        = Rig::Identity();
        auto forged         = Rig::Identity();
        EQUALS(victim->Sign(victims, kNow), true);
        EQUALS(attacker->Sign(forged, kNow), true);
        forged.publicKey = victims.publicKey;

        AccountProofVerifier verifier;
        EQUALS(verifier.Verify(forged, kNow) == AccountProofResult::BadSignature, true);
    });

    IT("refuses a request with no proof or a proof of the wrong shape", {
        AccountProofVerifier verifier;
        EQUALS(verifier.Verify(Rig::Identity(), kNow) == AccountProofResult::Missing, true);

        const auto key = AccountKey::FromSeed(Rig::Seed(7));
        auto identity  = Rig::Identity();
        EQUALS(key->Sign(identity, kNow), true);
        auto shortKey = identity;
        shortKey.publicKey.pop_back();
        EQUALS(verifier.Verify(shortKey, kNow) == AccountProofResult::Malformed, true);
        auto shortNonce = identity;
        shortNonce.nonce.pop_back();
        EQUALS(verifier.Verify(shortNonce, kNow) == AccountProofResult::Malformed, true);
    });

    IT("takes a request once", {
        const auto key = AccountKey::FromSeed(Rig::Seed(7));
        auto identity  = Rig::Identity();
        EQUALS(key->Sign(identity, kNow), true);

        AccountProofVerifier verifier;
        EQUALS(verifier.Verify(identity, kNow) == AccountProofResult::Valid, true);
        EQUALS(verifier.Verify(identity, kNow + 10) == AccountProofResult::Replayed, true);

        // A fresh signature from the same key is a new request.
        auto again = Rig::Identity();
        EQUALS(key->Sign(again, kNow + 10), true);
        EQUALS(verifier.Verify(again, kNow + 10) == AccountProofResult::Valid, true);
    });

    IT("takes a request only within the clock skew, and says which way the clock is off", {
        const auto key       = AccountKey::FromSeed(Rig::Seed(7));
        const uint64_t skew  = AccountProofVerifier::kMaxClockSkewSeconds;
        AccountProofVerifier verifier;

        auto edge = Rig::Identity();
        EQUALS(key->Sign(edge, kNow - skew), true);
        EQUALS(verifier.Verify(edge, kNow) == AccountProofResult::Valid, true);

        auto stale = Rig::Identity();
        EQUALS(key->Sign(stale, kNow - skew - 1), true);
        EQUALS(verifier.Verify(stale, kNow) == AccountProofResult::ClockBehind, true);

        auto early = Rig::Identity();
        EQUALS(key->Sign(early, kNow + skew + 600), true);
        EQUALS(verifier.Verify(early, kNow) == AccountProofResult::ClockAhead, true);
        STREQUALS(AccountProofVerifier::RefusalFor(AccountProofResult::ClockAhead, early, kNow).c_str(), "Your computer's clock is 15 minutes fast. Set it to the correct time and try again.");
    });

    IT("still refuses a replay after the request it replays has been forgotten as expired", {
        const auto key = AccountKey::FromSeed(Rig::Seed(7));
        auto identity  = Rig::Identity();
        EQUALS(key->Sign(identity, kNow), true);

        AccountProofVerifier verifier;
        EQUALS(verifier.Verify(identity, kNow) == AccountProofResult::Valid, true);
        // Forgotten only once its clock window has closed, so the replay is refused as stale.
        EQUALS(verifier.Verify(identity, kNow + AccountProofVerifier::kMaxClockSkewSeconds + 1) == AccountProofResult::ClockBehind, true);
    });

    IT("limits how often one address may ask, and no other address with it", {
        using Clock = Framework::Utils::TokenBucket::Clock;
        const Clock::time_point start {};
        AccountProofVerifier verifier(AccountProofVerifier::kMaxRemembered, 1.0, 3.0);
        EQUALS(verifier.Admit("10.0.0.1", start), true);
        EQUALS(verifier.Admit("10.0.0.1", start), true);
        EQUALS(verifier.Admit("10.0.0.1", start), true);
        EQUALS(verifier.Admit("10.0.0.1", start), false);
        // Another address has its own allowance.
        EQUALS(verifier.Admit("10.0.0.2", start), true);
        // The first earns one back a second later.
        EQUALS(verifier.Admit("10.0.0.1", start + std::chrono::seconds(1)), true);
        EQUALS(verifier.Admit("10.0.0.1", start + std::chrono::seconds(1)), false);
        STREQUALS(AccountProofVerifier::RefusalFor(AccountProofResult::RateLimited, Rig::Identity(), kNow).c_str(), "Too many connection attempts from your address. Wait a minute and try again.");
    });

    IT("refuses as busy rather than forget a request that could still be replayed", {
        const auto key = AccountKey::FromSeed(Rig::Seed(7));
        const auto sign = [&](uint64_t at) {
            auto identity = Rig::Identity();
            key->Sign(identity, at);
            return identity;
        };
        AccountProofVerifier verifier(2);
        const auto first = sign(kNow);
        EQUALS(verifier.Verify(first, kNow) == AccountProofResult::Valid, true);
        EQUALS(verifier.Verify(sign(kNow), kNow) == AccountProofResult::Valid, true);

        // Full of live requests: a new one is turned away, and the old ones are still recognised.
        EQUALS(verifier.Verify(sign(kNow), kNow) == AccountProofResult::Busy, true);
        EQUALS(verifier.Verify(first, kNow + 1) == AccountProofResult::Replayed, true);
        STREQUALS(AccountProofVerifier::RefusalFor(AccountProofResult::Busy, first, kNow).c_str(), "The server is busy checking connections. Try again in a minute.");

        // Once they could no longer pass the clock check, they make room.
        const uint64_t later = kNow + AccountProofVerifier::kMaxClockSkewSeconds + 1;
        EQUALS(verifier.Verify(sign(later), later) == AccountProofResult::Valid, true);
    });

    IT("forgets requests in the order they expire, not the order they arrived", {
        const auto key = AccountKey::FromSeed(Rig::Seed(7));
        const auto sign = [&](uint64_t at) {
            auto identity = Rig::Identity();
            key->Sign(identity, at);
            return identity;
        };
        AccountProofVerifier verifier(2);
        // Arrives first, expires last.
        EQUALS(verifier.Verify(sign(kNow), kNow) == AccountProofResult::Valid, true);
        // Arrives second, signed by a clock 200 s slow: expires 200 s sooner.
        EQUALS(verifier.Verify(sign(kNow - 200), kNow) == AccountProofResult::Valid, true);
        EQUALS(verifier.Verify(sign(kNow + 150), kNow + 150) == AccountProofResult::Valid, true);
    });

    IT("creates an account key once and reads the same account back", {
        Rig::TempDir dir;
        const auto path = dir.path / "nested" / "account.key";
        std::string error;
        const auto created = AccountKey::LoadOrCreate(path, error);
        EQUALS(created.has_value(), true);
        EQUALS(std::filesystem::exists(path), true);
        EQUALS(std::filesystem::exists(std::filesystem::path(path).concat(".new")), false);
        EQUALS(created->AccountId().size(), size_t {32});

        const auto loaded = AccountKey::LoadOrCreate(path, error);
        EQUALS(loaded.has_value(), true);
        STREQUALS(loaded->AccountId().c_str(), created->AccountId().c_str());

        // Only the staged file was ever written; nothing is left beside the key.
        size_t files = 0;
        for ([[maybe_unused]] const auto &entry : std::filesystem::directory_iterator(path.parent_path())) {
            ++files;
        }
        EQUALS(files, size_t {1});
        EQUALS(Rig::OwnerOnly(path), true);
    });

    IT("refuses a damaged key file and leaves it as it was", {
        Rig::TempDir dir;
        std::filesystem::create_directories(dir.path);
        const auto path = dir.path / "account.key";
        std::ofstream(path, std::ios::binary) << "mafiahub-account-key-v1 not-hex";

        std::string error;
        EQUALS(AccountKey::LoadOrCreate(path, error).has_value(), false);
        EQUALS(error.empty(), false);
        STREQUALS(Rig::ReadFile(path).c_str(), "mafiahub-account-key-v1 not-hex");
    });
});
