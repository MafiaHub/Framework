/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "package.h"

#include <logging/logger.h>

#include <miniz.h>

#include <algorithm>
#include <cstring>

namespace Framework::Utils::Package {
    namespace {
        // miniz stamps the current time when this is null, which would change the hash of an
        // unchanged package on every rebuild.
        constexpr MZ_TIME_T kFixedTimestamp = 0;

        void WriteU16(std::string &out, uint16_t value) {
            out.push_back(static_cast<char>(value & 0xFF));
            out.push_back(static_cast<char>((value >> 8) & 0xFF));
        }

        void WriteU32(std::string &out, uint32_t value) {
            for (int i = 0; i < 4; ++i) {
                out.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
            }
        }

        void WriteU64(std::string &out, uint64_t value) {
            for (int i = 0; i < 8; ++i) {
                out.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
            }
        }

        uint16_t ReadU16(const uint8_t *p) {
            return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8));
        }

        uint32_t ReadU32(const uint8_t *p) {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }

        uint64_t ReadU64(const uint8_t *p) {
            uint64_t value = 0;
            for (int i = 0; i < 8; ++i) {
                value |= static_cast<uint64_t>(p[i]) << (i * 8);
            }
            return value;
        }

        bool NormalisePath(const std::string &input, std::string &out) {
            if (input.empty()) {
                return false;
            }

            std::string normalised = input;
            std::replace(normalised.begin(), normalised.end(), '\\', '/');

            if (normalised.front() == '/') {
                return false;
            }
            if (normalised.size() >= 2 && normalised[1] == ':') {
                return false;
            }

            std::vector<std::string> parts;
            size_t start = 0;
            while (start <= normalised.size()) {
                const size_t slash = normalised.find('/', start);
                const size_t end   = (slash == std::string::npos) ? normalised.size() : slash;
                std::string part   = normalised.substr(start, end - start);
                if (part == "..") {
                    return false;
                }
                if (!part.empty() && part != ".") {
                    parts.push_back(std::move(part));
                }
                if (slash == std::string::npos) {
                    break;
                }
                start = slash + 1;
            }

            if (parts.empty()) {
                return false;
            }

            out.clear();
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) {
                    out.push_back('/');
                }
                out += parts[i];
            }
            return true;
        }

        // Tag region stays zeroed: that is what both sides authenticate.
        std::string BuildHeader(uint16_t flags, const Crypto::Nonce &nonce, uint64_t payloadSize) {
            std::string header;
            header.reserve(kHeaderSize);
            WriteU32(header, kMagic);
            WriteU16(header, kFormatVersion);
            WriteU16(header, flags);
            WriteU32(header, 0);
            header.append(reinterpret_cast<const char *>(nonce.data()), nonce.size());
            header.append(Crypto::kTagSize, '\0');
            WriteU64(header, payloadSize);
            return header;
        }
    } // namespace

    bool Writer::Add(const std::string &relativePath, std::string data) {
        std::string normalised;
        if (!NormalisePath(relativePath, normalised)) {
            Logging::GetLogger(FRAMEWORK_INNER_UTILS)->warn("Refusing to package path '{}': not a safe relative path", relativePath);
            return false;
        }
        if (data.size() > kMaxEntrySize) {
            Logging::GetLogger(FRAMEWORK_INNER_UTILS)->warn("Refusing to package '{}': {} bytes exceeds the {} byte entry limit", relativePath, data.size(), kMaxEntrySize);
            return false;
        }
        if (_entries.size() >= kMaxEntries) {
            Logging::GetLogger(FRAMEWORK_INNER_UTILS)->warn("Refusing to package '{}': entry limit {} reached", relativePath, kMaxEntries);
            return false;
        }

        for (const auto &existing : _entries) {
            if (existing.path == normalised) {
                Logging::GetLogger(FRAMEWORK_INNER_UTILS)->warn("Duplicate package entry '{}'; keeping the first", normalised);
                return false;
            }
        }

        _entries.push_back(Entry {std::move(normalised), std::move(data)});
        return true;
    }

    bool Writer::Build(const Crypto::Key *key, std::string &outBlob) const {
        mz_zip_archive zip {};
        if (!mz_zip_writer_init_heap(&zip, 0, 64 * 1024)) {
            Logging::GetLogger(FRAMEWORK_INNER_UTILS)->error("Could not initialise the package ZIP writer");
            return false;
        }

        for (const auto &entry : _entries) {
            // The only place compression can happen: ciphertext does not compress.
            if (!mz_zip_writer_add_mem_ex_v2(&zip, entry.path.c_str(), entry.data.data(), entry.data.size(), nullptr, 0, MZ_DEFAULT_LEVEL, 0, 0, const_cast<MZ_TIME_T *>(&kFixedTimestamp), nullptr, 0, nullptr, 0)) {
                Logging::GetLogger(FRAMEWORK_INNER_UTILS)->error("Could not add '{}' to the package ZIP", entry.path);
                mz_zip_writer_end(&zip);
                return false;
            }
        }

        void *zipBuffer = nullptr;
        size_t zipSize  = 0;
        if (!mz_zip_writer_finalize_heap_archive(&zip, &zipBuffer, &zipSize)) {
            Logging::GetLogger(FRAMEWORK_INNER_UTILS)->error("Could not finalise the package ZIP");
            mz_zip_writer_end(&zip);
            return false;
        }

        std::string payload(static_cast<const char *>(zipBuffer), zipSize);
        mz_zip_writer_end(&zip); // frees zipBuffer

        if (payload.size() > kMaxPayloadSize) {
            Logging::GetLogger(FRAMEWORK_INNER_UTILS)->error("Package payload of {} bytes exceeds the {} byte limit", payload.size(), kMaxPayloadSize);
            Crypto::SecureZero(payload.data(), payload.size());
            return false;
        }

        Crypto::Nonce nonce {};
        Crypto::Tag tag {};
        const uint16_t flags = key ? kFlagEncrypted : 0;

        if (key) {
            // Derived, not random: a fresh nonce each build would change the hash of unchanged
            // content and re-download it to every client.
            nonce = Crypto::DeriveNonce(*key, payload);
        }

        std::string header = BuildHeader(flags, nonce, payload.size());

        std::string body;
        if (key) {
            if (!Crypto::Encrypt(*key, nonce, header.data(), header.size(), payload, body, tag)) {
                Logging::GetLogger(FRAMEWORK_INNER_UTILS)->error("Failed to encrypt resource package");
                Crypto::SecureZero(payload.data(), payload.size());
                return false;
            }
            std::memcpy(header.data() + 24, tag.data(), tag.size());
        }
        else {
            body = payload;
        }

        Crypto::SecureZero(payload.data(), payload.size());

        outBlob.clear();
        outBlob.reserve(header.size() + body.size());
        outBlob += header;
        outBlob += body;
        return true;
    }

    bool Open(const std::string &blob, const Crypto::Key *key, std::string &outZip, std::string &outError) {
        outZip.clear();

        if (blob.size() < kHeaderSize) {
            outError = "package is smaller than its header";
            return false;
        }

        const auto *bytes = reinterpret_cast<const uint8_t *>(blob.data());
        if (ReadU32(bytes) != kMagic) {
            outError = "bad package magic";
            return false;
        }

        const uint16_t version = ReadU16(bytes + 4);
        if (version != kFormatVersion) {
            outError = "unsupported package version " + std::to_string(version);
            return false;
        }

        const uint16_t flags      = ReadU16(bytes + 6);
        const uint64_t payloadLen = ReadU64(bytes + 40);
        const bool encrypted      = (flags & kFlagEncrypted) != 0;

        if (payloadLen > kMaxPayloadSize) {
            outError = "package payload exceeds the size limit";
            return false;
        }
        if (blob.size() - kHeaderSize != payloadLen) {
            outError = "package payload length does not match the container";
            return false;
        }
        if (encrypted && !key) {
            outError = "package is encrypted but no key was supplied";
            return false;
        }
        if (!encrypted && key) {
            outError = "package is unencrypted but an encrypted one was required";
            return false;
        }

        if (!encrypted) {
            outZip = blob.substr(kHeaderSize);
            return true;
        }

        Crypto::Nonce nonce {};
        Crypto::Tag tag {};
        std::memcpy(nonce.data(), bytes + 12, nonce.size());
        std::memcpy(tag.data(), bytes + 24, tag.size());

        std::string aad = blob.substr(0, kHeaderSize);
        std::memset(aad.data() + 24, 0, tag.size());

        if (!Crypto::Decrypt(*key, nonce, aad.data(), aad.size(), blob.substr(kHeaderSize), tag, outZip)) {
            outError = "package failed authentication (wrong key or tampered content)";
            return false;
        }
        return true;
    }

    bool IsEncrypted(const std::string &blob) {
        if (blob.size() < kHeaderSize) {
            return false;
        }
        const auto *bytes = reinterpret_cast<const uint8_t *>(blob.data());
        if (ReadU32(bytes) != kMagic) {
            return false;
        }
        return (ReadU16(bytes + 6) & kFlagEncrypted) != 0;
    }
} // namespace Framework::Utils::Package
