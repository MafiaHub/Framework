/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "host.h"

#include "gui/cef/runtime.h"

#include <logging/logger.h>

#include <algorithm>

namespace Framework::GUI::Remote {
    namespace {
        constexpr DWORD kConnectTimeoutMs = 10000;

        // A delayed pump request is brought forward to this, as cefclient's external pump does, so
        // one CEF under-reports costs a frame at most.
        constexpr int64_t kMaxPumpDelayMs = 1000 / 30;

        // With nothing asked of it the loop still wakes this often, for the views' navigation retry.
        constexpr DWORD kIdleWakeMs = 250;

        bool ValidSize(int32_t width, int32_t height) {
            return width > 0 && height > 0 && width <= Protocol::kMaxViewDimension && height <= Protocol::kMaxViewDimension;
        }
    } // namespace

    Host::Host(std::wstring token, std::string rootDir, std::filesystem::path moduleDir): _token(std::move(token)), _rootDir(std::move(rootDir)), _moduleDir(std::move(moduleDir)) {
        _wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }

    Host::~Host() {
        _channel.Close();
        if (_wake) {
            CloseHandle(_wake);
        }
    }

    template <class T>
    bool Host::Read(const std::vector<uint8_t> &payload, T &message) {
        if (Protocol::Decode(payload, message)) {
            return true;
        }
        Framework::Logging::GetLogger("Web")->error("Malformed message {} from the client; closing", static_cast<int>(T::kType));
        _shutdownRequested = true;
        return false;
    }

    HostView *Host::FindView(int32_t viewId) {
        const auto it = _views.find(viewId);
        return it == _views.end() ? nullptr : it->second.get();
    }

    void Host::CloseView(std::unique_ptr<HostView> view) {
        view->Close();
        _closing.push_back(std::move(view));
    }

    void Host::SchedulePump(int64_t delayMs) {
        const ULONGLONG due = GetTickCount64() + static_cast<ULONGLONG>(std::clamp<int64_t>(delayMs, 0, kMaxPumpDelayMs));
        ULONGLONG current   = _pumpDueAt.load();
        while (due < current && !_pumpDueAt.compare_exchange_weak(current, due)) {}
        SetEvent(_wake);
    }

    DWORD Host::WaitTimeout() const {
        const ULONGLONG due = _pumpDueAt.load();
        if (due == kNoPump) {
            return kIdleWakeMs;
        }
        const ULONGLONG now = GetTickCount64();
        return due <= now ? 0 : static_cast<DWORD>((std::min)(due - now, static_cast<ULONGLONG>(kIdleWakeMs)));
    }

    void Host::OnMessage(Protocol::MessageType type, std::vector<uint8_t> payload) {
        // Resource traffic feeds CEF file threads blocked in the broker, not the UI thread.
        switch (type) {
        case Protocol::MessageType::ResourceResponse: {
            Protocol::ResourceResponse response;
            if (Protocol::Decode(payload, response)) {
                _broker->OnResponse(response);
            }
            return;
        }
        case Protocol::MessageType::ResourceData: {
            Protocol::ResourceData data;
            if (Protocol::Decode(payload, data)) {
                _broker->OnData(data);
            }
            return;
        }
        case Protocol::MessageType::ResourceEnd: {
            Protocol::ResourceEnd end;
            if (Protocol::Decode(payload, end)) {
                _broker->OnEnd(end);
            }
            return;
        }
        default: break;
        }

        {
            std::scoped_lock lock(_inboxMutex);
            _inbox.emplace_back(type, std::move(payload));
        }
        SetEvent(_wake);
    }

