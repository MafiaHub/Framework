/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <fu2/function2.hpp>
#include <string>

namespace Framework::GUI {
    // Reported by CEF, never by page content — page events go through SDK::BroadcastEvent instead.
    enum class ViewEvent {
        Created,
        LoadingStart,
        DocumentReady,
        LoadingFailed,
        Navigate,
        Popup,
        CursorChange,
        Tooltip,
        InputFocusChange,
        ResourceBlocked,
        ConsoleMessage,
        OriginChange,
    };

    enum class ViewBlockReason {
        CrossOrigin,
        InvalidURL,
        HostFilter,
        ForeignEvent,
    };

    // Each event fills only its own fields; the rest keep their defaults.
    // Per-event field lists live in docs/scripting_web_events.md.
    struct ViewEventData {
        ViewEvent event = ViewEvent::Created;

        std::string url;
        std::string openerUrl;
        std::string origin;
        std::string domain;
        std::string description;
        std::string tooltip;
        std::string message;
        std::string source;

        int errorCode  = 0;
        int cursorType = 0;
        int line       = 0;
        int severity   = 0;

        ViewBlockReason reason = ViewBlockReason::CrossOrigin;

        bool isMainFrame = false;
        bool blocked     = false;
        bool focused     = false;
    };

    using OnViewEventCallback = fu2::function<void(const ViewEventData &) const>;
} // namespace Framework::GUI
