/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"

#include <function2.hpp>
#include <unordered_map>

namespace Framework::GUI::CEF {

    using SchemaHandlerFactoryCallback = fu2::function<CefRefPtr<CefResourceHandler>(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &scheme_name, CefRefPtr<CefRequest> request) const>;

    struct SchemeDomainKey {
        std::string scheme;
        std::string domain;

        bool operator==(const SchemeDomainKey &o) const {
            return scheme == o.scheme && domain == o.domain;
        }
    };

    struct KeyHash {
        size_t operator()(const SchemeDomainKey &k) const {
            return std::hash<std::string> {}(k.scheme) ^ (std::hash<std::string> {}(k.domain) << 1);
        }
    };

    class App final
        : public CefApp
        , public CefBrowserProcessHandler
        , public CefRenderProcessHandler
        , public CefSchemeHandlerFactory {
      private:
        bool _contextInitialized = false;

      public:
        CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
            return this;
        }

        void OnBeforeCommandLineProcessing(const CefString &processType, CefRefPtr<CefCommandLine> commandLine) override;
        void OnContextInitialized() override;

        bool IsContextInitialized() const {
            return _contextInitialized;
        }

        virtual CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &scheme_name, CefRefPtr<CefRequest> request) override;

        void RegisterSchemeHandlerFactory(const std::string &schema, const std::string &domain, Framework::GUI::CEF::SchemaHandlerFactoryCallback callback);

        IMPLEMENT_REFCOUNTING(App);

        private:
        std::unordered_map<SchemeDomainKey, Framework::GUI::CEF::SchemaHandlerFactoryCallback, KeyHash> _handlers;
    };
} // namespace Framework::GUI::CEF
