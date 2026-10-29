/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "channel.h"

#include <algorithm>
#include <memory>
#include <new>
#include <utility>

namespace Framework::GUI::Remote {
    namespace {
        constexpr DWORD kPipeBufferBytes = 1u << 20;

        // A DACL with one entry: full access for the user this process runs as. The default one
        // also gives SYSTEM and administrators full control and Everyone read access.
        class OwnerOnlySecurity final {
          private:
            std::unique_ptr<uint8_t[]> _user;
            std::unique_ptr<uint8_t[]> _acl;
            SECURITY_DESCRIPTOR _descriptor {};

          public:
            SECURITY_ATTRIBUTES attributes {sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};

            bool Build() {
                HANDLE token = nullptr;
                if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
                    return false;
                }
                DWORD size = 0;
                GetTokenInformation(token, TokenUser, nullptr, 0, &size);
                _user          = std::make_unique<uint8_t[]>(size);
                const bool got = size > 0 && GetTokenInformation(token, TokenUser, _user.get(), size, &size);
                CloseHandle(token);
                if (!got) {
                    return false;
                }

                const PSID sid       = reinterpret_cast<TOKEN_USER *>(_user.get())->User.Sid;
                const DWORD aclBytes = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + GetLengthSid(sid);
                _acl                 = std::make_unique<uint8_t[]>(aclBytes);
                auto *acl            = reinterpret_cast<ACL *>(_acl.get());
                if (!InitializeAcl(acl, aclBytes, ACL_REVISION) || !AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS, sid) || !InitializeSecurityDescriptor(&_descriptor, SECURITY_DESCRIPTOR_REVISION) || !SetSecurityDescriptorDacl(&_descriptor, TRUE, acl, FALSE)) {
                    return false;
                }
                attributes.lpSecurityDescriptor = &_descriptor;
                return true;
            }
        };
    } // namespace

    Channel::~Channel() {
        Close();
    }

    bool Channel::CreateEvents() {
        _stopEvent    = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        _writeEvent   = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        _connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        return _stopEvent && _writeEvent && _connectEvent;
    }

    bool Channel::Listen(const std::wstring &name) {
        Close();
        if (!CreateEvents()) {
            Close();
            return false;
        }

        OwnerOnlySecurity security;
        if (!security.Build()) {
            Close();
            return false;
        }

        // One instance, and the first: a pipe someone else already holds under this name fails here.
        _pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, kPipeBufferBytes, kPipeBufferBytes, 0, &security.attributes);
        if (_pipe == INVALID_HANDLE_VALUE) {
            Close();
            return false;
        }

        _connectOverlapped        = {};
        _connectOverlapped.hEvent = _connectEvent;
        if (ConnectNamedPipe(_pipe, &_connectOverlapped)) {
            SetEvent(_connectEvent);
        }
        else {
            const DWORD error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED) {
                SetEvent(_connectEvent);
            }
            else if (error != ERROR_IO_PENDING) {
                Close();
                return false;
            }
        }
        _connectPending = true;
        return true;
    }

    Channel::ConnectState Channel::PollConnect() {
        if (_pipe == INVALID_HANDLE_VALUE) {
            return ConnectState::Failed;
        }
        if (!_connectPending) {
            return ConnectState::Connected;
        }
        if (WaitForSingleObject(_connectEvent, 0) != WAIT_OBJECT_0) {
            return ConnectState::Pending;
        }
        DWORD transferred = 0;
        if (!GetOverlappedResult(_pipe, &_connectOverlapped, &transferred, FALSE) && GetLastError() != ERROR_PIPE_CONNECTED) {
            return ConnectState::Failed;
        }
        _connectPending = false;
        return ConnectState::Connected;
    }

    DWORD Channel::GetPeerProcessId() const {
        ULONG processId = 0;
        if (_pipe == INVALID_HANDLE_VALUE || !GetNamedPipeClientProcessId(_pipe, &processId)) {
            return 0;
        }
        return processId;
    }

    bool Channel::Connect(const std::wstring &name, DWORD timeoutMs) {
        Close();
        if (!CreateEvents()) {
            Close();
            return false;
        }

        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        for (;;) {
            _pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
            if (_pipe != INVALID_HANDLE_VALUE) {
                return true;
            }
            const DWORD error = GetLastError();
            if ((error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND) || GetTickCount64() >= deadline) {
                Close();
                return false;
            }
            Sleep(10);
        }
    }

    void Channel::Start(MessageHandler onMessage, ClosedHandler onClosed) {
        _open.store(true);
        _reader = std::thread([this, onMessage = std::move(onMessage), onClosed = std::move(onClosed)]() mutable {
            ReadLoop(std::move(onMessage), std::move(onClosed));
        });
    }

    bool Channel::ReadExact(void *buffer, std::size_t size, HANDLE readEvent) {
        auto *bytes = static_cast<uint8_t *>(buffer);
        while (size > 0) {
            OVERLAPPED overlapped {};
            overlapped.hEvent = readEvent;
            ResetEvent(readEvent);

            const DWORD request = static_cast<DWORD>((std::min)(size, static_cast<std::size_t>(kPipeBufferBytes)));
            DWORD read          = 0;
            if (!ReadFile(_pipe, bytes, request, &read, &overlapped)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    return false;
                }
                const HANDLE waits[2] = {readEvent, _stopEvent};
                if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
                    CancelIoEx(_pipe, &overlapped);
                    GetOverlappedResult(_pipe, &overlapped, &read, TRUE);
                    return false;
                }
                if (!GetOverlappedResult(_pipe, &overlapped, &read, FALSE)) {
                    return false;
                }
            }
            if (read == 0) {
                return false;
            }
            bytes += read;
            size -= read;
        }
        return true;
    }

    void Channel::ReadLoop(MessageHandler onMessage, ClosedHandler onClosed) {
        const HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (readEvent) {
            uint8_t headerBytes[Protocol::kHeaderBytes];
            // Out of memory for a message, as a 32-bit client can be, ends the channel like a broken pipe.
            try {
                for (;;) {
                    Protocol::MessageHeader header;
                    if (!ReadExact(headerBytes, sizeof(headerBytes), readEvent) || !Protocol::ParseHeader(headerBytes, header, _maxReceiveBytes)) {
                        break;
                    }
                    std::vector<uint8_t> payload(header.payloadSize);
                    if (header.payloadSize > 0 && !ReadExact(payload.data(), payload.size(), readEvent)) {
                        break;
                    }
                    onMessage(static_cast<Protocol::MessageType>(header.type), std::move(payload));
                }
            }
            catch (const std::bad_alloc &) {
            }
            CloseHandle(readEvent);
        }
        _open.store(false);
        if (onClosed) {
            onClosed();
        }
    }

    bool Channel::Send(const std::vector<uint8_t> &message) {
        if (!_open.load() || message.size() < Protocol::kHeaderBytes || message.size() - Protocol::kHeaderBytes > _maxSendBytes) {
            return false;
        }

        std::scoped_lock lock(_writeMutex);
        // Re-checked under the lock: Close may have run, and a new pipe may be waiting for its peer.
        if (!_open.load() || _pipe == INVALID_HANDLE_VALUE) {
            return false;
        }
        std::size_t offset = 0;
        while (offset < message.size()) {
            OVERLAPPED overlapped {};
            overlapped.hEvent = _writeEvent;
            ResetEvent(_writeEvent);

            DWORD written = 0;
            if (!WriteFile(_pipe, message.data() + offset, static_cast<DWORD>(message.size() - offset), &written, &overlapped)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    _open.store(false);
                    return false;
                }
                if (WaitForSingleObject(_writeEvent, kWriteTimeoutMs) != WAIT_OBJECT_0) {
                    CancelIoEx(_pipe, &overlapped);
                    GetOverlappedResult(_pipe, &overlapped, &written, TRUE);
                    _open.store(false);
                    return false;
                }
                if (!GetOverlappedResult(_pipe, &overlapped, &written, FALSE)) {
                    _open.store(false);
                    return false;
                }
            }
            offset += written;
        }
        return true;
    }

    void Channel::Close() {
        if (_stopEvent) {
            SetEvent(_stopEvent);
        }
        if (_pipe != INVALID_HANDLE_VALUE) {
            CancelIoEx(_pipe, nullptr);
            // The kernel writes the OVERLAPPED until the cancelled connect completes.
            if (_connectPending) {
                DWORD transferred = 0;
                GetOverlappedResult(_pipe, &_connectOverlapped, &transferred, TRUE);
            }
        }
        if (_reader.joinable()) {
            _reader.join();
        }
        {
            std::scoped_lock lock(_writeMutex);
            if (_pipe != INVALID_HANDLE_VALUE) {
                CloseHandle(_pipe);
                _pipe = INVALID_HANDLE_VALUE;
            }
        }
        for (HANDLE *event : {&_stopEvent, &_writeEvent, &_connectEvent}) {
            if (*event) {
                CloseHandle(*event);
                *event = nullptr;
            }
        }
        _connectPending = false;
        _open.store(false);
    }
} // namespace Framework::GUI::Remote
