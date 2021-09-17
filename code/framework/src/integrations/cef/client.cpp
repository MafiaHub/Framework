#include "client.h"

#include "render_handler.h"

namespace Framework::Integrations::CEF {
    Client::Client() {
        _renderHandler = new RenderHandler(this);
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

    cef_return_value_t Client::OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                                    CefRefPtr<CefRequest> request,
                                                    CefRefPtr<CefRequestCallback> callback) {
        // TODO: check for forbidden url and block them if required
        return {};
    }
} // namespace Framework::Integrations::CEF
