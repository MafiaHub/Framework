/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <scripting/resource/package_manifest.h>
#include <utils/crypto.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>

namespace Framework::Integrations::Server {
    // Everything packaging one resource reads, copied out of the tick thread's objects so the build
    // can run on another thread.
    struct ClientPackageRequest {
        std::string name;
        std::string path;
        Scripting::PackageManifest manifest;
        Utils::Crypto::Key key {};
        std::set<std::string> serverOnlyResources;
        std::filesystem::path stagingDir;
    };

    struct ClientPackage {
        std::string name;
        // False when the resource could not be packaged; `error` says why.
        bool built = false;
        std::string error;
        std::string sha256;
        uint64_t size    = 0;
        size_t fileCount = 0;
        // Where clients fetch the container from: <staging>/<name>.fwpak.
        std::filesystem::path served;
        // The new container, written beside `served` and waiting to replace it. Empty when `served`
        // already holds these bytes.
        std::filesystem::path pending;
    };

    // Packages the resource and writes the container beside the served one, with its chunk list. The
    // served file is not touched, so what the server has announced stays what it serves. Any thread;
    // one call at a time per resource.
    ClientPackage BuildClientPackage(const ClientPackageRequest &request);

    // Puts a pending container where it is served from. Tick thread, right before the new hash is
    // announced. False when the package was not built or could not be put in place: it must not be
    // announced then.
    bool CommitClientPackage(ClientPackage &package);

    // Removes a pending container that will not be committed.
    void DiscardClientPackage(ClientPackage &package);
} // namespace Framework::Integrations::Server
