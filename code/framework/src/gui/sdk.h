/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/error.h>
#include <utils/result.h>

#include <string>
#include <unordered_map>

#include <fu2/function2.hpp>

#include "include/cef_browser.h"

namespace Framework::GUI {
    using EventCallback = fu2::function<void(const std::string &eventPayload) const>;

    class SDK final {
      private:
        CefRefPtr<CefBrowser> _browser;
        std::unordered_map<std::string, EventCallback> _eventListeners;

      public:
        [[nodiscard]] Utils::Result<void, Framework::Error> Init(CefRefPtr<CefBrowser> browser);
        void Shutdown();

        // Escape hatch: the raw CEF browser. Prefer the event API (AddEventListener/BroadcastEvent).
        CefRefPtr<CefBrowser> GetBrowser() const {
            return _browser;
        }

        inline void AddEventListener(const std::string &eventName, EventCallback proc) {
            _eventListeners[eventName] = proc;
        }

        inline void RemoveEventListener(const std::string &eventName) {
            if (!_eventListeners.contains(eventName)) {
                return;
            }
            _eventListeners.erase(eventName);
        }

        inline void BroadcastEvent(const std::string &eventName, const std::string &eventPayload) {
            if (!_eventListeners.contains(eventName)) {
                return;
            }
            _eventListeners[eventName](eventPayload);
        }
    };
} // namespace Framework::GUI