    void Host::Dispatch(Protocol::MessageType type, const std::vector<uint8_t> &payload) {
        using Protocol::MessageType;
        const auto logger = Framework::Logging::GetLogger("Web");

        switch (type) {
        case MessageType::CreateView: {
            Protocol::CreateView message;
            if (!Read(payload, message)) {
                return;
            }
            if (!ValidSize(message.width, message.height) || FindView(message.viewId)) {
                logger->error("Refusing view {} ({}x{})", message.viewId, message.width, message.height);
                return;
            }
            auto view = std::make_unique<HostView>(message.viewId, _channel, _token);
            if (view->Create(message.url, message.width, message.height)) {
                _views[message.viewId] = std::move(view);
            }
            return;
        }
        case MessageType::DestroyView: {
            Protocol::DestroyView message;
            if (!Read(payload, message)) {
                return;
            }
            if (const auto it = _views.find(message.viewId); it != _views.end()) {
                CloseView(std::move(it->second));
                _views.erase(it);
            }
            return;
        }
        case MessageType::RegisterRoot: {
            Protocol::RegisterRoot message;
            if (Read(payload, message)) {
                _registry->RegisterRoot(message.host, std::make_shared<RemoteResourceProvider>(message.host, _broker));
            }
            return;
        }
        case MessageType::UnregisterRoot: {
            Protocol::UnregisterRoot message;
            if (Read(payload, message)) {
                _registry->UnregisterRoot(message.host);
            }
            return;
        }
        case MessageType::Shutdown: _shutdownRequested = true; return;
        default: break;
        }

        // Everything else addresses one view; a message for one that is gone is dropped.
        const auto withView = [this, &payload]<class T>(T message, auto apply) {
            if (!Read(payload, message)) {
                return;
            }
            if (HostView *view = FindView(message.viewId)) {
                apply(*view, message);
            }
        };

        switch (type) {
        case MessageType::ResizeView:
            withView(Protocol::ResizeView {}, [](HostView &view, const Protocol::ResizeView &m) {
                if (ValidSize(m.width, m.height)) {
                    view.Resize(m.width, m.height);
                }
            });
            break;
        case MessageType::LoadUrl:
            withView(Protocol::LoadUrl {}, [](HostView &view, const Protocol::LoadUrl &m) {
                view.LoadURL(m.url);
            });
            break;
        case MessageType::LockOrigin:
            withView(Protocol::LockOrigin {}, [](HostView &view, const Protocol::LockOrigin &m) {
                view.LockToOrigin(m.url);
            });
            break;
        case MessageType::SetFocus:
            withView(Protocol::SetFocus {}, [](HostView &view, const Protocol::SetFocus &m) {
                view.Focus(m.focused);
            });
            break;
        case MessageType::SetAudioMuted:
            withView(Protocol::SetAudioMuted {}, [](HostView &view, const Protocol::SetAudioMuted &m) {
                view.SetAudioMuted(m.muted);
            });
            break;
        case MessageType::EvaluateScript:
            withView(Protocol::EvaluateScript {}, [](HostView &view, const Protocol::EvaluateScript &m) {
                view.EvaluateScript(m.script);
            });
            break;
        case MessageType::BeginFrame:
            withView(Protocol::BeginFrame {}, [](HostView &view, const Protocol::BeginFrame &) {
                view.BeginFrame();
            });
            break;
        case MessageType::MouseMove:
            withView(Protocol::MouseMove {}, [](HostView &view, const Protocol::MouseMove &m) {
                view.MouseMove(m);
            });
            break;
        case MessageType::MouseClick:
            withView(Protocol::MouseClick {}, [](HostView &view, const Protocol::MouseClick &m) {
                view.MouseClick(m);
            });
            break;
        case MessageType::MouseWheel:
            withView(Protocol::MouseWheel {}, [](HostView &view, const Protocol::MouseWheel &m) {
                view.MouseWheel(m);
            });
            break;
        case MessageType::KeyEvent:
            withView(Protocol::KeyEvent {}, [](HostView &view, const Protocol::KeyEvent &m) {
                view.Key(m);
            });
            break;
        case MessageType::ImeSetComposition:
            withView(Protocol::ImeSetComposition {}, [](HostView &view, const Protocol::ImeSetComposition &m) {
                view.ImeSetComposition(m);
            });
            break;
        case MessageType::ImeCommitText:
            withView(Protocol::ImeCommitText {}, [](HostView &view, const Protocol::ImeCommitText &m) {
                view.ImeCommitText(m);
            });
            break;
        case MessageType::ImeFinishComposing:
            withView(Protocol::ImeFinishComposing {}, [](HostView &view, const Protocol::ImeFinishComposing &m) {
                view.ImeFinishComposing(m.keepSelection);
            });
            break;
        default: logger->warn("Ignoring unknown message {} from the client", static_cast<int>(type)); break;
        }
    }

