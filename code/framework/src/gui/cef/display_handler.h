/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "gui/view_events.h"

#include "include/cef_display_handler.h"

namespace Framework::GUI::CEF {
    class DisplayHandler final: public CefDisplayHandler {
      private:
        OnViewEventCallback _onViewEvent;
        cef_cursor_type_t _currentCursorType = CT_POINTER;

      public:
        bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString &message, const CefString &source, int line) override;
        bool OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor, cef_cursor_type_t type, const CefCursorInfo &customCursorInfo) override;
        bool OnTooltip(CefRefPtr<CefBrowser> browser, CefString &text) override;

        cef_cursor_type_t GetCursorType() const {
            return _currentCursorType;
        }

        void SetViewEventCallback(OnViewEventCallback cb) {
            _onViewEvent = std::move(cb);
        }

        IMPLEMENT_REFCOUNTING(DisplayHandler);
    };
} // namespace Framework::GUI::CEF
