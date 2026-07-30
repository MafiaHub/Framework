/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "gui/view_events.h"

#include "include/cef_load_handler.h"

namespace Framework::GUI::CEF {
    class LoadHandler final: public CefLoadHandler {
      private:
        OnViewEventCallback _onViewEvent;

      public:
        void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transitionType) override;
        void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) override;
        void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString &errorText, const CefString &failedUrl) override;

        void SetViewEventCallback(OnViewEventCallback cb) {
            _onViewEvent = std::move(cb);
        }

        IMPLEMENT_REFCOUNTING(LoadHandler);
    };
} // namespace Framework::GUI::CEF
