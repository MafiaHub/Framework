/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/crypto.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Framework::Utils::Package {
    // One resource's client files as an AES-256-GCM encrypted ZIP.
    // See docs/research/client_resource_protection.md.
    //
    // Envelope (little-endian, authenticated as GCM additional data):
    //   0   4   magic 'F','W','P','K'
    //   4   2   formatVersion
    //   6   2   flags
    //   8   4   reserved (0)
    //   12  12  nonce
    //   24  16  tag
    //   40  8   payloadSize
    //   48  ..  payload -- a ZIP archive, encrypted when kFlagEncrypted
    inline constexpr uint32_t kMagic         = 0x4B505746; // 'FWPK'
    inline constexpr uint16_t kFormatVersion = 2;
    inline constexpr uint16_t kFlagEncrypted = 1 << 0;

    inline constexpr size_t kHeaderSize = 48;

    inline constexpr uint64_t kMaxPayloadSize = 256ull * 1024 * 1024;
    inline constexpr uint32_t kMaxEntries     = 8192;
    inline constexpr uint32_t kMaxEntrySize   = 64u * 1024 * 1024;

    inline constexpr const char *kExtension = ".fwpak";

    class Writer final {
      public:
        // False when |relativePath| is empty, absolute, escapes the root, or is a duplicate.
        bool Add(const std::string &relativePath, std::string data);

        // Null |key| emits an unencrypted container. Byte-identical for identical input, so an
        // unchanged resource keeps its hash and the delta transfer skips it.
        bool Build(const Crypto::Key *key, std::string &outBlob) const;

        size_t GetEntryCount() const {
            return _entries.size();
        }

      private:
        struct Entry {
            std::string path;
            std::string data;
        };

        std::vector<Entry> _entries;
    };

    // Validates the envelope and decrypts, yielding the ZIP bytes.
    bool Open(const std::string &blob, const Crypto::Key *key, std::string &outZip, std::string &outError);

    // Reads the flag without decrypting.
    bool IsEncrypted(const std::string &blob);
} // namespace Framework::Utils::Package
