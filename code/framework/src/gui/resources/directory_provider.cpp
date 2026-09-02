/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "directory_provider.h"

#include <utils/vfs.h>

#include <system_error>
#include <utility>

namespace Framework::GUI::Resources {
    std::filesystem::path DirectoryProvider::Resolve(std::filesystem::path root) {
        std::error_code error;
        std::filesystem::path resolved = std::filesystem::weakly_canonical(std::filesystem::absolute(std::move(root), error), error);
        return error ? std::filesystem::path {} : resolved;
    }

    DirectoryProvider::DirectoryProvider(std::filesystem::path root): _root(Resolve(std::move(root))) {}

    void DirectoryProvider::MarkImmutable(std::string pathPrefix) {
        _immutablePrefixes.push_back(std::move(pathPrefix));
    }

    void DirectoryProvider::SetVirtualPrefix(std::string prefix) {
        std::scoped_lock lock(_mutex);
        _virtualPrefix = std::move(prefix);
    }

    void DirectoryProvider::SetRoot(std::filesystem::path root) {
        std::filesystem::path resolved = Resolve(std::move(root));

        std::scoped_lock lock(_mutex);
        _root = std::move(resolved);
    }

    std::filesystem::path DirectoryProvider::GetRoot() const {
        std::scoped_lock lock(_mutex);
        return _root;
    }

    std::unique_ptr<ResourceStream> DirectoryProvider::Open(const std::string &path, ResourceStat &stat) const {
        const std::filesystem::path root = GetRoot();
        if (root.empty()) {
            return nullptr;
        }

        std::error_code error;
        const std::filesystem::path joined = root / std::filesystem::path(path).lexically_normal();
        const std::filesystem::path file   = std::filesystem::weakly_canonical(joined, error);
        if (error) {
            return nullptr;
        }

        // relative() starts with ".." exactly when |file| is outside |root|. The
        // URL check already refused an encoded "..", but only this one catches a
        // symlink inside the root pointing out of it.
        const std::filesystem::path relative = std::filesystem::relative(file, root, error);
        if (error || relative.empty() || *relative.begin() == "..") {
            return nullptr;
        }

        // After the traversal guard, so a mounted path gets the same containment rules as a file.
        // Contains() rather than Read(): Read() falls back to disk and would buffer a real file
        // that FileStream should be streaming.
        const auto &vfs = Utils::Vfs::Get();
        std::string virtualPath;
        {
            std::scoped_lock lock(_mutex);
            if (!_virtualPrefix.empty()) {
                virtualPath = Utils::Vfs::NormalizeVirtual(_virtualPrefix + "/" + path);
            }
        }

        std::string mounted;
        if (!virtualPath.empty() && vfs.Contains(virtualPath) && vfs.Read(virtualPath, mounted)) {
            stat.size = static_cast<std::uint64_t>(mounted.size());
            for (const auto &prefix : _immutablePrefixes) {
                if (path.rfind(prefix, 0) == 0) {
                    stat.immutable = true;
                    break;
                }
            }
            return std::make_unique<MemoryStream>(std::move(mounted));
        }

        if (!std::filesystem::is_regular_file(file, error) || error) {
            return nullptr;
        }

        const std::uintmax_t size = std::filesystem::file_size(file, error);
        if (error) {
            return nullptr;
        }

        auto stream = std::make_unique<FileStream>(file);
        if (!stream->IsOpen()) {
            return nullptr;
        }

        stat.size = static_cast<std::uint64_t>(size);
        for (const auto &prefix : _immutablePrefixes) {
            if (path.rfind(prefix, 0) == 0) {
                stat.immutable = true;
                break;
            }
        }
        return stream;
    }

    std::string DirectoryProvider::Describe() const {
        const std::filesystem::path root = GetRoot();
        return root.empty() ? "<unresolved directory>" : root.string();
    }
} // namespace Framework::GUI::Resources
