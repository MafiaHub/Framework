#include "application.h"

namespace Framework::External::CEF {
    void Application::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
        // TODO: make this customizable
        registrar->AddCustomScheme("framework", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE | CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_FETCH_ENABLED);
    }

    void Application::OnContextInitialized() {}

    void Application::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {}

    void Application::OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {}

    void Application::OnBeforeCommandLineProcessing(const CefString &processType, CefRefPtr<CefCommandLine> commandLine) {
        commandLine->AppendSwitch("enable-experimental-web-platform-features");
        commandLine->AppendSwitch("transparent-painting-enabled");
        commandLine->AppendSwitch("off-screen-rendering-enabled");
        commandLine->AppendSwitch("disable-gpu-compositing");
        commandLine->AppendSwitch("enable-begin-frame-scheduling");
        commandLine->AppendSwitch("ignore-gpu-blacklist");
        commandLine->AppendSwitch("ignore-gpu-blocklist");
    }

    bool Application::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId sourceProcess, CefRefPtr<CefProcessMessage> message) {
        return true;
    }

    bool Application::Execute(const CefString &name, CefRefPtr<CefV8Value> object, const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception) {
        return true;
    }
} // namespace Framework::External::CEF