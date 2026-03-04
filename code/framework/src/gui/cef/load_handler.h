/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <fu2/function2.hpp>
#include <string>

#include "include/cef_load_handler.h"

namespace Framework::GUI::CEF {
    using OnDOMReadyCallback          = fu2::function<void(const std::string &, bool, const std::string &)>;
    using OnWindowObjectReadyCallback = fu2::function<void(const std::string &, bool, const std::string &)>;

    class LoadHandler final: public CefLoadHandler {
      private:
        OnDOMReadyCallback _onDOMReadyCallback;
        OnWindowObjectReadyCallback _onWindowObjectReadyCallback;

      public:
        void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) override;
        void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transitionType) override;

        void SetOnDOMReadyCallback(OnDOMReadyCallback cb) {
            _onDOMReadyCallback = std::move(cb);
        }

        void SetOnWindowObjectReadyCallback(OnWindowObjectReadyCallback cb) {
            _onWindowObjectReadyCallback = std::move(cb);
        }

        IMPLEMENT_REFCOUNTING(LoadHandler);
    };
} // namespace Framework::GUI::CEF
