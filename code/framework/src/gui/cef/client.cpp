/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "client.h"
#include "gui/sdk.h"

namespace Framework::GUI::CEF {
    Client::Client(CefRefPtr<RenderHandler> renderHandler, CefRefPtr<LifeSpanHandler> lifeSpanHandler, CefRefPtr<LoadHandler> loadHandler, CefRefPtr<DisplayHandler> displayHandler, SDK *sdk)
        : _renderHandler(renderHandler)
        , _lifeSpanHandler(lifeSpanHandler)
        , _loadHandler(loadHandler)
        , _displayHandler(displayHandler)
        , _sdk(sdk) {
    }

    bool Client::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId sourceProcess, CefRefPtr<CefProcessMessage> message) {
        const std::string name = message->GetName().ToString();

        if (name == "CallEvent" && _sdk) {
            // drop events from frames off the locked origin
            if (_lifeSpanHandler && !_lifeSpanHandler->GetAllowedOrigin().empty()) {
                if (!frame || LifeSpanHandler::OriginFromURL(frame->GetURL()) != _lifeSpanHandler->GetAllowedOrigin()) {
                    if (_onViewEvent) {
                        ViewEventData data;
                        data.event  = ViewEvent::ResourceBlocked;
                        data.url    = frame ? frame->GetURL().ToString() : std::string();
                        data.domain = frame ? LifeSpanHandler::HostFromURL(frame->GetURL()) : std::string();
                        data.reason = ViewBlockReason::ForeignEvent;
                        _onViewEvent(data);
                    }
                    return true;
                }
            }
            auto args             = message->GetArgumentList();
            std::string eventName = args->GetString(0).ToString();
            std::string payload   = args->GetString(1).ToString();
            _sdk->BroadcastEvent(eventName, payload);
            return true;
        }

        // Sent by our renderer process, not reachable from page script.
        if (name == "InputFocus") {
            auto args = message->GetArgumentList();
            if (args->GetSize() < 1 || args->GetType(0) != VTYPE_BOOL) {
                return true;
            }
            if (_onViewEvent) {
                ViewEventData data;
                data.event   = ViewEvent::InputFocusChange;
                data.focused = args->GetBool(0);
                _onViewEvent(data);
            }
            return true;
        }

        return false;
    }
} // namespace Framework::GUI::CEF
