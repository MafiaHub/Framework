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
    namespace {
        std::string ToLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }
    } // namespace

    std::string LifeSpanHandler::OriginFromURL(const CefString &url) {
        CefURLParts parts;
        if (!CefParseURL(url, parts)) {
            return "";
        }
        std::string scheme     = CefString(&parts.scheme).ToString();
        std::string host       = CefString(&parts.host).ToString();
        const std::string port = CefString(&parts.port).ToString();
        if (scheme.empty() || host.empty()) {
            return "";
        }
        return ToLower(std::move(scheme)) + "://" + ToLower(std::move(host)) + (port.empty() ? "" : ":" + port);
    }

    std::string LifeSpanHandler::HostFromURL(const CefString &url) {
        CefURLParts parts;
        if (!CefParseURL(url, parts)) {
            return "";
        }
        return ToLower(CefString(&parts.host).ToString());
    }

    std::atomic<int> LifeSpanHandler::s_liveBrowserCount {0};

    void LifeSpanHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
        _browser = browser;
        ++s_liveBrowserCount;

        if (_onViewEvent) {
            ViewEventData data;
            data.event = ViewEvent::Created;
            data.url   = browser && browser->GetMainFrame() ? browser->GetMainFrame()->GetURL().ToString() : std::string();
            _onViewEvent(data);
        }
    }

    bool LifeSpanHandler::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popupId, const CefString &targetUrl, const CefString &targetFrameName, CefLifeSpanHandler::WindowOpenDisposition targetDisposition, bool userGesture, const CefPopupFeatures &popupFeatures, CefWindowInfo &windowInfo, CefRefPtr<CefClient> &client, CefBrowserSettings &settings, CefRefPtr<CefDictionaryValue> &extraInfo, bool *noJavascriptAccess) {
        if (_onViewEvent) {
            ViewEventData data;
            data.event     = ViewEvent::Popup;
            data.url       = targetUrl.ToString();
            data.openerUrl = frame ? frame->GetURL().ToString() : std::string();
            _onViewEvent(data);
        }

        // a popup needs its own OS window; block it and let the script react
        return true;
    }

    void LifeSpanHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
        _browser = nullptr;
        --s_liveBrowserCount;
    }

    bool LifeSpanHandler::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect) {
        const CefString requestUrl = request->GetURL();
        const bool isMainFrame     = frame && frame->IsMain();

        // refusals are reported on their own event too, so a script can watch rejections
        // without filtering every navigation
        const auto report = [&](bool blocked, ViewBlockReason reason) {
            if (!_onViewEvent) {
                return;
            }
            if (blocked) {
                ViewEventData blockedData;
                blockedData.event  = ViewEvent::ResourceBlocked;
                blockedData.url    = requestUrl.ToString();
                blockedData.domain = HostFromURL(requestUrl);
                blockedData.reason = reason;
                _onViewEvent(blockedData);
            }

            ViewEventData navigateData;
            navigateData.event       = ViewEvent::Navigate;
            navigateData.url         = requestUrl.ToString();
            navigateData.isMainFrame = isMainFrame;
            navigateData.blocked     = blocked;
            _onViewEvent(navigateData);
        };

        CefURLParts urlParts;
        if (!CefParseURL(requestUrl, urlParts)) {
            report(true, ViewBlockReason::InvalidURL);
            return true;
        }

        if (!_allowedOrigin.empty() && isMainFrame && OriginFromURL(requestUrl) != _allowedOrigin) {
            report(true, ViewBlockReason::CrossOrigin);
            return true;
        }

        const bool blocked = _onBeforeBrowse && _onBeforeBrowse(urlParts, browser, frame, request, userGesture, isRedirect);
        report(blocked, ViewBlockReason::HostFilter);
        return blocked;
    }
} // namespace Framework::GUI::CEF
