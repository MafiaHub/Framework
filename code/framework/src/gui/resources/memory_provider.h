/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "resource_provider.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace Framework::GUI::Resources {
    // Serves a host out of bytes held in the process: a page built at runtime, a
    // bundle unpacked from the executable, or a fixture in a test. Entries may
    // be added or replaced while responses are in flight; a response already
    // opened keeps serving the bytes it started with.
    class MemoryProvider final: public ResourceProvider {
      private:
        struct Entry {
            std::string data;
            std::string mimeType;
        };

        mutable std::mutex _mutex;
        std::unordered_map<std::string, Entry> _entries;
        std::string _name;

      public:
        explicit MemoryProvider(std::string name): _name(std::move(name)) {}

        // |path| is the request path without its leading slash, e.g. "index.html".
        // An empty |mimeType| leaves the type to the extension table.
        void Set(const std::string &path, std::string data, std::string mimeType = {});
        bool Erase(const std::string &path);

        std::unique_ptr<ResourceStream> Open(const std::string &path, ResourceStat &stat) const override;
        std::string Describe() const override;
    };
} // namespace Framework::GUI::Resources
