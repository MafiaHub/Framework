/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "resource_provider.h"

#include "include/cef_scheme.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Framework::GUI::Resources {
    // Maps the scheme's hosts onto providers. Registered with CEF once for the
    // whole scheme; roots come and go behind it without touching CEF again.
    class ResourceRegistry final: public CefSchemeHandlerFactory {
      private:
        // Create() runs on the CEF IO thread, registration on the game thread.
        mutable std::mutex _mutex;
        std::unordered_map<std::string, std::shared_ptr<ResourceProvider>> _roots;

      public:
        // Replaces whatever held |host|. Responses already in flight keep the
        // previous provider alive and finish against it.
        void RegisterRoot(const std::string &host, std::shared_ptr<ResourceProvider> provider);
        bool UnregisterRoot(const std::string &host);
        bool HasRoot(const std::string &host) const;

        CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &schemeName, CefRefPtr<CefRequest> request) override;

        IMPLEMENT_REFCOUNTING(ResourceRegistry);
    };
} // namespace Framework::GUI::Resources
