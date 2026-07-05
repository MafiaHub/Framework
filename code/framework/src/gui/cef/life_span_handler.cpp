/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "life_span_handler.h"
#include "include/cef_parser.h"

#include <algorithm>
#include <cctype>

namespace Framework::GUI::CEF {
    std::string LifeSpanHandler::OriginFromURL(const CefString &url) {
        CefURLParts parts;
        if (!CefParseURL(url, parts)) {
            return "";
        }
        std::string scheme = CefString(&parts.scheme).ToString();
        std::string host   = CefString(&parts.host).ToString();
        const std::string port = CefString(&parts.port).ToString();
        if (scheme.empty() || host.empty()) {
            return "";
        }
        const auto lower = [](std::string &s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
        };
        lower(scheme);
        lower(host);
        return scheme + "://" + host + (port.empty() ? "" : ":" + port);
    }

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

        if (!_allowedOrigin.empty() && frame && frame->IsMain() && OriginFromURL(request->GetURL()) != _allowedOrigin) {
            return true; // origin lock: no cross-origin main-frame navigation
        }

        if (_onBeforeBrowse) {
            return _onBeforeBrowse(urlParts, browser, frame, request, userGesture, isRedirect);
        }
        return false;

    }
} // namespace Framework::GUI::CEF
