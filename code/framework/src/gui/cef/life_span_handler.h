/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "include/cef_life_span_handler.h"

namespace Framework::GUI::CEF {
    class LifeSpanHandler final: public CefLifeSpanHandler {
      private:
        CefRefPtr<CefBrowser> _browser;

      public:
        void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
        bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popupId, const CefString &targetUrl, const CefString &targetFrameName, CefLifeSpanHandler::WindowOpenDisposition targetDisposition, bool userGesture, const CefPopupFeatures &popupFeatures, CefWindowInfo &windowInfo, CefRefPtr<CefClient> &client, CefBrowserSettings &settings, CefRefPtr<CefDictionaryValue> &extraInfo, bool *noJavascriptAccess) override;
        void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

        CefRefPtr<CefBrowser> GetBrowser() const {
            return _browser;
        }

        IMPLEMENT_REFCOUNTING(LifeSpanHandler);
    };
} // namespace Framework::GUI::CEF
