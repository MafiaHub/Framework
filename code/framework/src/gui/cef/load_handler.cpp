/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "load_handler.h"

namespace Framework::GUI::CEF {
    void LoadHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {
        if (!frame->IsMain()) {
            return;
        }

        if (_onDOMReadyCallback) {
            _onDOMReadyCallback(frame->GetIdentifier().ToString(), true, frame->GetURL().ToString());
        }
    }

    void LoadHandler::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transitionType) {
        if (!frame->IsMain()) {
            return;
        }

        if (_onWindowObjectReadyCallback) {
            _onWindowObjectReadyCallback(frame->GetIdentifier().ToString(), true, frame->GetURL().ToString());
        }
    }
} // namespace Framework::GUI::CEF
