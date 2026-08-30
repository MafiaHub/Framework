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
        // Body length in bytes, sent as the response length.
        std::uint64_t size = 0;

        // The bytes behind this URL are guaranteed never to change, because the
        // name carries a content hash or the provider is immutable by
        // construction. Served with a one-year immutable cache instead of the
        // revalidate-always default.
        bool immutable = false;

        // Overrides the extension-derived MIME type when non-empty. A provider
        // that knows better than the file name says so here.
        std::string mimeType;
    };

    // Backs one URL host with bytes.
    //
    // Open() is called from a CEF file-thread task and may block. It must be
    // safe to call concurrently, because several responses from the same host
    // can be in flight at once.
    class ResourceProvider {
      public:
        virtual ~ResourceProvider() = default;

        // |path| arrives already percent-decoded, stripped of its leading slash
        // and of any query or fragment, and rejected for traversal, so an
        // implementation may map it onto its own namespace directly. Returns
        // null when the path names nothing.
        virtual std::unique_ptr<ResourceStream> Open(const std::string &path, ResourceStat &stat) const = 0;

        // Names the backing store in logs. Not part of any response.
        virtual std::string Describe() const = 0;
    };
} // namespace Framework::GUI::Resources
