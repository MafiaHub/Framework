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
    // Maps the hosts of the framework resource scheme onto providers, and turns
    // each request into a handler. Registered once with CEF for the whole
    // scheme; roots come and go behind it without touching CEF again.
    class ResourceRegistry final: public CefSchemeHandlerFactory {
      private:
        // Create() runs on the CEF IO thread per request; roots are registered
        // from the game thread.
        mutable std::mutex _mutex;
        std::unordered_map<std::string, std::shared_ptr<ResourceProvider>> _roots;

      public:
        // Claims |host| for |provider|, replacing whatever held it. Responses
        // already in flight from the previous provider keep it alive and finish
        // against it.
        void RegisterRoot(const std::string &host, std::shared_ptr<ResourceProvider> provider);
        bool UnregisterRoot(const std::string &host);
        bool HasRoot(const std::string &host) const;

        CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &schemeName, CefRefPtr<CefRequest> request) override;

        IMPLEMENT_REFCOUNTING(ResourceRegistry);
    };
} // namespace Framework::GUI::Resources
