/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "package_manifest.h"

#include <utils/crypto.h>

#include <set>
#include <string>
#include <vector>

namespace Framework::Scripting {
    struct PackagedResource {
        std::string name;
        std::string blob;   // the .fwpak container
        std::string sha256; // hex digest of |blob|
        size_t fileCount = 0;
    };

    // Packages what a resource ships: package.json, its client and shared scripts, then
    // mafiahub.files globs, or a filtered scan of the script directories when none are declared.
    class ResourcePackager final {
      public:
        // Null |key| emits an unencrypted container. |serverOnlyResources| names the resources
        // clients never receive; the shipped package.json drops its dependencies on them, since
        // the client validates dependencies against only the resources it was sent.
        static bool Package(const std::string &resourceName, const std::string &resourcePath, const PackageManifest &manifest, const Utils::Crypto::Key *key, PackagedResource &out, std::string &outError, const std::set<std::string> &serverOnlyResources = {});

        // Rewrites |packageJson| without the resourceDependencies entries naming a resource in
        // |serverOnlyResources|. False, leaving |out| untouched, when there is nothing to drop.
        static bool StripServerOnlyDependencies(const std::string &packageJson, const std::set<std::string> &serverOnlyResources, std::string &out);

        // Only consulted by the scan used when mafiahub.files is absent.
        static bool IsClientAssetExtension(const std::string &extension);

        // '*' within a segment, '**' across segments, '?' one character. |path| uses '/'.
        static bool MatchGlob(const std::string &pattern, const std::string &path);
    };
} // namespace Framework::Scripting
