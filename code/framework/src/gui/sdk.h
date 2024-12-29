/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <queue>
#include <unordered_map>
#include <string>
#include <fu2/function2.hpp>

#include <Ultralight/Ultralight.h>
#include <JavaScriptCore/JavaScript.h>

namespace Framework::GUI {
    using EventCallbackProc = fu2::function<void(std::string eventPayload)>;

    class SDK {
      private:
        ultralight::View *_view;
        std::unordered_map<std::string, EventCallbackProc> _eventListeners;

        JSContextRef _context;
        JSObjectRef _globalObject;

      public:
        bool Init(ultralight::View *);
        bool Shutdown();

        inline void AddEventListener(std::string eventName, EventCallbackProc proc) {
            _eventListeners[eventName] = proc;
        }

        inline void RemoveEventListener(std::string eventName) {
            // Make sure the event exist
            if (_eventListeners.find(eventName) == _eventListeners.end()) {
                return;
            }

            // Remove the event
            _eventListeners.erase(eventName);
        }

        inline void BroadcastEvent(std::string eventName, std::string eventPayload) {
            // Make sure the event exist
            if (_eventListeners.find(eventName) == _eventListeners.end()) {
                return;
            }

            // Process all the event callback
            _eventListeners[eventName](eventPayload);
        }

      private:
        void InitEventHandlers();
    };
}
