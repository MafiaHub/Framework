/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/crypto.h>

#include <string>
#include <vector>

namespace Framework::Integrations::Client {
    // Verifies each downloaded .fwpak against the hash the server announced, decrypts it, and
    // mounts it at /resources/<name>. A resource that fails any step is not mounted.
    class ResourcePackageMounter final {
      public:
        ~ResourcePackageMounter();

        // |hexKey| comes from the ServerResources RPC.
        bool SetKey(const std::string &hexKey);
        bool HasKey() const {
            return _hasKey;
        }
        void ClearKey();

        // |expectedHash| is the hex SHA-256 of the container.
        bool Mount(const std::string &cacheRoot, const std::string &resourceName, const std::string &expectedHash, std::string &outError);

        void Unmount(const std::string &cacheRoot, const std::string &resourceName);

        // Unmounts everything and forgets the key.
        void Reset();

        const std::vector<std::string> &GetMountedResources() const {
            return _mounted;
        }

      private:
        Utils::Crypto::Key _key {};
        bool _hasKey = false;
        std::vector<std::string> _mounted;
    };
} // namespace Framework::Integrations::Client
