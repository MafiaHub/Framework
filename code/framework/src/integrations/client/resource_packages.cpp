/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "resource_packages.h"

#include <logging/logger.h>
#include <utils/package/package.h>
#include <utils/vfs.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace Framework::Integrations::Client {
    namespace {
        bool ReadWholeFile(const std::filesystem::path &path, std::string &out) {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                return false;
            }
            const auto size = file.tellg();
            if (size < 0) {
                return false;
            }
            file.seekg(0, std::ios::beg);
            out.resize(static_cast<size_t>(size));
            if (size > 0) {
                file.read(out.data(), size);
                if (file.fail() || file.gcount() != size) {
                    out.clear();
                    return false;
                }
            }
            return true;
        }

        // PhysicsFS picks its archiver from the extension.
        std::string MountId(const std::string &resourceName) {
            return resourceName + ".zip";
        }
    } // namespace

    ResourcePackageMounter::~ResourcePackageMounter() {
        ClearKey();
    }

    bool ResourcePackageMounter::SetKey(const std::string &hexKey) {
        if (hexKey.size() != Utils::Crypto::kKeySize * 2) {
            ClearKey();
            return false;
        }
        if (!Utils::Crypto::FromHex(hexKey, _key.data(), _key.size())) {
            ClearKey();
            return false;
        }
        _hasKey = true;
        return true;
    }

    void ResourcePackageMounter::ClearKey() {
        Utils::Crypto::SecureZero(_key.data(), _key.size());
        _hasKey = false;
    }

    bool ResourcePackageMounter::Mount(const std::string &cacheRoot, const std::string &resourceName, const std::string &expectedHash, std::string &outError) {
        if (!_hasKey) {
            outError = "no package key was received from the server";
            return false;
        }
        if (expectedHash.empty()) {
            outError = "the server announced no package hash for this resource";
            return false;
        }

        const auto packagePath = std::filesystem::path(cacheRoot) / (resourceName + Utils::Package::kExtension);

        std::string blob;
        if (!ReadWholeFile(packagePath, blob)) {
            outError = "package '" + packagePath.string() + "' is missing or unreadable";
            return false;
        }

        // Verified before it is decrypted.
        const std::string actualHash = Utils::Crypto::Sha256Hex(blob);
        if (!Utils::Crypto::ConstantTimeEquals(actualHash, expectedHash)) {
            outError = "package hash mismatch (expected " + expectedHash.substr(0, 16) + ", got " + actualHash.substr(0, 16) + ")";
            return false;
        }

        if (!Utils::Package::IsEncrypted(blob)) {
            outError = "package is not encrypted";
            return false;
        }

        std::string zip;
        if (!Utils::Package::Open(blob, &_key, zip, outError)) {
            return false;
        }

        // Replaced wholesale so a refresh cannot leave stale files mounted.
        const std::string id = MountId(resourceName);
        Utils::Vfs::Get().Unmount(id);

        if (!Utils::Vfs::Get().MountMemory(std::move(zip), id, Utils::Vfs::ResourcePath(resourceName), outError)) {
            return false;
        }

        if (std::find(_mounted.begin(), _mounted.end(), resourceName) == _mounted.end()) {
            _mounted.push_back(resourceName);
        }

        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Mounted resource package '{}' at {}", resourceName, Utils::Vfs::ResourcePath(resourceName));
        return true;
    }

    void ResourcePackageMounter::Unmount(const std::string &cacheRoot, const std::string &resourceName) {
        (void)cacheRoot;
        Utils::Vfs::Get().Unmount(MountId(resourceName));
        _mounted.erase(std::remove(_mounted.begin(), _mounted.end(), resourceName), _mounted.end());
    }

    void ResourcePackageMounter::Reset() {
        for (const auto &resourceName : _mounted) {
            Utils::Vfs::Get().Unmount(MountId(resourceName));
        }
        _mounted.clear();
        ClearKey();
    }
} // namespace Framework::Integrations::Client