    int Host::Run(const std::wstring &pipeName) {
        const auto logger = Framework::Logging::GetLogger("Web");
        if (!_wake || !_channel.Connect(pipeName, kConnectTimeoutMs)) {
            logger->error("CEF host could not connect to the client (error {})", GetLastError());
            return 1;
        }

        _broker = std::make_shared<ResourceBroker>(_channel);
        _channel.Start(
            [this](Protocol::MessageType type, std::vector<uint8_t> payload) {
                OnMessage(type, std::move(payload));
            },
            [this] {
                _closed.store(true);
                _broker->FailAll();
                SetEvent(_wake);
            });

        _channel.Send(Protocol::Hello {});

        // The client composites on the CPU path only; there is no game device here to share with.
        _app = new CEF::App();
        _app->SetGPUAccelerated(false);
        _app->SetMessagePumpScheduler([this](int64_t delayMs) {
            SchedulePump(delayMs);
        });
        _registry = new Resources::ResourceRegistry();
        if (!CEF::StartRuntime(_rootDir, _moduleDir, _app, _registry, _profileLock)) {
            Protocol::Ready ready;
            ready.error = "CefInitialize failed; see cef_host.log and cef.log";
            _channel.Send(ready);
            return 2;
        }

        Protocol::Ready ready;
        ready.ok = true;
        _channel.Send(ready);
        logger->info("CEF host ready (pid {})", GetCurrentProcessId());

        while (!_shutdownRequested && !_closed.load() && !_pumpFailed) {
            const DWORD woke = MsgWaitForMultipleObjects(1, &_wake, FALSE, WaitTimeout(), QS_ALLINPUT);

            std::deque<std::pair<Protocol::MessageType, std::vector<uint8_t>>> inbox;
            {
                std::scoped_lock lock(_inboxMutex);
                inbox.swap(_inbox);
            }
            for (const auto &[type, payload] : inbox) {
                Dispatch(type, payload);
                if (_shutdownRequested) {
                    break;
                }
            }

            // When CEF asked for it, a begin frame among the messages above included, and for
            // window messages, which only CEF's pump dispatches here. Cleared first: a request made
            // during the pump is for after it.
            if (woke == WAIT_OBJECT_0 + 1 || _pumpDueAt.load() <= GetTickCount64()) {
                _pumpDueAt.store(kNoPump);
                if (!CEF::PumpMessageLoopGuarded()) {
                    _pumpFailed = true;
                    logger->warn("CEF message pump faulted; host exiting");
                    break;
                }
            }
            std::erase_if(_closing, [](const std::unique_ptr<HostView> &view) {
                return view->HasClosed();
            });
            for (auto &[id, view] : _views) {
                view->CheckNavigationStarted();
                view->PublishFrame();
            }
        }

        logger->info("CEF host stopping ({})", _closed.load() ? "client gone" : _pumpFailed ? "pump faulted" : "asked to");
        for (auto &[id, view] : _views) {
            CloseView(std::move(view));
        }
        _views.clear();
        bool stopped = false;
        if (!_pumpFailed) {
            stopped = CEF::StopRuntime(_pumpFailed);
        }
        // Past the drain CEF calls into no view again, closed or not.
        _closing.clear();
        // A skipped CefShutdown can leave CEF owning the profile until exit.
        if (stopped && _profileLock != INVALID_HANDLE_VALUE) {
            CloseHandle(_profileLock);
            _profileLock = INVALID_HANDLE_VALUE;
        }
        _broker->FailAll();
        _channel.Close();
        return 0;
    }
} // namespace Framework::GUI::Remote
