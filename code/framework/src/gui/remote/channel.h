/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include "protocol.h"

#include <function2/function2.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Framework::GUI::Remote {
    // One duplex, overlapped named pipe carrying Protocol messages. The client listens on a name
    // only it and the host it started know, on a pipe only its own user may open; the host
    // connects. Received messages are handed to the handler on the channel's own reader thread.
    // Send may be called from any thread. Each end is built with the largest payload it accepts
    // from the other and the largest the other accepts from it.
    class Channel final {
      public:
        using MessageHandler = fu2::function<void(Protocol::MessageType type, std::vector<uint8_t> payload)>;
        using ClosedHandler  = fu2::function<void()>;

        enum class ConnectState {
            Pending,
            Connected,
            Failed,
        };

      private:
        HANDLE _pipe         = INVALID_HANDLE_VALUE;
        HANDLE _stopEvent    = nullptr;
        HANDLE _writeEvent   = nullptr;
        HANDLE _connectEvent = nullptr;
        OVERLAPPED _connectOverlapped {};
        bool _connectPending = false;

        std::mutex _writeMutex;
        std::thread _reader;
        std::atomic<bool> _open = false;

        const uint32_t _maxReceiveBytes;
        const uint32_t _maxSendBytes;

        // Bounds a write the peer does not drain; a stuck peer fails the channel instead of the caller.
        static constexpr DWORD kWriteTimeoutMs = 3000;

        bool CreateEvents();
        bool ReadExact(void *buffer, std::size_t size, HANDLE readEvent);
        void ReadLoop(MessageHandler onMessage, ClosedHandler onClosed);

      public:
        Channel(uint32_t maxReceiveBytes, uint32_t maxSendBytes): _maxReceiveBytes(maxReceiveBytes), _maxSendBytes(maxSendBytes) {}
        ~Channel();

        Channel(const Channel &)            = delete;
        Channel &operator=(const Channel &) = delete;

        // Client process: create the pipe and start waiting for the host.
        bool Listen(const std::wstring &name);
        ConnectState PollConnect();

        // Process id at the other end of a listening pipe, 0 when unknown.
        DWORD GetPeerProcessId() const;

        // Host process: connect to the client's pipe.
        bool Connect(const std::wstring &name, DWORD timeoutMs);

        // Starts the reader thread. onClosed runs on it once, when the pipe breaks, a message cannot
        // be taken (too large, or no memory for it) or Close is called. Neither handler may Close.
        void Start(MessageHandler onMessage, ClosedHandler onClosed);

        // False once the channel is broken; a failed write breaks it. A message over the peer's limit
        // is refused without breaking it.
        bool Send(const std::vector<uint8_t> &message);

        template <class T>
        bool Send(const T &message) {
            return Send(Protocol::Encode(message));
        }

        bool IsOpen() const {
            return _open.load();
        }

        void Close();
    };
} // namespace Framework::GUI::Remote
