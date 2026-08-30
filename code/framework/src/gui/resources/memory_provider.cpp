/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "memory_provider.h"

#include <utility>

namespace Framework::GUI::Resources {
    void MemoryProvider::Set(const std::string &path, std::string data, std::string mimeType) {
        std::scoped_lock lock(_mutex);
        _entries[path] = Entry {std::move(data), std::move(mimeType)};
    }

    bool MemoryProvider::Erase(const std::string &path) {
        std::scoped_lock lock(_mutex);
        return _entries.erase(path) > 0;
    }

    std::unique_ptr<ResourceStream> MemoryProvider::Open(const std::string &path, ResourceStat &stat) const {
        std::scoped_lock lock(_mutex);
        const auto entry = _entries.find(path);
        if (entry == _entries.end()) {
            return nullptr;
        }

        stat.size     = entry->second.data.size();
        stat.mimeType = entry->second.mimeType;
        return std::make_unique<MemoryStream>(entry->second.data);
    }

    std::string MemoryProvider::Describe() const {
        return "memory:" + _name;
    }
} // namespace Framework::GUI::Resources
