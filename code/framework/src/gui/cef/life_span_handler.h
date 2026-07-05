/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "include/cef_life_span_handler.h"
#include "include/cef_request_handler.h"
#include <atomic>
#include <functional>
#include <string>

namespace Framework::GUI::CEF {
    using OnBeforeBrowseCallback = std::function<bool(CefURLParts, CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, bool, bool)>;
    
    class LifeSpanHandler final
        : public CefLifeSpanHandler
        , public CefRequestHandler {
      private:
        CefRefPtr<CefBrowser> _browser;

        std::function<void(CefRefPtr<CefBrowser>)> _onAfterCreated;
        OnBeforeBrowseCallback _onBeforeBrowse;

        // Non-empty = origin lock: rejects cross-origin main-frame navigation and page events.
        std::string _allowedOrigin;

        // Live browsers (pre-OnBeforeClose); Shutdown drains this before CefShutdown.
        static std::atomic<int> s_liveBrowserCount;

      public:
        static int GetLiveBrowserCount() {
            return s_liveBrowserCount.load();
        }

        void SetOnAfterCreatedCallback(std::function<void(CefRefPtr<CefBrowser>)> cb) {
            _onAfterCreated = std::move(cb);
        }
        void SetOnBeforeBrowseCallback(OnBeforeBrowseCallback cb) {
            _onBeforeBrowse = std::move(cb);
        }

        void SetAllowedOrigin(std::string origin) {
            _allowedOrigin = std::move(origin);
        }

        const std::string &GetAllowedOrigin() const {
            return _allowedOrigin;
        }

        // "scheme://host[:port]" lowercased, empty when unparsable (data:, about:).
        static std::string OriginFromURL(const CefString &url);

        void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
        bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popupId, const CefString &targetUrl, const CefString &targetFrameName, CefLifeSpanHandler::WindowOpenDisposition targetDisposition, bool userGesture, const CefPopupFeatures &popupFeatures, CefWindowInfo &windowInfo, CefRefPtr<CefClient> &client, CefBrowserSettings &settings, CefRefPtr<CefDictionaryValue> &extraInfo, bool *noJavascriptAccess) override;
        void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
        bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect) override;

        CefRefPtr<CefBrowser> GetBrowser() const {
            return _browser;
        }

        IMPLEMENT_REFCOUNTING(LifeSpanHandler);
    };
} // namespace Framework::GUI::CEF
