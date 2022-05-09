/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <glm/vec2.hpp>
#include <include/cef_client.h>
#include <include/cef_media_router.h>

namespace Framework::External::CEF {
    struct ClientInformation {
        uint32_t sizeX = 0;
        uint32_t sizeY = 0;
        glm::vec2 position {0};
    };

    class Client
        : public CefClient
        , public CefLifeSpanHandler
        , public CefDisplayHandler
        , public CefContextMenuHandler
        , public CefLoadHandler
        , public CefRequestHandler
        , public CefResourceRequestHandler {
      private:
        CefRefPtr<CefRenderHandler> _renderHandler;
        CefRefPtr<CefBrowser> _browser;

        ClientInformation _information;
        bool _visible;

      public:
        explicit Client(const ClientInformation &info);
        ~Client() override;

        bool Initialize(const std::string &);

        CefBrowser *GetBrowser() {
            return _browser.get();
        }

        const ClientInformation &GetClientInformation() const {
            return _information;
        }

        bool IsVisible() const {
            return _visible;
        }

        void SetVisible(bool visible) {
            _visible = visible;
        }

      protected:
        CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
            return this;
        };
        CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
            return this;
        };
        CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
            return this;
        };
        CefRefPtr<CefLoadHandler> GetLoadHandler() override {
            return this;
        };
        CefRefPtr<CefRenderHandler> GetRenderHandler() override {
            return _renderHandler;
        };
        CefRefPtr<CefRequestHandler> GetRequestHandler() override {
            return this;
        };
        CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
            CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, bool, bool, const CefString &, bool &) override {
            return this;
        }

        void OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, TransitionType) override;
        void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int) override;
        void OnAfterCreated(CefRefPtr<CefBrowser>) override;
        void OnBeforeClose(CefRefPtr<CefBrowser>) override;
        cef_return_value_t OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, CefRefPtr<CefRequestCallback>) override;
        void OnRenderProcessTerminated(CefRefPtr<CefBrowser>, TerminationStatus) override;

        void OnMouseMove(const glm::ivec2 &);
        void OnMouseClick(bool, bool, int32_t);

        void OnEndScene();
        void OnReset();

        IMPLEMENT_REFCOUNTING(Client);
    };
} // namespace Framework::External::CEF
