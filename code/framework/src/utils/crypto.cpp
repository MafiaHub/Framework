/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "crypto.h"

#include <logging/logger.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <fstream>
#include <memory>
#include <vector>

namespace Framework::Utils::Crypto {
    namespace {
        constexpr char kHexDigits[] = "0123456789abcdef";

        struct CipherContextDeleter {
            void operator()(EVP_CIPHER_CTX *ctx) const {
                EVP_CIPHER_CTX_free(ctx);
            }
        };

        using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, CipherContextDeleter>;

        int HexValue(char c) {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }
            return -1;
        }
    } // namespace

    bool RandomBytes(uint8_t *out, size_t len) {
        if (!out || len == 0) {
            return false;
        }
        if (RAND_bytes(out, static_cast<int>(len)) != 1) {
            Logging::GetLogger(FRAMEWORK_INNER_UTILS)->error("RAND_bytes failed; refusing to produce weak key material");
            return false;
        }
        return true;
    }

    Key GenerateKey(bool *outOk) {
        Key key {};
        const bool ok = RandomBytes(key.data(), key.size());
        if (!ok) {
            key.fill(0);
        }
        if (outOk) {
            *outOk = ok;
        }
        return key;
    }

    Nonce GenerateNonce(bool *outOk) {
        Nonce nonce {};
        const bool ok = RandomBytes(nonce.data(), nonce.size());
        if (!ok) {
            nonce.fill(0);
        }
        if (outOk) {
            *outOk = ok;
        }
        return nonce;
    }

    Nonce DeriveNonce(const Key &key, const std::string &data) {
        Nonce nonce {};
        unsigned int length = 0;
        uint8_t digest[EVP_MAX_MD_SIZE] {};

        static constexpr char kLabel[] = "FWPK-nonce-v1";
        std::string message;
        message.reserve(sizeof(kLabel) - 1 + data.size());
        message.append(kLabel, sizeof(kLabel) - 1);
        message += data;

        if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), reinterpret_cast<const uint8_t *>(message.data()), message.size(), digest, &length) || length < nonce.size()) {
            Logging::GetLogger(FRAMEWORK_INNER_UTILS)->error("HMAC failed while deriving a package nonce");
            nonce.fill(0);
            return nonce;
        }

        std::memcpy(nonce.data(), digest, nonce.size());
        return nonce;
    }

    bool Encrypt(const Key &key, const Nonce &nonce, const void *aad, size_t aadLen, const std::string &plaintext, std::string &outCiphertext, Tag &outTag) {
        CipherContext ctx(EVP_CIPHER_CTX_new());
        if (!ctx) {
            return false;
        }
        if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            return false;
        }
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1) {
            return false;
        }
        if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) {
            return false;
        }

        int outLen = 0;
        if (aad && aadLen > 0) {
            if (EVP_EncryptUpdate(ctx.get(), nullptr, &outLen, static_cast<const uint8_t *>(aad), static_cast<int>(aadLen)) != 1) {
                return false;
            }
        }

        outCiphertext.resize(plaintext.size());
        int written = 0;
        if (!plaintext.empty()) {
            if (EVP_EncryptUpdate(ctx.get(), reinterpret_cast<uint8_t *>(outCiphertext.data()), &written, reinterpret_cast<const uint8_t *>(plaintext.data()), static_cast<int>(plaintext.size())) != 1) {
                return false;
            }
        }

        int finalLen = 0;
        if (EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<uint8_t *>(outCiphertext.data()) + written, &finalLen) != 1) {
            return false;
        }
        outCiphertext.resize(static_cast<size_t>(written) + static_cast<size_t>(finalLen));

        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(outTag.size()), outTag.data()) != 1) {
            return false;
        }
        return true;
    }

    bool Decrypt(const Key &key, const Nonce &nonce, const void *aad, size_t aadLen, const std::string &ciphertext, const Tag &tag, std::string &outPlaintext) {
        CipherContext ctx(EVP_CIPHER_CTX_new());
        if (!ctx) {
            return false;
        }
        if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            return false;
        }
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1) {
            return false;
        }
        if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) {
            return false;
        }

        int outLen = 0;
        if (aad && aadLen > 0) {
            if (EVP_DecryptUpdate(ctx.get(), nullptr, &outLen, static_cast<const uint8_t *>(aad), static_cast<int>(aadLen)) != 1) {
                return false;
            }
        }

        outPlaintext.resize(ciphertext.size());
        int written = 0;
        if (!ciphertext.empty()) {
            if (EVP_DecryptUpdate(ctx.get(), reinterpret_cast<uint8_t *>(outPlaintext.data()), &written, reinterpret_cast<const uint8_t *>(ciphertext.data()), static_cast<int>(ciphertext.size())) != 1) {
                outPlaintext.clear();
                return false;
            }
        }

        // Const-cast is required by the OpenSSL control interface; the tag is not modified.
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()), const_cast<uint8_t *>(tag.data())) != 1) {
            outPlaintext.clear();
            return false;
        }

        int finalLen = 0;
        // Fails when the tag does not authenticate the ciphertext and AAD.
        if (EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<uint8_t *>(outPlaintext.data()) + written, &finalLen) != 1) {
            SecureZero(outPlaintext.data(), outPlaintext.size());
            outPlaintext.clear();
            return false;
        }
        outPlaintext.resize(static_cast<size_t>(written) + static_cast<size_t>(finalLen));
        return true;
    }

    std::string Sha256Hex(const void *data, size_t len) {
        uint8_t digest[SHA256_DIGEST_LENGTH] {};
        SHA256(static_cast<const uint8_t *>(data), len, digest);
        return ToHex(digest, sizeof(digest));
    }

    std::string Sha256Hex(const std::string &data) {
        return Sha256Hex(data.data(), data.size());
    }

    std::string Sha256FileHex(const std::string &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return {};
        }

        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        if (!ctx) {
            return {};
        }
        if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
            EVP_MD_CTX_free(ctx);
            return {};
        }

        std::vector<char> buffer(64 * 1024);
        while (file.good()) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto read = file.gcount();
            if (read > 0) {
                if (EVP_DigestUpdate(ctx, buffer.data(), static_cast<size_t>(read)) != 1) {
                    EVP_MD_CTX_free(ctx);
                    return {};
                }
            }
        }
        if (file.bad()) {
            EVP_MD_CTX_free(ctx);
            return {};
        }

        uint8_t digest[EVP_MAX_MD_SIZE] {};
        unsigned int digestLen = 0;
        if (EVP_DigestFinal_ex(ctx, digest, &digestLen) != 1) {
            EVP_MD_CTX_free(ctx);
            return {};
        }
        EVP_MD_CTX_free(ctx);
        return ToHex(digest, digestLen);
    }

    bool ConstantTimeEquals(const std::string &a, const std::string &b) {
        if (a.size() != b.size()) {
            return false;
        }
        if (a.empty()) {
            return true;
        }
        return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
    }

    std::string ToHex(const void *data, size_t len) {
        const auto *bytes = static_cast<const uint8_t *>(data);
        std::string out;
        out.resize(len * 2);
        for (size_t i = 0; i < len; ++i) {
            out[i * 2]     = kHexDigits[bytes[i] >> 4];
            out[i * 2 + 1] = kHexDigits[bytes[i] & 0x0F];
        }
        return out;
    }

    bool FromHex(const std::string &hex, uint8_t *out, size_t outLen) {
        if (!out || hex.size() != outLen * 2) {
            return false;
        }
        for (size_t i = 0; i < outLen; ++i) {
            const int hi = HexValue(hex[i * 2]);
            const int lo = HexValue(hex[i * 2 + 1]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            out[i] = static_cast<uint8_t>((hi << 4) | lo);
        }
        return true;
    }

    void SecureZero(void *data, size_t len) {
        if (data && len > 0) {
            OPENSSL_cleanse(data, len);
        }
    }
} // namespace Framework::Utils::Crypto
