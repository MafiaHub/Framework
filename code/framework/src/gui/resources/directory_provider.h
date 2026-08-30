/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "resource_provider.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace Framework::GUI::Resources {
    // Serves a host out of one directory on disk.
    class DirectoryProvider final: public ResourceProvider {
      private:
        // Open() runs on a CEF file thread, SetRoot() on the game thread.
        mutable std::mutex _mutex;
        std::filesystem::path _root;

        // Request-path prefixes whose contents are content-addressed. Fixed
        // after construction.
        std::vector<std::string> _immutablePrefixes;

        static std::filesystem::path Resolve(std::filesystem::path root);

      public:
        explicit DirectoryProvider(std::filesystem::path root);

        // e.g. MarkImmutable("assets/"). Call before registering the provider.
        void MarkImmutable(std::string pathPrefix);

        // Responses already open keep reading the file they opened.
        void SetRoot(std::filesystem::path root);

        std::unique_ptr<ResourceStream> Open(const std::string &path, ResourceStat &stat) const override;
        std::string Describe() const override;

        // Absolute and canonical; empty when the directory did not resolve.
        std::filesystem::path GetRoot() const;
    };
} // namespace Framework::GUI::Resources
