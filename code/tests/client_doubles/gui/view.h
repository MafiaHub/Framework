/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// A stand-in for a web view, for FrameworkClientScriptingTests only: this directory is ahead of the
// framework's on the include path of that binary, so the client Web builtin, which includes <gui/view.h> and
// <gui/manager.h>, gets these two. The real headers need CEF and Direct3D and build on Windows alone. The
// view here has the functions the builtin calls, under their real signatures, and keeps what it was told
// where a test reads it; nothing is loaded and nothing is drawn. gui/view_events.h is the real one.

#include <gui/view_events.h>

#include <function2/function2.hpp>

#include <map>
#include <string>
#include <utility>
#include <vector>

// This is a second definition of GUI::View. It is sound only because nothing in the binary sees the real
// one: code/tests/CMakeLists.txt fails the configuration if FrameworkClientScriptingTests ever links
// FrameworkClient. Do not include this from, or add this directory to, any other target.

// The two enumerations of CEF the builtin names values of, with the values of the CEF the framework pins
// (include/internal/cef_types.h of 156.0.14).
typedef enum {
    LOGSEVERITY_DEFAULT,
    LOGSEVERITY_VERBOSE,
    LOGSEVERITY_DEBUG = LOGSEVERITY_VERBOSE,
    LOGSEVERITY_INFO,
    LOGSEVERITY_WARNING,
    LOGSEVERITY_ERROR,
    LOGSEVERITY_FATAL,
    LOGSEVERITY_DISABLE = 99
} cef_log_severity_t;

typedef enum {
    CT_POINTER,
    CT_CROSS,
    CT_HAND,
    CT_IBEAM,
    CT_WAIT,
    CT_HELP,
    CT_EASTRESIZE,
    CT_NORTHRESIZE,
    CT_NORTHEASTRESIZE,
    CT_NORTHWESTRESIZE,
    CT_SOUTHRESIZE,
    CT_SOUTHEASTRESIZE,
    CT_SOUTHWESTRESIZE,
    CT_WESTRESIZE,
    CT_NORTHSOUTHRESIZE,
    CT_EASTWESTRESIZE,
    CT_NORTHEASTSOUTHWESTRESIZE,
    CT_NORTHWESTSOUTHEASTRESIZE,
    CT_COLUMNRESIZE,
    CT_ROWRESIZE,
    CT_MIDDLEPANNING,
    CT_EASTPANNING,
    CT_NORTHPANNING,
    CT_NORTHEASTPANNING,
    CT_NORTHWESTPANNING,
    CT_SOUTHPANNING,
    CT_SOUTHEASTPANNING,
    CT_SOUTHWESTPANNING,
    CT_WESTPANNING,
    CT_MOVE,
    CT_VERTICALTEXT,
    CT_CELL,
    CT_CONTEXTMENU,
    CT_ALIAS,
    CT_PROGRESS,
    CT_NODROP,
    CT_COPY,
    CT_NONE,
    CT_NOTALLOWED,
    CT_ZOOMIN,
    CT_ZOOMOUT,
    CT_GRAB,
    CT_GRABBING,
    CT_MIDDLE_PANNING_VERTICAL,
    CT_MIDDLE_PANNING_HORIZONTAL,
    CT_CUSTOM,
    CT_DND_NONE,
    CT_DND_MOVE,
    CT_DND_COPY,
    CT_DND_LINK,
    CT_NUM_VALUES,
} cef_cursor_type_t;

namespace Framework::GUI {
    // As gui/sdk.h has it.
    using EventCallback = fu2::function<void(const std::string &eventPayload) const>;

    class View final {
      public:
        // What the view was made with and told since.
        int id = 0;
        std::string url;
        int width            = 0;
        int height           = 0;
        int x                = 0;
        int y                = 0;
        int z                = 0;
        bool shouldDisplay   = false;
        bool hasFocus        = false;
        bool offscreen       = false;
        bool garbageCollected = false;
        bool autoResize      = false;
        // The URL the view is locked to the origin of; empty for a view nobody locked.
        std::string lockedTo;
        // Every URL it was told to load after its first, and every script it was told to run, in order.
        std::vector<std::string> loaded;
        std::vector<std::string> scripts;
        // The name of each function called on it, in order.
        std::vector<std::string> calls;
        // What listens to the events of its page, by name: one listener a name, as in the SDK of a view.
        std::map<std::string, EventCallback> listeners;
        OnViewEventCallback onViewEvent;

