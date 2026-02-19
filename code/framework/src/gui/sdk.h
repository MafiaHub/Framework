/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>
#include <unordered_map>

#include <fu2/function2.hpp>

#include "include/cef_browser.h"

namespace Framework::GUI {
    using EventCallbackProc = fu2::function<void(std::string eventPayload)>;

    class SDK {
      private:
        CefRefPtr<CefBrowser> _browser;
        std::unordered_map<std::string, EventCallbackProc> _eventListeners;

      public:
        bool Init(CefRefPtr<CefBrowser> browser);
        bool Shutdown();

        CefRefPtr<CefBrowser> GetBrowser() const {
            return _browser;
        }

        inline void AddEventListener(std::string eventName, EventCallbackProc proc) {
            _eventListeners[eventName] = proc;
        }

        inline void RemoveEventListener(std::string eventName) {
            if (!_eventListeners.contains(eventName)) {
                return;
            }
            _eventListeners.erase(eventName);
        }

        inline void BroadcastEvent(std::string eventName, std::string eventPayload) {
            if (!_eventListeners.contains(eventName)) {
                return;
            }
            _eventListeners[eventName](eventPayload);
        }
    };
} // namespace Framework::GUI
