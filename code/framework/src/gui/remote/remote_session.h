/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include <utils/error.h>
#include <utils/result.h>

#include "channel.h"
#include "protocol.h"
#include "resource_server.h"

#include <function2/function2.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace Framework::GUI::Resources {
    class ResourceRegistry;
} // namespace Framework::GUI::Resources

namespace Framework::GUI::Remote {
    using HostMessage = std::variant<Protocol::ViewEventMessage, Protocol::PageEvent, Protocol::FrameSurface>;

    // The client's end of the remote CEF host: starts cef64/cef_host.exe with a fresh random name
    // for its pipe and frame sections, watches it, and restarts it when it dies. Answers the
    // host's fw:// requests from the Manager's resource registry through a ResourceServer. Owned
    // by the Manager; Start, Stop and Poll run on the thread that updates it.
    class RemoteSession final {
      public:
        struct Listener {
            fu2::function<void(const HostMessage &message)> onMessage;
            // The host died: its views are gone and frames stop until it is back.
            fu2::function<void()> onHostLost;
            // A host is up: every view not yet created on it must be now.
            fu2::function<void()> onHostReady;
        };

      private:
        enum class State {
            Stopped,
            Connecting,
            Handshaking,
            Ready,
            Restarting,
            Failed,
        };

        Resources::ResourceRegistry *_registry;
        std::string _rootDir;
        std::filesystem::path _hostPath;

        std::atomic<State> _state = State::Stopped;
        // Start has returned it to the caller: from then on a failed host is restarted.
        bool _started        = false;
        bool _helloSeen      = false;
        ULONGLONG _deadline  = 0;
        ULONGLONG _restartAt = 0;
        ULONGLONG _readyAt   = 0;
        int _restarts        = 0;
        std::string _failure;

        std::wstring _token;
        Channel _channel {Protocol::kMaxHostMessageBytes, Protocol::kMaxClientMessageBytes};
        ResourceServer _server;
        HANDLE _process  = nullptr;
        DWORD _processId = 0;
        HANDLE _job      = nullptr;

        // Filled on the channel thread, drained by Poll.
        std::mutex _inboxMutex;
        std::deque<std::pair<Protocol::MessageType, std::vector<uint8_t>>> _inbox;
        bool _inboxOverflow              = false;
        std::atomic<bool> _channelClosed = false;

        static constexpr int kMaxRestarts = 5;
        // A host that ran this long without failing has its restart budget back.
        static constexpr ULONGLONG kStableRunMs = 5 * 60 * 1000;
        // Start blocks the game's thread this long at most, for a host that dies at once; one still
        // starting is left to Poll.
        static constexpr ULONGLONG kStartWaitMs        = 3000;
        static constexpr ULONGLONG kConnectTimeoutMs   = 15000;
        static constexpr ULONGLONG kHandshakeTimeoutMs = 30000;
        static constexpr std::size_t kMaxInboxMessages = 16384;

        bool Launch();
        void Teardown(bool kill);
        void Fail(const std::string &reason, Listener *listener);
        void Advance(Listener *listener);
        void HandleMessage(Protocol::MessageType type, const std::vector<uint8_t> &payload, Listener *listener);
        void OnChannelMessage(Protocol::MessageType type, std::vector<uint8_t> payload);
        void SendRoots();

      public:
        explicit RemoteSession(Resources::ResourceRegistry *registry);
        ~RemoteSession();

        RemoteSession(const RemoteSession &)            = delete;
        RemoteSession &operator=(const RemoteSession &) = delete;

        // Starts the host and returns once it has connected, or failed to, or kStartWaitMs passed.
        // CEF comes up after that: views created before it are created on the host by Poll's
        // onHostReady.
        [[nodiscard]] Utils::Result<void, Error> Start(const std::string &rootDir, const std::filesystem::path &hostPath);

        // Asks the host to close its browsers and exit, and kills it if it does not.
        void Stop();

        // Receives what the host sent, notices it dying and drives its restart.
        void Poll(Listener &listener);

        bool IsReady() const {
            return _state.load() == State::Ready;
        }

        // Names this host's frame sections; changes with every restart.
        const std::wstring &GetToken() const {
            return _token;
        }

        template <class T>
        bool Send(const T &message) {
            return IsReady() && _channel.Send(message);
        }

        void RegisterRoot(const std::string &host);
        void UnregisterRoot(const std::string &host);
    };
} // namespace Framework::GUI::Remote