        // What the page does when it calls callEvent(name, payload): the listener of that name hears of it,
        // if there is one. As SDK::BroadcastEvent, a copy runs, so the listener may remove itself or destroy
        // the view.
        void Page(const std::string &eventName, const std::string &payload) const {
            const auto it = listeners.find(eventName);
            if (it == listeners.end()) {
                return;
            }
            const EventCallback listener = it->second;
            listener(payload);
        }

        // What the browser does when it has something to report.
        void Raise(const ViewEventData &data) const {
            if (onViewEvent) {
                const OnViewEventCallback callback = onViewEvent;
                callback(data);
            }
        }

        void Focus(bool enable) {
            calls.emplace_back("Focus");
            hasFocus = enable;
        }

        void Display(bool enable) {
            calls.emplace_back("Display");
            shouldDisplay = enable;
        }

        bool ShouldDisplay() const {
            return shouldDisplay;
        }

        void SetOffscreen(bool enable) {
            calls.emplace_back("SetOffscreen");
            offscreen = enable;
        }

        bool IsOffscreen() const {
            return offscreen;
        }

        void SetPosition(int newX, int newY) {
            calls.emplace_back("SetPosition");
            x = newX;
            y = newY;
        }

        void Resize(int newWidth, int newHeight) {
            calls.emplace_back("Resize");
            if (newWidth <= 0 || newHeight <= 0) {
                return;
            }
            width  = newWidth;
            height = newHeight;
        }

        // As the real view: the lock is the origin of the URL, here the URL up to its path, and "null" for
        // one that has none; a change of it is reported.
        void LockToOrigin(const std::string &lockUrl) {
            calls.emplace_back("LockToOrigin");
            const std::string before = OriginOf(lockedTo);
            lockedTo                 = lockUrl;
            if (OriginOf(lockUrl) == before) {
                return;
            }
            ViewEventData data;
            data.event  = ViewEvent::OriginChange;
            data.origin = OriginOf(lockUrl);
            data.url    = lockUrl;
            Raise(data);
        }

        // Locks again, as the real view does once it is locked.
        void LoadURL(const std::string &newUrl) {
            calls.emplace_back("LoadURL");
            if (!lockedTo.empty()) {
                LockToOrigin(newUrl);
            }
            loaded.push_back(newUrl);
        }

        void SetAutoResize(bool enable) {
            calls.emplace_back("SetAutoResize");
            autoResize = enable;
        }

        void SetZIndex(int newZ) {
            calls.emplace_back("SetZIndex");
            z = newZ;
        }

        void SetGarbageCollected(bool enable) {
            calls.emplace_back("SetGarbageCollected");
            garbageCollected = enable;
        }

        void AddEventListener(const std::string &eventName, const EventCallback &proc) {
            calls.emplace_back("AddEventListener");
            listeners[eventName] = proc;
        }

        void RemoveEventListener(const std::string &eventName) {
            calls.emplace_back("RemoveEventListener");
            listeners.erase(eventName);
        }

        void EvaluateScript(const std::string &script) {
            calls.emplace_back("EvaluateScript");
            scripts.push_back(script);
        }

        void SetOnViewEventCallback(OnViewEventCallback proc) {
            calls.emplace_back("SetOnViewEventCallback");
            onViewEvent = std::move(proc);
        }

        // The browser of a view is created before CreateView returns.
        bool IsCreated() const {
            return true;
        }

        static std::string OriginOf(const std::string &of) {
            if (of.empty()) {
                return {};
            }
            const size_t scheme = of.find("://");
            if (scheme == std::string::npos) {
                return "null";
            }
            return of.substr(0, of.find('/', scheme + 3));
        }
    };
} // namespace Framework::GUI
