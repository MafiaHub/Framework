/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "client.h"

#include "render_handler.h"

namespace Framework::External::CEF {
    Client::Client(const ClientInformation &info): _information(info), _visible(false) {
    }

    Client::~Client() {
        if (_browser) {
            _browser->GetHost()->CloseBrowser(true);
        }
    }

    bool Client::Initialize(const std::string &destinationUrl){
        // Create the rendering instance 
        _renderHandler = new RenderHandler(this);

        // Prepare the window itself
        CefWindowInfo window;
        window.SetAsWindowless(NULL);

        // Prepare the rendering settings
        // Most of them are security settings enhancements
        CefBrowserSettings settings;
        settings.javascript_close_windows = STATE_DISABLED;
        settings.javascript_access_clipboard = STATE_DISABLED;
        settings.plugins = STATE_DISABLED;
        settings.windowless_frame_rate = 30;
        settings.webgl = STATE_ENABLED;
        settings.file_access_from_file_urls = STATE_DISABLED; // To be enabled when we have proper resource handling
        settings.local_storage = STATE_ENABLED;
        settings.application_cache = STATE_ENABLED;

        // Initialize the browser
        return CefBrowserHost::CreateBrowser(window, this, destinationUrl, settings, {}, CefRequestContext::GetGlobalContext());
    }

    void Client::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transitionType) {}

    void Client::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {}

    void Client::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
        _browser = browser;
    }

    void Client::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
        _browser = nullptr;
    }

    void Client::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus status) {}

    cef_return_value_t Client::OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
        CefRefPtr<CefRequestCallback> callback) {
        // TODO: check for forbidden url and block them if required
        return {};
    }

    void Client::OnMouseMove(const glm::ivec2 &pos) {
        if (!_browser || !IsVisible()) {
            return;
        }

        CefMouseEvent ev;
        ev.x = pos.x;
        ev.y = pos.y;
        _browser->GetHost()->SendMouseMoveEvent(ev, false);
    }

    void Client::OnMouseClick(bool down, bool left, int32_t modifiers) {
        if (!_browser || !IsVisible()) {
            return;
        }

        CefMouseEvent ev;
        ev.modifiers = modifiers;

        _browser->GetHost()->SendMouseClickEvent(ev, left ? cef_mouse_button_type_t::MBT_LEFT : cef_mouse_button_type_t::MBT_RIGHT, !down, 1);
    }

    void Client::OnEndScene(){
        if(!_renderHandler || !_browser || !IsVisible()){
            return;
        }

        //TODO: call rendering method
    }

    void Client::OnReset(){
        if(!_renderHandler){
            return;
        }

        //TODO: call rendering reset method
    }
} // namespace Framework::External::CEF
