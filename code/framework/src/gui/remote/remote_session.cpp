/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "remote_session.h"

#include "launch.h"

#include "gui/resources/resource_registry.h"

#include <logging/logger.h>

#include <bcrypt.h>

#include <algorithm>

namespace Framework::GUI::Remote {
    namespace {
        constexpr ULONGLONG kMaxRestartDelayMs = 16000;
        constexpr DWORD kShutdownWaitMs        = 5000;

        // 128 random bits, hex: names the pipe and every frame section, so nothing but the
        // host this client started can find them.
        std::wstring RandomToken() {
            uint8_t bytes[16] = {};
            if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
                return {};
            }
            constexpr wchar_t kHex[] = L"0123456789abcdef";
            std::wstring token;
            for (const uint8_t byte : bytes) {
                token += kHex[byte >> 4];
                token += kHex[byte & 0xF];
            }
            return token;
        }
    } // namespace

    RemoteSession::RemoteSession(Resources::ResourceRegistry *registry)
        : _registry(registry)
        , _server(_channel, [registry](const std::string &host) {
            return registry->FindRoot(host);
        }) {}

    RemoteSession::~RemoteSession() {
        Stop();
    }

    Utils::Result<void, Error> RemoteSession::Start(const std::string &rootDir, const std::filesystem::path &hostPath) {
        std::error_code error;
        _rootDir  = std::filesystem::absolute(rootDir, error).string();
        _hostPath = hostPath;
        _restarts = 0;
        _started  = false;

        // Kill-on-close: should the client die without a word, the host and its helpers go with it.
        if (!_job) {
            _job = CreateJobObjectW(nullptr, nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (_job && !SetInformationJobObject(_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
                CloseHandle(_job);
                _job = nullptr;
            }
        }

        _server.Start();

        if (!Launch()) {
            _state = State::Failed;
            return Error(_failure);
        }
        // Only what fails fast is worth the game's thread: a host that cannot run here at all.
        const ULONGLONG waitUntil = GetTickCount64() + kStartWaitMs;
        while ((_state.load() == State::Connecting || (_state.load() == State::Handshaking && !_helloSeen)) && GetTickCount64() < waitUntil) {
            Advance(nullptr);
            Sleep(5);
        }
        const State state = _state.load();
        if (state != State::Connecting && state != State::Handshaking && state != State::Ready) {
            return Error("The CEF host did not start: " + _failure);
        }
        _started = true;
        return {};
    }

    bool RemoteSession::Launch() {
        _token = RandomToken();
        if (_token.empty()) {
            _failure = "no random source for the host's channel name";
            return false;
        }

        const std::wstring pipeName = L"\\\\.\\pipe\\fwcef-" + std::to_wstring(GetCurrentProcessId()) + L"-" + _token;
        if (!_channel.Listen(pipeName)) {
            _failure = "could not create the host's pipe (error " + std::to_string(GetLastError()) + ")";
            return false;
        }

        // A trailing backslash would escape the closing quote.
        std::wstring root = std::filesystem::path(_rootDir).wstring();
        while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) {
            root.pop_back();
        }
        std::wstring commandLine = L"\"" + _hostPath.wstring() + L"\"";
        commandLine += std::wstring(L" --") + kHostPipeSwitch + L"=" + pipeName;
        commandLine += std::wstring(L" --") + kHostTokenSwitch + L"=" + _token;
        commandLine += std::wstring(L" --") + kHostParentSwitch + L"=" + std::to_wstring(GetCurrentProcessId());
        commandLine += std::wstring(L" --") + kHostRootSwitch + L"=\"" + root + L"\"";

        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process {};
        const std::wstring workingDir = _hostPath.parent_path().wstring();
        if (!CreateProcessW(_hostPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, workingDir.c_str(), &startup, &process)) {
            _failure = "could not start '" + _hostPath.string() + "' (error " + std::to_string(GetLastError()) + ")";
            _channel.Close();
            return false;
        }
        if (_job && !AssignProcessToJobObject(_job, process.hProcess)) {
            Framework::Logging::GetLogger("Web")->debug("CEF host runs outside the kill-on-close job (error {}); it still exits with this process", GetLastError());
        }
        ResumeThread(process.hThread);
        CloseHandle(process.hThread);

        _process       = process.hProcess;
        _processId     = process.dwProcessId;
        _helloSeen     = false;
        _channelClosed = false;
        _deadline      = GetTickCount64() + kConnectTimeoutMs;
        _state         = State::Connecting;
        Framework::Logging::GetLogger("Web")->debug("Started CEF host '{}' (pid {})", _hostPath.string(), _processId);
        return true;
    }

    void RemoteSession::Teardown(bool kill) {
        _channel.Close();
        _server.Reset();
        if (_process) {
            if (kill && WaitForSingleObject(_process, 0) == WAIT_TIMEOUT) {
                TerminateProcess(_process, 1);
                WaitForSingleObject(_process, 1000);
            }
            CloseHandle(_process);
            _process   = nullptr;
            _processId = 0;
        }
        {
            std::scoped_lock lock(_inboxMutex);
            _inbox.clear();
            _inboxOverflow = false;
        }
        _channelClosed = false;
    }

    void RemoteSession::Fail(const std::string &reason, Listener *listener) {
        const auto logger   = Framework::Logging::GetLogger("Web");
        const bool wasReady = _state.load() == State::Ready;
        DWORD exitCode      = 0;
        if (_process && GetExitCodeProcess(_process, &exitCode) && exitCode != STILL_ACTIVE) {
            logger->error("CEF host: {} (exit code 0x{:X})", reason, exitCode);
        }
        else {
            logger->error("CEF host: {}", reason);
        }

        Teardown(true);
        _failure = reason;
        if (wasReady && listener && listener->onHostLost) {
            listener->onHostLost();
        }
        // The budget is for a host that keeps dying, not for one crash an hour.
        if (wasReady && GetTickCount64() - _readyAt >= kStableRunMs) {
            _restarts = 0;
        }

        // Start() reports a failure while it waits to the caller instead.
        if (_started && _restarts < kMaxRestarts) {
            ++_restarts;
            const ULONGLONG delay = (std::min)(kMaxRestartDelayMs, 1000ull << (_restarts - 1));
            _restartAt            = GetTickCount64() + delay;
            _state                = State::Restarting;
            logger->warn("Restarting the CEF host in {} ms (attempt {} of {})", delay, _restarts, kMaxRestarts);
            return;
        }
        if (_started) {
            logger->error("CEF host failed {} times in a row; web views stay blank for the rest of this session", _restarts + 1);
        }
        _state = State::Failed;
    }

    void RemoteSession::Advance(Listener *listener) {
        switch (_state.load()) {
        case State::Stopped:
        case State::Failed:
        case State::Ready:
        case State::Handshaking: break;
        case State::Restarting:
            if (GetTickCount64() >= _restartAt && !Launch()) {
                Fail(_failure, listener);
            }
            return;
        case State::Connecting: {
            switch (_channel.PollConnect()) {
            case Channel::ConnectState::Connected: {
                // Only the process started may hold the pipe; one the kernel cannot name is refused too.
                const DWORD peer = _channel.GetPeerProcessId();
                if (peer != _processId) {
                    Fail("an unexpected process (pid " + std::to_string(peer) + ") connected to the host's pipe", listener);
                    return;
                }
                _channel.Start(
                    [this](Protocol::MessageType type, std::vector<uint8_t> payload) {
                        OnChannelMessage(type, std::move(payload));
                    },
                    [this] {
                        _channelClosed = true;
                    });
                _deadline = GetTickCount64() + kHandshakeTimeoutMs;
                _state    = State::Handshaking;
                break;
            }
            case Channel::ConnectState::Failed: Fail("the host's pipe failed while waiting for it", listener); return;
            case Channel::ConnectState::Pending:
                if (WaitForSingleObject(_process, 0) == WAIT_OBJECT_0) {
                    Fail("exited before connecting", listener);
                }
                else if (GetTickCount64() > _deadline) {
                    Fail("did not connect within " + std::to_string(kConnectTimeoutMs) + " ms", listener);
                }
                return;
            }
            break;
        }
        }

        const State state = _state.load();
        if (state != State::Handshaking && state != State::Ready) {
            return;
        }

        std::deque<std::pair<Protocol::MessageType, std::vector<uint8_t>>> inbox;
        bool overflow = false;
        {
            std::scoped_lock lock(_inboxMutex);
            inbox.swap(_inbox);
            overflow = _inboxOverflow;
        }
        for (const auto &[type, payload] : inbox) {
            HandleMessage(type, payload, listener);
            const State now = _state.load();
            if (now != State::Handshaking && now != State::Ready) {
                return;
            }
        }

        if (overflow) {
            Fail("sent more than the client could take", listener);
        }
        else if (_channelClosed.load() || !_channel.IsOpen()) {
            Fail("its channel closed", listener);
        }
        else if (WaitForSingleObject(_process, 0) == WAIT_OBJECT_0) {
            Fail("exited", listener);
        }
        else if (_state.load() == State::Handshaking && GetTickCount64() > _deadline) {
            Fail("CEF did not come up within " + std::to_string(kHandshakeTimeoutMs) + " ms", listener);
        }
    }

    void RemoteSession::HandleMessage(Protocol::MessageType type, const std::vector<uint8_t> &payload, Listener *listener) {
        using Protocol::MessageType;
        const auto malformed = [this, type, listener] {
            Fail("sent a malformed message (type " + std::to_string(static_cast<int>(type)) + ")", listener);
        };

        switch (type) {
        case MessageType::Hello: {
            Protocol::Hello hello;
            if (!Protocol::Decode(payload, hello)) {
                return malformed();
            }
            if (hello.version != Protocol::kVersion) {
                return Fail("speaks protocol " + std::to_string(hello.version) + ", this client " + std::to_string(Protocol::kVersion), listener);
            }
            _helloSeen = true;
            return;
        }
        case MessageType::Ready: {
            Protocol::Ready ready;
            if (!Protocol::Decode(payload, ready) || !_helloSeen || _state.load() != State::Handshaking) {
                return malformed();
            }
            if (!ready.ok) {
                return Fail(ready.error.empty() ? "CEF failed to start" : ready.error, listener);
            }
            _state   = State::Ready;
            _readyAt = GetTickCount64();
            SendRoots();
            Framework::Logging::GetLogger("Web")->info("CEF host ready (pid {})", _processId);
            if (listener && listener->onHostReady) {
                listener->onHostReady();
            }
            return;
        }
        default: break;
        }

        // View traffic only makes sense from a host that is up.
        if (_state.load() != State::Ready) {
            return malformed();
        }
        switch (type) {
        case MessageType::ViewEvent: {
            Protocol::ViewEventMessage message;
            if (!Protocol::Decode(payload, message)) {
                return malformed();
            }
            if (listener && listener->onMessage) {
                listener->onMessage(message);
            }
            return;
        }
        case MessageType::PageEvent: {
            Protocol::PageEvent message;
            if (!Protocol::Decode(payload, message)) {
                return malformed();
            }
            if (listener && listener->onMessage) {
                listener->onMessage(message);
            }
            return;
        }
        case MessageType::FrameSurface: {
            Protocol::FrameSurface message;
            if (!Protocol::Decode(payload, message) || message.width <= 0 || message.height <= 0 || message.width > Protocol::kMaxViewDimension || message.height > Protocol::kMaxViewDimension) {
                return malformed();
            }
            if (listener && listener->onMessage) {
                listener->onMessage(message);
            }
            return;
        }
        default: Framework::Logging::GetLogger("Web")->debug("Ignoring message type {} from the CEF host", static_cast<int>(type)); return;
        }
    }

    void RemoteSession::OnChannelMessage(Protocol::MessageType type, std::vector<uint8_t> payload) {
        // Resource traffic goes straight to the server, which must not wait for the game's tick.
        switch (type) {
        case Protocol::MessageType::ResourceRequest: {
            Protocol::ResourceRequest request;
            if (Protocol::Decode(payload, request)) {
                _server.OnRequest(std::move(request));
            }
            return;
        }
        case Protocol::MessageType::ResourceAck: {
            Protocol::ResourceAck ack;
            if (Protocol::Decode(payload, ack)) {
                _server.OnAck(ack);
            }
            return;
        }
        case Protocol::MessageType::ResourceSeek: {
            Protocol::ResourceSeek seek;
            if (Protocol::Decode(payload, seek)) {
                _server.OnSeek(seek);
            }
            return;
        }
        case Protocol::MessageType::ResourceCancel: {
            Protocol::ResourceCancel cancel;
            if (Protocol::Decode(payload, cancel)) {
                _server.OnCancel(cancel);
            }
            return;
        }
        default: break;
        }

        std::scoped_lock lock(_inboxMutex);
        if (_inbox.size() >= kMaxInboxMessages) {
            _inboxOverflow = true;
            return;
        }
        _inbox.emplace_back(type, std::move(payload));
    }

    void RemoteSession::SendRoots() {
        for (const auto &host : _registry->GetRootHosts()) {
            RegisterRoot(host);
        }
    }

    void RemoteSession::RegisterRoot(const std::string &host) {
        Protocol::RegisterRoot message;
        message.host = host;
        Send(message);
    }

    void RemoteSession::UnregisterRoot(const std::string &host) {
        Protocol::UnregisterRoot message;
        message.host = host;
        Send(message);
    }

    void RemoteSession::Poll(Listener &listener) {
        Advance(&listener);
    }

    void RemoteSession::Stop() {
        if (_state.load() == State::Ready) {
            _channel.Send(Protocol::Shutdown {});
            if (_process) {
                WaitForSingleObject(_process, kShutdownWaitMs);
            }
        }
        Teardown(true);
        _state   = State::Stopped;
        _started = false;
        _server.Stop();

        if (_job) {
            CloseHandle(_job);
            _job = nullptr;
        }
    }
} // namespace Framework::GUI::Remote
