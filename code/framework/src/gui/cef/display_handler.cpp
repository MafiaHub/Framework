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

        if (_onConsoleMessageCallback) {
            _onConsoleMessageCallback(msg, static_cast<uint32_t>(line), 0, src);
        }

        return true;
    }

    bool DisplayHandler::OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor, cef_cursor_type_t type, const CefCursorInfo &customCursorInfo) {
        _currentCursorType = type;
        return true;
    }
} // namespace Framework::GUI::CEF
