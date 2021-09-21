#pragma once

#include <include/cef_client.h>
#include <include/cef_media_router.h>

namespace Framework::Integrations::CEF {
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

      public:
        Client();

        CefBrowser *GetBrowser() {
            return _browser.get();
        }

      protected:
        virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
            return this;
        };
        virtual CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
            return this;
        };
        virtual CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
            return this;
        };
        virtual CefRefPtr<CefLoadHandler> GetLoadHandler() override {
            return this;
        };
        virtual CefRefPtr<CefRenderHandler> GetRenderHandler() override {
            return _renderHandler;
        };
        virtual CefRefPtr<CefRequestHandler> GetRequestHandler() override {
            return this;
        };
        virtual CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, bool, bool, const CefString &,
                                                                               bool &) override {
            return this;
        }

        virtual void OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, TransitionType) override;
        virtual void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int) override;
        virtual void OnAfterCreated(CefRefPtr<CefBrowser>) override;
        virtual void OnBeforeClose(CefRefPtr<CefBrowser>) override;
        virtual cef_return_value_t OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, CefRefPtr<CefRequestCallback>) override;
        virtual void OnRenderProcessTerminated(CefRefPtr<CefBrowser>, TerminationStatus) override;

        IMPLEMENT_REFCOUNTING(Client);
    };
} // namespace Framework::Integrations::CEF