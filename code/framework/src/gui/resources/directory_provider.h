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
    //
    // The registry has already rejected traversal at the URL level; this class
    // rejects it again against the resolved path, because the two catch
    // different things. The URL check stops an encoded "..", and the canonical
    // check stops a symlink or junction inside the root from pointing out of it.
    class DirectoryProvider final: public ResourceProvider {
      private:
        // Open() runs on a CEF file thread while SetRoot() is called from the
        // game thread, so the root is not simply a member read in passing.
        mutable std::mutex _mutex;
        std::filesystem::path _root;

        // Everything under these path prefixes is content-addressed by the
        // bundler and gets an immutable cache. Prefixes are matched against the
        // request path with forward slashes. Fixed after construction.
        std::vector<std::string> _immutablePrefixes;

        static std::filesystem::path Resolve(std::filesystem::path root);

      public:
        explicit DirectoryProvider(std::filesystem::path root);

        // Marks a subtree as content-hashed, e.g. MarkImmutable("assets/").
        // Call before the provider is registered.
        void MarkImmutable(std::string pathPrefix);

        // Points the host at a different directory. Responses already open keep
        // reading the file they opened.
        void SetRoot(std::filesystem::path root);

        std::unique_ptr<ResourceStream> Open(const std::string &path, ResourceStat &stat) const override;
        std::string Describe() const override;

        // Absolute, canonical root. Empty when the directory did not resolve.
        std::filesystem::path GetRoot() const;
    };
} // namespace Framework::GUI::Resources
