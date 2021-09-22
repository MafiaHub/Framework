#pragma once

#include <functional>
#include <include/cef_app.h>

namespace Framework::External::CEF {
    class Application
        : public CefApp
        , public CefRenderProcessHandler
        , public CefResourceBundleHandler
        , public CefV8Handler
        , public CefBrowserProcessHandler {
      protected:
        virtual void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar>) override;

        virtual bool GetDataResource(int, void *&, size_t &) override {
            return false;
        }
        virtual bool GetDataResourceForScale(int, ScaleFactor, void *&, size_t &) override {
            return false;
        }
        virtual bool GetLocalizedString(int, CefString &string) override {
            string = "";
            return true;
        }

        virtual CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
            return this;
        }

        virtual inline CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
            return this;
        }

        virtual void OnContextCreated(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefV8Context>) override;
        virtual void OnContextReleased(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefV8Context>) override;
        virtual void OnContextInitialized() override;
        virtual void OnBeforeCommandLineProcessing(const CefString &, CefRefPtr<CefCommandLine>) override;

        virtual bool OnProcessMessageReceived(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefProcessId, CefRefPtr<CefProcessMessage>) override;

        virtual bool Execute(const CefString &, CefRefPtr<CefV8Value>, const CefV8ValueList &, CefRefPtr<CefV8Value> &, CefString &) override;

        IMPLEMENT_REFCOUNTING(Application);
    };
    // DECLARE_INSTANCE_TYPE(Application);
} // namespace Framework::External::CEF