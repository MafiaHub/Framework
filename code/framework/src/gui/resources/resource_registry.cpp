/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "resource_registry.h"

#include "resource_handler.h"
#include "resource_path.h"
#include "scheme.h"

#include <logging/logger.h>

#include "include/cef_parser.h"
#include "include/cef_request.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Framework::GUI::Resources {
    namespace {
        constexpr const char *kLogger = "Web";

        std::string ToLower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }
    } // namespace

    void ResourceRegistry::RegisterRoot(const std::string &host, std::shared_ptr<ResourceProvider> provider) {
        const std::string key = ToLower(host);
        Framework::Logging::GetLogger(kLogger)->info("Serving {}://{}/ from {}", kResourceScheme, key, provider ? provider->Describe() : "<null>");

        std::scoped_lock lock(_mutex);
        _roots[key] = std::move(provider);
    }

    bool ResourceRegistry::UnregisterRoot(const std::string &host) {
        std::scoped_lock lock(_mutex);
        return _roots.erase(ToLower(host)) > 0;
    }

    bool ResourceRegistry::HasRoot(const std::string &host) const {
        std::scoped_lock lock(_mutex);
        return _roots.count(ToLower(host)) > 0;
    }

    CefRefPtr<CefResourceHandler> ResourceRegistry::Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &schemeName, CefRefPtr<CefRequest> request) {
        const std::string url = request->GetURL().ToString();

        CefURLParts parts;
        if (!CefParseURL(url, parts)) {
            Framework::Logging::GetLogger(kLogger)->warn("Rejecting unparsable resource URL '{}'", url);
            return new ResourceHandler(400, "400 Bad Request", "", "");
        }

        const std::string host = ToLower(CefString(&parts.host).ToString());

        std::string path;
        if (!NormalizeResourcePath(CefString(&parts.path).ToString(), path)) {
            Framework::Logging::GetLogger(kLogger)->warn("Rejecting resource path outside the root: '{}'", url);
            return new ResourceHandler(403, "403 Forbidden", host, "");
        }

        std::shared_ptr<ResourceProvider> provider;
        {
            std::scoped_lock lock(_mutex);
            const auto root = _roots.find(host);
            if (root != _roots.end()) {
                provider = root->second;
            }
        }

        if (!provider) {
            Framework::Logging::GetLogger(kLogger)->warn("No resource root is registered for {}://{}", kResourceScheme, host);
            return new ResourceHandler(404, "404 Not Found", host, path);
        }

        return new ResourceHandler(std::move(provider), host, std::move(path));
    }
} // namespace Framework::GUI::Resources
