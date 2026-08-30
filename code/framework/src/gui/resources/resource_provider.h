/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "resource_stream.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Framework::GUI::Resources {
    struct ResourceStat {
        std::uint64_t size = 0;

        // Content-addressed, so it can carry a one-year cache instead of the
        // revalidate-always default.
        bool immutable = false;

        // Overrides the extension-derived MIME type when non-empty.
        std::string mimeType;
    };

    // Backs one URL host with bytes.
    class ResourceProvider {
      public:
        virtual ~ResourceProvider() = default;

        // |path| arrives percent-decoded, without its leading slash, query or
        // fragment, and already rejected for traversal. Null when it names
        // nothing. Called from a CEF file thread, so it may block, and must be
        // safe to call concurrently.
        virtual std::unique_ptr<ResourceStream> Open(const std::string &path, ResourceStat &stat) const = 0;

        // Names the backing store in logs.
        virtual std::string Describe() const = 0;
    };
} // namespace Framework::GUI::Resources
