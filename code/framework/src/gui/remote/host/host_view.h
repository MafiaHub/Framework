/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include "gui/cef/client.h"
#include "gui/remote/channel.h"
#include "gui/remote/frame_buffer.h"
#include "gui/sdk.h"

#include "include/cef_browser.h"

#include <memory>
#include <string>
#include <vector>

namespace Framework::GUI::Remote {
    // The host half of a client's View: one windowless browser painting on the CPU into the view's
    // frame surface, and its events sent back to the client. Lives on the host's CEF UI thread.
    // What it does with the browser mirrors View, which is the in-process reference. Closing is
    // asynchronous and CEF's client keeps a raw pointer to the SDK until OnBeforeClose, so the Host
    // keeps a closed view alive until HasClosed.
    class HostView final {
      private:
        int32_t _id;
        Channel &_channel;
        const std::wstring &_token;

        CefRefPtr<CEF::RenderHandler> _renderHandler;
        CefRefPtr<CEF::LifeSpanHandler> _lifeSpanHandler;
        CefRefPtr<CEF::LoadHandler> _loadHandler;
        CefRefPtr<CEF::DisplayHandler> _displayHandler;
        CefRefPtr<CEF::Client> _client;
        std::unique_ptr<SDK> _sdk;
        CefRefPtr<CefBrowser> _browser;

        FrameWriter _frames;
        uint32_t _generation = 0;
        bool _surfaceFailed  = false;

        // Same retry as View::CheckNavigationStarted.
        static constexpr unsigned long long kNavigationStartTimeoutMs = 5000;
        static constexpr int kMaxNavigationReloads                    = 2;
        unsigned long long _navigationIssuedAt                        = 0;
        int _navigationReloads                                        = 0;
        bool _navigationStarted                                       = false;
        bool _navigationGaveUp                                        = false;

        void Emit(const ViewEventData &data);
        void Send(const std::vector<uint8_t> &message, const char *what);

      public:
        HostView(int32_t id, Channel &channel, const std::wstring &token);
        ~HostView();

        HostView(const HostView &)            = delete;
        HostView &operator=(const HostView &) = delete;

        bool Create(const std::string &url, int width, int height);

        // Unhooks every callback into this object and asks CEF to close the browser.
        void Close();
        bool HasClosed() const;

        void Resize(int width, int height);
        void LoadURL(const std::string &url);
        void LockToOrigin(const std::string &url);
        void Focus(bool focused);
        void SetAudioMuted(bool muted);
        void EvaluateScript(const std::string &script);
        void BeginFrame();

        void MouseMove(const Protocol::MouseMove &message);
        void MouseClick(const Protocol::MouseClick &message);
        void MouseWheel(const Protocol::MouseWheel &message);
        void Key(const Protocol::KeyEvent &message);
        void ImeSetComposition(const Protocol::ImeSetComposition &message);
        void ImeCommitText(const Protocol::ImeCommitText &message);
        void ImeFinishComposing(bool keepSelection);

        void CheckNavigationStarted();

        // After each pump: hands what OnPaint changed to the client.
        void PublishFrame();
    };
} // namespace Framework::GUI::Remote
