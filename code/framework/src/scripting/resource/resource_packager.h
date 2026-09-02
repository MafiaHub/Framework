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

#include <string>
#include <vector>

namespace Framework::Scripting {
    struct PackagedResource {
        std::string name;
        std::string blob;   // the .fwpak container
        std::string sha256; // hex digest of |blob|
        size_t fileCount = 0;
    };

    // Packages the files a resource ships to clients: package.json and the client entry point
    // always, then mafiahub.clientFiles globs, or a filtered scan of the entry's directory when
    // none are declared.
    class ResourcePackager final {
      public:
        // Null |key| emits an unencrypted container.
        static bool Package(const std::string &resourceName, const std::string &resourcePath, const PackageManifest &manifest, const Utils::Crypto::Key *key, PackagedResource &out, std::string &outError);

        // Only consulted by the undeclared-clientFiles scan.
        static bool IsClientAssetExtension(const std::string &extension);

        // '*' within a segment, '**' across segments, '?' one character. |path| uses '/'.
        static bool MatchGlob(const std::string &pattern, const std::string &path);
    };
} // namespace Framework::Scripting
