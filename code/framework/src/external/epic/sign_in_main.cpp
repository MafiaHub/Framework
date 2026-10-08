/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "sign_in_view.h"

#include "logging/logger.h"

#include "include/cef_app.h"

#include <windows.h>

#include <filesystem>
#include <string>

// epic_sign_in.exe: the launcher runs it when no stored Epic sign-in refreshes. Exits 0 once a refresh
// token is persisted, 1 on cancel or failure. Its own process so launchers never host CEF.
namespace {
    class SignInApp final
        : public CefApp
        , public CefBrowserProcessHandler {
      public:
        CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
            return this;
        }

        void OnBeforeCommandLineProcessing(const CefString &, CefRefPtr<CefCommandLine> commandLine) override {
            commandLine->AppendSwitch("disable-spell-checking");
            commandLine->AppendSwitch("disable-pdf-extension");
            commandLine->AppendSwitch("disable-component-update");
        }

        void OnContextInitialized() override {
            Framework::External::Epic::ShowSignInWindow([this](bool signedIn) {
                _signedIn = signedIn;
                CefQuitMessageLoop();
            });
        }

        bool SignedIn() const {
            return _signedIn;
        }

      private:
        bool _signedIn = false;
        IMPLEMENT_REFCOUNTING(SignInApp);
    };

    // A private profile keeps the Epic session cookie (signing in again stays one click) out of the
    // shared %LOCALAPPDATA%\CEF\User Data, which other CEF apps also read.
    std::wstring ProfileDirectory() {
        wchar_t localAppData[MAX_PATH] = {};
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) {
            return {};
        }
        const auto dir = std::filesystem::path(localAppData) / L"MafiaHub" / L"epic_auth_cef";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return ec ? std::wstring {} : dir.wstring();
    }
} // namespace

int main() {
    CefMainArgs mainArgs(::GetModuleHandleW(nullptr));
    CefRefPtr<SignInApp> app(new SignInApp());

    // CEF re-launches this exe for its renderer and GPU subprocesses; those return here.
    const int subprocessExit = CefExecuteProcess(mainArgs, app, nullptr);
    if (subprocessExit >= 0) {
        return subprocessExit;
    }

    // The waiting launcher still writes its own log in the same folder.
    Framework::Logging::GetInstance()->SetLogName("epic_sign_in");

    CefSettings settings;
    settings.no_sandbox                  = true;
    settings.multi_threaded_message_loop = false;
    settings.log_severity                = LOGSEVERITY_WARNING;
    if (const std::wstring profile = ProfileDirectory(); !profile.empty()) {
        CefString(&settings.root_cache_path) = profile;
        CefString(&settings.cache_path)      = profile;
    }

    if (!CefInitialize(mainArgs, settings, app, nullptr)) {
        return 1;
    }
    CefRunMessageLoop();
    CefShutdown();
    return app->SignedIn() ? 0 : 1;
}
