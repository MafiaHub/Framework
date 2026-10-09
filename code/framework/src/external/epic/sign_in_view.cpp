/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "sign_in_view.h"

#include "auth.h"

#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_request_handler.h"
#include "include/cef_string_visitor.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

#include <string>
#include <utility>

namespace Framework::External::Epic {
    namespace {
        constexpr int kWindowWidth  = 520;
        constexpr int kWindowHeight = 760;

        // The redirect page whose body carries the single-use authorizationCode.
        constexpr const char *kRedirectMarker = "/id/api/redirect";

        class SignInClient;

        // Reads the redirect page's decoded text on the UI thread, then completes the sign-in.
        class PageTextVisitor final: public CefStringVisitor {
          public:
            PageTextVisitor(CefRefPtr<SignInClient> client, CefRefPtr<CefBrowser> browser): _client(client), _browser(browser) {}
            void Visit(const CefString &text) override;

          private:
            CefRefPtr<SignInClient> _client;
            CefRefPtr<CefBrowser> _browser;
            IMPLEMENT_REFCOUNTING(PageTextVisitor);
        };

        class SignInClient final
            : public CefClient
            , public CefLifeSpanHandler
            , public CefLoadHandler
            , public CefRequestHandler {
          public:
            explicit SignInClient(std::function<void(bool)> onDone): _onDone(std::move(onDone)) {}

            CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
                return this;
            }
            CefRefPtr<CefLoadHandler> GetLoadHandler() override {
                return this;
            }
            CefRefPtr<CefRequestHandler> GetRequestHandler() override {
                return this;
            }

            // The redirect page shows the authorization code, so hide the window as navigation to it
            // starts, before it can paint. The page still has to load for the code to be read.
            bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request, bool, bool) override {
                if (request->GetURL().ToString().find(kRedirectMarker) != std::string::npos) {
                    if (CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser)) {
                        if (CefRefPtr<CefWindow> window = view->GetWindow()) {
                            window->Hide();
                        }
                    }
                }
                return false;
            }

            void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int) override {
                if (_handled || !frame->IsMain()) {
                    return;
                }
                if (frame->GetURL().ToString().find(kRedirectMarker) == std::string::npos) {
                    return;
                }
                _handled = true;
                frame->GetText(new PageTextVisitor(this, browser));
            }

            // The grant blocks on the network, so it runs off the UI thread; the window closes back on it.
            void Complete(CefRefPtr<CefBrowser> browser, const std::string &pageText) {
                CefPostTask(TID_FILE_USER_BLOCKING, base::BindOnce(&SignInClient::SignIn, this, browser, pageText));
            }

            void SignIn(CefRefPtr<CefBrowser> browser, const std::string &pageText) {
                const bool signedIn = SignInWithAuthorizationCode(pageText);
                CefPostTask(TID_UI, base::BindOnce(&SignInClient::Finish, this, browser, signedIn));
            }

            void Finish(CefRefPtr<CefBrowser> browser, bool signedIn) {
                _result = signedIn;
                if (CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser)) {
                    if (CefRefPtr<CefWindow> window = view->GetWindow()) {
                        window->Close();
                    }
                }
            }

            void OnBeforeClose(CefRefPtr<CefBrowser>) override {
                if (_onDone) {
                    _onDone(_result);
                    _onDone = nullptr;
                }
            }

          private:
            std::function<void(bool)> _onDone;
            bool _handled = false;
            bool _result  = false;
            IMPLEMENT_REFCOUNTING(SignInClient);
        };

        void PageTextVisitor::Visit(const CefString &text) {
            _client->Complete(_browser, text.ToString());
        }

        class SignInWindowDelegate final: public CefWindowDelegate {
          public:
            explicit SignInWindowDelegate(CefRefPtr<CefBrowserView> view): _view(view) {}

            void OnWindowCreated(CefRefPtr<CefWindow> window) override {
                window->AddChildView(_view);
                window->CenterWindow(CefSize(kWindowWidth, kWindowHeight));
                window->SetTitle("Sign in to Epic Games");
                window->Show();
                _view->RequestFocus();
            }
            void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
                _view = nullptr;
            }
            CefSize GetPreferredSize(CefRefPtr<CefView>) override {
                return CefSize(kWindowWidth, kWindowHeight);
            }
            bool CanResize(CefRefPtr<CefWindow>) override {
                return true;
            }

          private:
            CefRefPtr<CefBrowserView> _view;
            IMPLEMENT_REFCOUNTING(SignInWindowDelegate);
        };
    } // namespace

    void ShowSignInWindow(std::function<void(bool)> onDone) {
        CefRefPtr<SignInClient> client(new SignInClient(std::move(onDone)));
        CefBrowserSettings settings;
        const CefString url            = GetLoginUrl();
        CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(client, url, settings, nullptr, nullptr, nullptr);
        CefWindow::CreateTopLevelWindow(new SignInWindowDelegate(view));
    }
} // namespace Framework::External::Epic
