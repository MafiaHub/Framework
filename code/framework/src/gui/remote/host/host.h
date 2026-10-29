/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include "gui/cef/app.h"
#include "gui/remote/channel.h"
#include "gui/resources/resource_registry.h"

#include "host_view.h"
#include "remote_provider.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Framework::GUI::Remote {
    // The CEF host process: the browser process, moved out of a 32-bit game whose address space
    // cannot spare Chromium. Owns CEF, every view's browser and the fw:// scheme, and serves
    // exactly one client over the channel; it exits when that client goes. Its main thread is the
    // CEF UI thread and sleeps until CEF asks to be pumped (external_message_pump) or the client
    // sends something; the client's begin-frame messages set the paint cadence.
    class Host final {
      private:
        Channel _channel {Protocol::kMaxClientMessageBytes, Protocol::kMaxHostMessageBytes};
        std::wstring _token;
        std::string _rootDir;
        std::filesystem::path _moduleDir;

        CefRefPtr<CEF::App> _app;
        CefRefPtr<Resources::ResourceRegistry> _registry;
        std::shared_ptr<ResourceBroker> _broker;
        HANDLE _profileLock = INVALID_HANDLE_VALUE;

        std::map<int32_t, std::unique_ptr<HostView>> _views;
        // Destroyed by the client, kept until CEF has let go of them.
        std::vector<std::unique_ptr<HostView>> _closing;

        // Filled on the channel thread, drained on the CEF UI thread.
        std::mutex _inboxMutex;
        std::deque<std::pair<Protocol::MessageType, std::vector<uint8_t>>> _inbox;
        // Set by the channel thread and by CEF asking for its pump, from any thread.
        HANDLE _wake = nullptr;

        // GetTickCount64 at which CEF wants CefDoMessageLoopWork, or kNoPump.
        static constexpr ULONGLONG kNoPump = ~0ull;
        std::atomic<ULONGLONG> _pumpDueAt  = kNoPump;

        std::atomic<bool> _closed = false;
        bool _shutdownRequested   = false;
        bool _pumpFailed          = false;

        void OnMessage(Protocol::MessageType type, std::vector<uint8_t> payload);
        void Dispatch(Protocol::MessageType type, const std::vector<uint8_t> &payload);
        HostView *FindView(int32_t viewId);
        void CloseView(std::unique_ptr<HostView> view);
        void SchedulePump(int64_t delayMs);
        DWORD WaitTimeout() const;

        template <class T>
        bool Read(const std::vector<uint8_t> &payload, T &message);

      public:
        Host(std::wstring token, std::string rootDir, std::filesystem::path moduleDir);
        ~Host();

        // Process exit code.
        int Run(const std::wstring &pipeName);
    };
} // namespace Framework::GUI::Remote
