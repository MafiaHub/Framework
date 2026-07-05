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
        if (message->GetName() == "CallEvent" && _sdk) {
            // drop events from frames off the locked origin
            if (_lifeSpanHandler && !_lifeSpanHandler->GetAllowedOrigin().empty()) {
                if (!frame || LifeSpanHandler::OriginFromURL(frame->GetURL()) != _lifeSpanHandler->GetAllowedOrigin()) {
                    return true;
                }
            }
            auto args              = message->GetArgumentList();
            std::string eventName  = args->GetString(0).ToString();
            std::string payload    = args->GetString(1).ToString();
            _sdk->BroadcastEvent(eventName, payload);
            return true;
        }
        return false;
    }
} // namespace Framework::GUI::CEF
