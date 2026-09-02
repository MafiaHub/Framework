/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Framework::Utils::Crypto {
    inline constexpr size_t kKeySize   = 32; // AES-256
    inline constexpr size_t kNonceSize = 12; // GCM standard IV
    inline constexpr size_t kTagSize   = 16; // GCM tag

    using Key   = std::array<uint8_t, kKeySize>;
    using Nonce = std::array<uint8_t, kNonceSize>;
    using Tag   = std::array<uint8_t, kTagSize>;

    bool RandomBytes(uint8_t *out, size_t len);

    // Zeroed on failure.
    Key GenerateKey(bool *outOk = nullptr);
    Nonce GenerateNonce(bool *outOk = nullptr);

    // Derived from the key and the data it protects, so identical input yields identical output.
    // A nonce repeat can therefore only occur for identical plaintext, which already encrypts to
    // identical bytes -- the AES-GCM-SIV rationale.
    Nonce DeriveNonce(const Key &key, const std::string &data);

    // AES-256-GCM. |aad| is authenticated but not encrypted.
    bool Encrypt(const Key &key, const Nonce &nonce, const void *aad, size_t aadLen, const std::string &plaintext, std::string &outCiphertext, Tag &outTag);
    bool Decrypt(const Key &key, const Nonce &nonce, const void *aad, size_t aadLen, const std::string &ciphertext, const Tag &tag, std::string &outPlaintext);

    std::string Sha256Hex(const void *data, size_t len);
    std::string Sha256Hex(const std::string &data);

    // Empty on failure.
    std::string Sha256FileHex(const std::string &path);

    bool ConstantTimeEquals(const std::string &a, const std::string &b);

    std::string ToHex(const void *data, size_t len);
    bool FromHex(const std::string &hex, uint8_t *out, size_t outLen);

    // Not elided by the optimiser, unlike memset.
    void SecureZero(void *data, size_t len);
} // namespace Framework::Utils::Crypto
