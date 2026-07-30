/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include "gui/view_events.h"

#include "include/cef_client.h"

#include "display_handler.h"
#include "life_span_handler.h"
#include "load_handler.h"
#include "render_handler.h"

namespace Framework::GUI {
    class SDK;
} // namespace Framework::GUI

namespace Framework::GUI::CEF {
    class Client final: public CefClient {
      private:
        CefRefPtr<RenderHandler> _renderHandler;
        CefRefPtr<LifeSpanHandler> _lifeSpanHandler;
        CefRefPtr<LoadHandler> _loadHandler;
        CefRefPtr<DisplayHandler> _displayHandler;
        SDK *_sdk = nullptr;
        OnViewEventCallback _onViewEvent;

      public:
        Client(CefRefPtr<RenderHandler> renderHandler, CefRefPtr<LifeSpanHandler> lifeSpanHandler, CefRefPtr<LoadHandler> loadHandler, CefRefPtr<DisplayHandler> displayHandler, SDK *sdk);

        CefRefPtr<CefRenderHandler> GetRenderHandler() override {
            return _renderHandler;
        }

        CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
            return _lifeSpanHandler;
        }

        CefRefPtr<CefLoadHandler> GetLoadHandler() override {
            return _loadHandler;
        }

        CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
            return _displayHandler;
        }

        CefRefPtr<CefRequestHandler> GetRequestHandler() override {
            return _lifeSpanHandler;
        }

        bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId sourceProcess, CefRefPtr<CefProcessMessage> message) override;

        void SetViewEventCallback(OnViewEventCallback cb) {
            _onViewEvent = std::move(cb);
        }

        IMPLEMENT_REFCOUNTING(Client);
    };
} // namespace Framework::GUI::CEF
