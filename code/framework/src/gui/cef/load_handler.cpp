/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "load_handler.h"

namespace Framework::GUI::CEF {
    void LoadHandler::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transitionType) {
        if (!_onViewEvent || !frame) {
            return;
        }

        ViewEventData data;
        data.event       = ViewEvent::LoadingStart;
        data.url         = frame->GetURL().ToString();
        data.isMainFrame = frame->IsMain();
        _onViewEvent(data);
    }

    void LoadHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {
        // main frame only: sub-frames finish independently of "the document is ready"
        if (!_onViewEvent || !frame || !frame->IsMain()) {
            return;
        }

        ViewEventData data;
        data.event       = ViewEvent::DocumentReady;
        data.url         = frame->GetURL().ToString();
        data.isMainFrame = true;
        _onViewEvent(data);
    }

    void LoadHandler::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString &errorText, const CefString &failedUrl) {
        if (!_onViewEvent) {
            return;
        }

        ViewEventData data;
        data.event       = ViewEvent::LoadingFailed;
        data.url         = failedUrl.ToString();
        data.description = errorText.ToString();
        data.errorCode   = static_cast<int>(errorCode);
        data.isMainFrame = frame && frame->IsMain();
        _onViewEvent(data);
    }
} // namespace Framework::GUI::CEF
