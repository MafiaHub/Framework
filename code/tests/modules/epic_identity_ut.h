/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "external/epic/account_proof.h"
#include "integrations/server/epic_identity_verifier.h"

#include <nlohmann/json.hpp>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

#include <chrono>
#include <memory>
#include <thread>

// Generate a test-only signer rather than depending on an Epic account, stored credentials,
// network access, or a captured real user's proof. The verifier receives only its public JWK.
class EpicProofFixture {
  public:
    static constexpr const char *account = "0123456789abcdef0123456789abcdef";
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key {nullptr, EVP_PKEY_free};
    nlohmann::json jwk;

    static int64_t Now() {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    static std::string Encode(std::string_view value) {
        std::string encoded((value.size() + 2) / 3 * 4, '\0');
        EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded.data()), reinterpret_cast<const unsigned char *>(value.data()), static_cast<int>(value.size()));
        for (char &c : encoded) {
            if (c == '+')
                c = '-';
            else if (c == '/')
                c = '_';
        }
        while (!encoded.empty() && encoded.back() == '=') encoded.pop_back();
        return encoded;
    }

    EpicProofFixture() {
        key.reset(EVP_RSA_gen(2048));
        if (!key)
            return;
        const auto component = [&](const char *name) {
            BIGNUM *raw = nullptr;
            EVP_PKEY_get_bn_param(key.get(), name, &raw);
            const std::unique_ptr<BIGNUM, decltype(&BN_free)> value(raw, BN_free);
            std::string bytes(BN_num_bytes(value.get()), '\0');
            BN_bn2bin(value.get(), reinterpret_cast<unsigned char *>(bytes.data()));
            return Encode(bytes);
        };
        jwk = {{"kty", "RSA"}, {"kid", "test-key"}, {"n", component(OSSL_PKEY_PARAM_RSA_N)}, {"e", component(OSSL_PKEY_PARAM_RSA_E)}};
    }

    nlohmann::json Claims(std::string tokenId = "one-join") const {
        const auto now = Now();
        return {{"sub", account}, {"jti", tokenId}, {"clid", "launcher-client"}, {"iat", now}, {"exp", now + 300}, {"ent", nlohmann::json::array()}};
    }

    std::string Sign(const nlohmann::json &claims, const nlohmann::json &header = {{"alg", "RS512"}, {"kid", "test-key"}}) const {
        const std::string bytes = Encode(header.dump()) + "." + Encode(claims.dump());
        const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        if (!key || EVP_DigestSignInit(digest.get(), nullptr, EVP_sha512(), nullptr, key.get()) <= 0)
            return {};
        std::size_t length {};
        EVP_DigestSign(digest.get(), nullptr, &length, reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
        std::string signature(length, '\0');
        if (EVP_DigestSign(digest.get(), reinterpret_cast<unsigned char *>(signature.data()), &length, reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size()) <= 0)
            return {};
        signature.resize(length);
        return "egoc1~" + bytes + "." + Encode(signature);
    }

    static MafiaNet::RakNetGUID Guid(uint64_t id) {
        MafiaNet::RakNetGUID guid;
        guid.g = id;
        return guid;
    }

    static std::vector<Framework::Integrations::Server::EpicIdentityVerifier::Decision> Collect(Framework::Integrations::Server::EpicIdentityVerifier &verifier) {
        std::vector<Framework::Integrations::Server::EpicIdentityVerifier::Decision> decisions;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do {
            verifier.Collect(decisions);
            if (!decisions.empty())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        return decisions;
    }
};

MODULE(epic_identity, {
    using namespace Framework::External::Epic;
    using Framework::Integrations::Server::EpicIdentityVerifier;
    using Framework::Networking::RPC::ClientIdentity;
    EpicProofFixture signer;

    IT("extracts the account only from a valid Epic-signed ownership proof", {
        const auto proof    = signer.Sign(signer.Claims());
        const auto verified = VerifyAccountProof(proof, signer.jwk.dump(), signer.Now());
        EQUALS(verified.has_value(), true);
        if (verified)
            STREQUALS(verified->accountId.c_str(), signer.account);
    });

    IT("rejects an account id substituted into a signed proof", {
        auto proof       = signer.Sign(signer.Claims());
        const auto first = proof.find('.');
        const auto last  = proof.rfind('.');
        auto forged      = signer.Claims();
        forged["sub"]    = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        proof.replace(first + 1, last - first - 1, signer.Encode(forged.dump()));
        EQUALS(VerifyAccountProof(proof, signer.jwk.dump(), signer.Now()).has_value(), false);
    });

    IT("rejects a proof signed by another key even with the same key id", {
        EpicProofFixture attacker;
        EQUALS(VerifyAccountProof(attacker.Sign(attacker.Claims()), signer.jwk.dump(), signer.Now()).has_value(), false);
    });

    IT("rejects algorithm confusion, unknown critical headers, unsafe key ids and access tokens", {
        for (const std::string algorithm : {"none", "HS512", "RS256"}) {
            EQUALS(VerifyAccountProof(signer.Sign(signer.Claims(), {{"alg", algorithm}, {"kid", "test-key"}}), signer.jwk.dump(), signer.Now()).has_value(), false);
        }
        EQUALS(AccountProofKeyId(signer.Sign(signer.Claims(), {{"alg", "RS512"}, {"kid", "../keys"}})).has_value(), false);
        EQUALS(AccountProofKeyId(signer.Sign(signer.Claims(), {{"alg", "RS512"}, {"kid", "test-key"}, {"crit", {"unknown"}}})).has_value(), false);
        EQUALS(AccountProofKeyId("eg1~" + signer.Sign(signer.Claims()).substr(6)).has_value(), false);
        EQUALS(AccountProofKeyId(std::string(kMaxAccountProofLength + 1, 'a')).has_value(), false);
    });

    IT("rejects expired, future-dated and stale proofs", {
        auto claims   = signer.Claims();
        claims["iat"] = signer.Now() - 300;
        claims["exp"] = signer.Now();
        EQUALS(VerifyAccountProof(signer.Sign(claims), signer.jwk.dump(), signer.Now()).has_value(), false);
        claims["iat"] = signer.Now() + 60;
        claims["exp"] = signer.Now() + 300;
        EQUALS(VerifyAccountProof(signer.Sign(claims), signer.jwk.dump(), signer.Now()).has_value(), false);
        claims        = signer.Claims();
        claims["iat"] = signer.Now() - 301;
        EQUALS(VerifyAccountProof(signer.Sign(claims), signer.jwk.dump(), signer.Now()).has_value(), false);
    });

    IT("limits a fresh proof to five minutes even if Epic gives it a longer lifetime", {
        const auto now    = signer.Now();
        auto claims       = signer.Claims();
        claims["iat"]     = now;
        claims["exp"]     = now + 28800;
        const auto result = VerifyAccountProof(signer.Sign(claims), signer.jwk.dump(), now);
        EQUALS(result.has_value(), true);
        if (result)
            EQUALS(result->expiresAt, now + 300);
    });

    IT("fails closed on missing or malformed identity and time claims", {
        for (const std::string field : {"sub", "jti", "iat", "exp"}) {
            auto claims = signer.Claims();
            claims.erase(field);
            EQUALS(VerifyAccountProof(signer.Sign(claims), signer.jwk.dump(), signer.Now()).has_value(), false);
            claims[field] = nlohmann::json::array();
            EQUALS(VerifyAccountProof(signer.Sign(claims), signer.jwk.dump(), signer.Now()).has_value(), false);
        }
        EQUALS(AccountProofKeyId("egoc1~broken.input.signature").has_value(), false);
    });

    const auto verify = [&signer](const std::string &proof) {
        return VerifyAccountProof(proof, signer.jwk.dump(), signer.Now());
    };

    IT("refuses a bare client account claim without starting verification", {
        EpicIdentityVerifier verifier(verify);
        ClientIdentity identity;
        identity.epicId = signer.account;
        EQUALS(verifier.Begin(signer.Guid(1), identity), false);
    });

    IT("retains the verified id, removes the proof and rejects replay on another join", {
        EpicIdentityVerifier verifier(verify);
        ClientIdentity identity;
        identity.epicId    = signer.account;
        identity.epicProof = signer.Sign(signer.Claims());
        EQUALS(verifier.Begin(signer.Guid(1), identity), true);
        const auto first = signer.Collect(verifier);
        EQUALS(first.size(), 1u);
        if (!first.empty()) {
            EQUALS(first[0].verified, true);
            STREQUALS(first[0].identity.epicId.c_str(), signer.account);
            EQUALS(first[0].identity.epicProof.empty(), true);
        }
        EQUALS(verifier.Begin(signer.Guid(2), identity), true);
        const auto replay = signer.Collect(verifier);
        EQUALS(replay.size(), 1u);
        if (!replay.empty())
            EQUALS(replay[0].verified, false);
    });

    IT("refuses a claimed account that differs from the verified account", {
        EpicIdentityVerifier verifier(verify);
        ClientIdentity identity;
        identity.epicId    = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        identity.epicProof = signer.Sign(signer.Claims("mismatch"));
        EQUALS(verifier.Begin(signer.Guid(1), identity), true);
        const auto result = signer.Collect(verifier);
        EQUALS(result.size(), 1u);
        if (!result.empty()) {
            EQUALS(result[0].verified, false);
            EQUALS(result[0].identity.epicId.empty(), true);
        }
    });

    IT("bounds outstanding verification work and never revives abandoned requests", {
        std::promise<void> release;
        const auto wait = release.get_future().share();
        EpicIdentityVerifier verifier([wait, verify](const std::string &proof) {
            wait.wait();
            return verify(proof);
        });
        ClientIdentity identity;
        identity.epicProof = signer.Sign(signer.Claims("cancelled"));
        for (uint64_t id = 1; id <= 16; ++id) {
            EQUALS(verifier.Begin(signer.Guid(id), identity), true);
            verifier.Drop(id);
        }
        EQUALS(verifier.Begin(signer.Guid(17), identity), false);
        release.set_value();
        std::vector<EpicIdentityVerifier::Decision> result;
        bool nextStarted    = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!nextStarted && std::chrono::steady_clock::now() < deadline) {
            verifier.Collect(result);
            nextStarted = verifier.Begin(signer.Guid(17), identity);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EQUALS(nextStarted, true);
        verifier.Drop(17);
        EQUALS(result.empty(), true);
        verifier.Shutdown();
    });

    IT("refuses verification service failures", {
        EpicIdentityVerifier verifier([](const std::string &) -> std::optional<VerifiedAccount> {
            return std::nullopt;
        });
        ClientIdentity identity;
        identity.epicProof = signer.Sign(signer.Claims("unreachable"));
        EQUALS(verifier.Begin(signer.Guid(1), identity), true);
        const auto result = signer.Collect(verifier);
        EQUALS(result.size(), 1u);
        if (!result.empty())
            EQUALS(result[0].verified, false);
    });
});
