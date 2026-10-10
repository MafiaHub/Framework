/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "account_proof.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/rsa.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace Framework::External::Epic {
    namespace {
        constexpr std::string_view kPrefix    = "egoc1~";
        constexpr std::size_t kMaxKeyResponse = 16384;

        bool IsBase64Url(std::string_view value) {
            return !value.empty() && value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") == std::string_view::npos;
        }

        std::optional<std::string> DecodeBase64Url(std::string_view value) {
            if (!IsBase64Url(value) || value.size() % 4 == 1) {
                return std::nullopt;
            }
            std::string padded(value);
            for (char &c : padded) {
                if (c == '-')
                    c = '+';
                else if (c == '_')
                    c = '/';
            }
            const auto originalSize = padded.size();
            while (padded.size() % 4) padded += '=';
            std::string decoded(padded.size() / 4 * 3, '\0');
            const int length = EVP_DecodeBlock(reinterpret_cast<unsigned char *>(decoded.data()), reinterpret_cast<const unsigned char *>(padded.data()), static_cast<int>(padded.size()));
            if (length < 0) {
                return std::nullopt;
            }
            decoded.resize(static_cast<std::size_t>(length) - (padded.size() - originalSize));
            return decoded;
        }

        struct Proof {
            std::string_view signedBytes;
            std::string signature;
            nlohmann::json header;
            nlohmann::json claims;
        };

        std::optional<Proof> ParseProof(std::string_view value) {
            if (value.size() > kMaxAccountProofLength || !value.starts_with(kPrefix)) {
                return std::nullopt;
            }
            value.remove_prefix(kPrefix.size());
            const auto first = value.find('.');
            const auto last  = value.rfind('.');
            if (first == std::string_view::npos || first == last) {
                return std::nullopt;
            }
            const auto header    = DecodeBase64Url(value.substr(0, first));
            const auto claims    = DecodeBase64Url(value.substr(first + 1, last - first - 1));
            const auto signature = DecodeBase64Url(value.substr(last + 1));
            if (!header || !claims || !signature) {
                return std::nullopt;
            }
            Proof proof {value.substr(0, last), *signature, nlohmann::json::parse(*header, nullptr, false), nlohmann::json::parse(*claims, nullptr, false)};
            if (!proof.header.is_object() || !proof.claims.is_object() || proof.header.value("alg", std::string {}) != "RS512" || proof.header.contains("crit")) {
                return std::nullopt;
            }
            const auto kid = proof.header.value("kid", std::string {});
            if (kid.size() > 128 || !IsBase64Url(kid)) {
                return std::nullopt;
            }
            return proof;
        }

        int64_t Now() {
            return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        std::size_t ReceiveKey(char *data, std::size_t size, std::size_t count, void *context) {
            auto &body = *static_cast<std::string *>(context);
            if (size != 1 || count > kMaxKeyResponse - body.size()) {
                return 0;
            }
            body.append(data, count);
            return count;
        }

        std::optional<std::string> FetchKey(const std::string &kid) {
            struct CurlRuntime {
                CURLcode status = curl_global_init(CURL_GLOBAL_DEFAULT);
                ~CurlRuntime() {
                    if (status == CURLE_OK)
                        curl_global_cleanup();
                }
            };
            static const CurlRuntime runtime;
            if (runtime.status != CURLE_OK)
                return std::nullopt;

            // Cache only keys fetched over authenticated HTTPS, and periodically re-fetch them
            // so removing a compromised key at Epic also removes trust on a running server.
            struct CachedKey {
                std::string body;
                std::chrono::steady_clock::time_point expires;
            };
            static std::mutex mutex;
            static std::unordered_map<std::string, CachedKey> keys;
            {
                const std::lock_guard lock(mutex);
                if (const auto it = keys.find(kid); it != keys.end() && it->second.expires > std::chrono::steady_clock::now())
                    return it->second.body;
            }
            const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
            if (!curl)
                return std::nullopt;
            const std::string url = "https://ecommerceintegration-public-service-ecomprod02.ol.epicgames.com/ecommerceintegration/api/public/publickeys/" + kid;
            std::string body;
            curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
            curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 3000L);
            curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 5000L);
            curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, ReceiveKey);
            curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
            if (curl_easy_perform(curl.get()) != CURLE_OK)
                return std::nullopt;
            long status {};
            curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
            if (status != 200)
                return std::nullopt;
            const auto key = nlohmann::json::parse(body, nullptr, false);
            if (!key.is_object() || !key.contains("kid") || key["kid"] != kid || !key.contains("kty") || key["kty"] != "RSA")
                return std::nullopt;
            {
                const std::lock_guard lock(mutex);
                if (keys.size() >= 8)
                    keys.clear();
                keys.insert_or_assign(kid, CachedKey {body, std::chrono::steady_clock::now() + std::chrono::minutes(5)});
            }
            return body;
        }
    } // namespace

    std::optional<std::string> AccountProofKeyId(std::string_view proof) {
        try {
            const auto parsed = ParseProof(proof);
            return parsed ? std::optional(parsed->header.at("kid").get<std::string>()) : std::nullopt;
        }
        catch (const nlohmann::json::exception &) {
            return std::nullopt;
        }
    }

    std::optional<VerifiedAccount> VerifyAccountProof(std::string_view proof, std::string_view publicJwk, int64_t now) {
        try {
            const auto parsed = ParseProof(proof);
            const auto jwk    = nlohmann::json::parse(publicJwk, nullptr, false);
            if (!parsed || !jwk.is_object() || jwk.value("kty", std::string {}) != "RSA" || jwk.value("kid", std::string {}) != parsed->header.at("kid").get<std::string>())
                return std::nullopt;
            const auto modulus  = DecodeBase64Url(jwk.value("n", std::string {}));
            const auto exponent = DecodeBase64Url(jwk.value("e", std::string {}));
            if (!modulus || !exponent || modulus->size() < 256 || modulus->size() > 512 || exponent->size() > 8)
                return std::nullopt;
            const std::unique_ptr<BIGNUM, decltype(&BN_free)> n(BN_bin2bn(reinterpret_cast<const unsigned char *>(modulus->data()), static_cast<int>(modulus->size()), nullptr), BN_free);
            const std::unique_ptr<BIGNUM, decltype(&BN_free)> e(BN_bin2bn(reinterpret_cast<const unsigned char *>(exponent->data()), static_cast<int>(exponent->size()), nullptr), BN_free);
            const std::unique_ptr<OSSL_PARAM_BLD, decltype(&OSSL_PARAM_BLD_free)> builder(OSSL_PARAM_BLD_new(), OSSL_PARAM_BLD_free);
            if (!n || !e || !builder || !OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_RSA_N, n.get()) || !OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_RSA_E, e.get()))
                return std::nullopt;
            const std::unique_ptr<OSSL_PARAM, decltype(&OSSL_PARAM_free)> params(OSSL_PARAM_BLD_to_param(builder.get()), OSSL_PARAM_free);
            const std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> keyContext(EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), EVP_PKEY_CTX_free);
            EVP_PKEY *rawKey = nullptr;
            if (!params || !keyContext || EVP_PKEY_fromdata_init(keyContext.get()) <= 0 || EVP_PKEY_fromdata(keyContext.get(), &rawKey, EVP_PKEY_PUBLIC_KEY, params.get()) <= 0)
                return std::nullopt;
            const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(rawKey, EVP_PKEY_free);
            const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
            EVP_PKEY_CTX *verifyContext = nullptr;
            if (!digest || EVP_DigestVerifyInit(digest.get(), &verifyContext, EVP_sha512(), nullptr, key.get()) <= 0 || EVP_PKEY_CTX_set_rsa_padding(verifyContext, RSA_PKCS1_PADDING) <= 0
                || EVP_DigestVerify(digest.get(), reinterpret_cast<const unsigned char *>(parsed->signature.data()), parsed->signature.size(), reinterpret_cast<const unsigned char *>(parsed->signedBytes.data()), parsed->signedBytes.size()) != 1)
                return std::nullopt;

            // Read identity only after verifying the signed bytes. No caller-provided account id
            // participates in deciding which account Epic authenticated.
            const auto &claims = parsed->claims;
            if (!claims.contains("iat") || !claims["iat"].is_number_integer() || !claims.contains("exp") || !claims["exp"].is_number_integer())
                return std::nullopt;
            const auto issuedAt = claims["iat"].get<int64_t>();
            VerifiedAccount result {claims.value("sub", std::string {}), claims.value("jti", std::string {}), claims["exp"].get<int64_t>()};
            if (result.accountId.size() != 32 || result.accountId.find_first_not_of("0123456789abcdef") != std::string::npos || result.tokenId.empty() || result.tokenId.size() > 128 || issuedAt < 0 || issuedAt > now + 30 || issuedAt <= now - 300 || result.expiresAt <= now
                || result.expiresAt <= issuedAt)
                return std::nullopt;
            // Require a fresh proof even if Epic issued it with a longer lifetime. The client
            // mints one for each join, so no cached launch-time token needs to remain usable.
            result.expiresAt = std::min(result.expiresAt, issuedAt + 300);
            return result;
        }
        catch (const nlohmann::json::exception &) {
            return std::nullopt;
        }
    }

    std::optional<VerifiedAccount> VerifyAccountProofOnline(const std::string &proof) {
        const auto kid = AccountProofKeyId(proof);
        if (!kid)
            return std::nullopt;
        const auto key = FetchKey(*kid);
        return key ? VerifyAccountProof(proof, *key, Now()) : std::nullopt;
    }
} // namespace Framework::External::Epic
