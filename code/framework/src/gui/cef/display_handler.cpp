/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "display_handler.h"

#include <logging/logger.h>

namespace Framework::GUI::CEF {
    bool DisplayHandler::OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString &message, const CefString &source, int line) {
        const auto msg    = message.ToString();
        const auto src    = source.ToString();
        const auto logger = Framework::Logging::GetLogger("Web/JS");

        switch (level) {
        case LOGSEVERITY_DEBUG:
            logger->debug("[{}:{}] {}", src, line, msg);
            break;
        case LOGSEVERITY_INFO:
        case LOGSEVERITY_DEFAULT:
            logger->info("[{}:{}] {}", src, line, msg);
            break;
        case LOGSEVERITY_WARNING:
            logger->warn("[{}:{}] {}", src, line, msg);
            break;
        case LOGSEVERITY_ERROR:
            logger->error("[{}:{}] {}", src, line, msg);
            break;
        case LOGSEVERITY_FATAL:
            logger->critical("[{}:{}] {}", src, line, msg);
            break;
        default:
            logger->info("[{}:{}] {}", src, line, msg);
            break;
        }

        if (_onViewEvent) {
            ViewEventData data;
            data.event    = ViewEvent::ConsoleMessage;
            data.message  = msg;
            data.source   = src;
            data.line     = line;
            data.severity = static_cast<int>(level);
            _onViewEvent(data);
        }

        return true;
    }

    bool DisplayHandler::OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor, cef_cursor_type_t type, const CefCursorInfo &customCursorInfo) {
        const bool changed = _currentCursorType != type;
        _currentCursorType = type;

        // Blink re-asserts the cursor every hover tick; report transitions only.
        if (changed && _onViewEvent) {
            ViewEventData data;
            data.event      = ViewEvent::CursorChange;
            data.cursorType = static_cast<int>(type);
            _onViewEvent(data);
        }

        return true;
    }

    bool DisplayHandler::OnTooltip(CefRefPtr<CefBrowser> browser, CefString &text) {
        if (_onViewEvent) {
            ViewEventData data;
            data.event   = ViewEvent::Tooltip;
            data.tooltip = text.ToString();
            _onViewEvent(data);
        }

        // windowless: no window to host a native tooltip, the script draws it
        return true;
    }
} // namespace Framework::GUI::CEF
