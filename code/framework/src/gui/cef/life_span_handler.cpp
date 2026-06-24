/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "life_span_handler.h"
#include "include/cef_parser.h"

namespace Framework::GUI::CEF {
    std::atomic<int> LifeSpanHandler::s_liveBrowserCount {0};

    void LifeSpanHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
        _browser = browser;
        ++s_liveBrowserCount;

        if (_onAfterCreated) {
            _onAfterCreated(browser);
        }
    }

    bool LifeSpanHandler::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popupId, const CefString &targetUrl, const CefString &targetFrameName, CefLifeSpanHandler::WindowOpenDisposition targetDisposition, bool userGesture, const CefPopupFeatures &popupFeatures, CefWindowInfo &windowInfo, CefRefPtr<CefClient> &client, CefBrowserSettings &settings, CefRefPtr<CefDictionaryValue> &extraInfo, bool *noJavascriptAccess) {
        // Block all popups in game mod context
        return true;
    }

    void LifeSpanHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
        _browser = nullptr;
        --s_liveBrowserCount;
    }

    bool LifeSpanHandler::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect) {
        CefURLParts urlParts;
        if (!CefParseURL(request->GetURL(), urlParts))
            return true; // Cancel if invalid URL

        if (_onBeforeBrowse) {
            return _onBeforeBrowse(urlParts, browser, frame, request, userGesture, isRedirect);
        }
        return false;

    }
} // namespace Framework::GUI::CEF
