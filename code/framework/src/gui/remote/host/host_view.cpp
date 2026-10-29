/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "host_view.h"

#include <logging/logger.h>

#include <algorithm>

namespace Framework::GUI::Remote {
    namespace {
        // Same cap as View: external begin frames drive the cadence.
        constexpr int kWindowlessFrameRate = 240;

        CefMouseEvent MouseEventOf(int32_t x, int32_t y, uint32_t modifiers) {
            CefMouseEvent event;
            event.x         = x;
            event.y         = y;
            event.modifiers = modifiers;
            return event;
        }
    } // namespace

    HostView::HostView(int32_t id, Channel &channel, const std::wstring &token): _id(id), _channel(channel), _token(token) {
        _sdk = std::make_unique<SDK>();
        _sdk->SetEventSink([this](const std::string &name, const std::string &payload) {
            Protocol::PageEvent message;
            message.viewId  = _id;
            message.name    = name;
            message.payload = payload;
            Send(Protocol::Encode(message), "callEvent");
        });
    }

    HostView::~HostView() {
        Close();
    }

    void HostView::Close() {
        if (!_browser) {
            return;
        }
        _sdk->SetEventSink({});
        _client->SetViewEventCallback({});
        _loadHandler->SetViewEventCallback({});
        _displayHandler->SetViewEventCallback({});
        _lifeSpanHandler->SetViewEventCallback({});
        _browser->GetHost()->CloseBrowser(true);
        _browser = nullptr;
        _frames.Close();
    }

    bool HostView::HasClosed() const {
        return _lifeSpanHandler->HasClosed();
    }

    void HostView::Send(const std::vector<uint8_t> &message, const char *what) {
        // A message over the client's limit is refused and the channel stays up; nothing else fails while it is open.
        if (!_channel.Send(message) && _channel.IsOpen()) {
            Framework::Logging::GetLogger("Web")->warn("View {}: dropped a {} of {} bytes, over the client's limit of {}", _id, what, message.size(), Protocol::kMaxHostMessageBytes);
        }
    }

    bool HostView::Create(const std::string &url, int width, int height) {
        _renderHandler   = new CEF::RenderHandler();
        _lifeSpanHandler = new CEF::LifeSpanHandler();
        _loadHandler     = new CEF::LoadHandler();
        _displayHandler  = new CEF::DisplayHandler();
        _renderHandler->SetDimensions(width, height);

        const auto forward = [this](const ViewEventData &data) {
            Emit(data);
        };
        _loadHandler->SetViewEventCallback(forward);
        _displayHandler->SetViewEventCallback(forward);
        _lifeSpanHandler->SetViewEventCallback(forward);

        _client = new CEF::Client(_renderHandler, _lifeSpanHandler, _loadHandler, _displayHandler, _sdk.get());
        _client->SetViewEventCallback(forward);

        // The client composites on a game device this process has no access to, so the CPU paint
        // path is the only one.
        CefWindowInfo windowInfo;
        windowInfo.SetAsWindowless(nullptr);
        windowInfo.shared_texture_enabled       = false;
        windowInfo.external_begin_frame_enabled = true;

        CefBrowserSettings browserSettings;
        browserSettings.windowless_frame_rate = kWindowlessFrameRate;
        browserSettings.background_color      = CefColorSetARGB(0, 0, 0, 0);

        _browser = CefBrowserHost::CreateBrowserSync(windowInfo, _client, url, browserSettings, nullptr, nullptr);
        if (!_browser) {
            Framework::Logging::GetLogger("Web")->error("View {}: failed to create CEF browser", _id);
            return false;
        }
        return true;
    }

    void HostView::Emit(const ViewEventData &data) {
        if (data.event == ViewEvent::LoadingStart && data.isMainFrame) {
            _navigationStarted = true;
        }
        Protocol::ViewEventMessage message;
        message.viewId = _id;
        message.data   = data;
        Send(Protocol::Encode(message), "view event");
    }

    void HostView::Resize(int width, int height) {
        _renderHandler->SetDimensions(width, height);
        _browser->GetHost()->WasResized();
    }

    void HostView::LoadURL(const std::string &url) {
        if (!_browser->GetMainFrame()) {
            return;
        }
        if (!_lifeSpanHandler->GetAllowedOrigin().empty()) {
            LockToOrigin(url);
        }
        _browser->GetMainFrame()->LoadURL(url);
    }

    void HostView::LockToOrigin(const std::string &url) {
        std::string origin = CEF::LifeSpanHandler::OriginFromURL(url);
        if (origin.empty()) {
            // unparsable origin (data:, about:): lock everything out rather than open up
            Framework::Logging::GetLogger("Web")->warn("View {}: cannot derive origin from '{}', locking view down", _id, url);
            origin = "null";
        }

        if (_lifeSpanHandler->GetAllowedOrigin() == origin) {
            return;
        }
        _lifeSpanHandler->SetAllowedOrigin(origin);

        ViewEventData data;
        data.event  = ViewEvent::OriginChange;
        data.origin = std::move(origin);
        data.url    = url;
        Emit(data);
    }

    void HostView::Focus(bool focused) {
        _browser->GetHost()->SetFocus(focused);
    }

    void HostView::SetAudioMuted(bool muted) {
        _browser->GetHost()->SetAudioMuted(muted);
    }

    void HostView::EvaluateScript(const std::string &script) {
        if (auto frame = _browser->GetMainFrame()) {
            frame->ExecuteJavaScript(script, "", 0);
        }
    }

    void HostView::BeginFrame() {
        _browser->GetHost()->SendExternalBeginFrame();
    }

    void HostView::MouseMove(const Protocol::MouseMove &message) {
        _browser->GetHost()->SendMouseMoveEvent(MouseEventOf(message.x, message.y, message.modifiers), message.mouseLeave);
    }

    void HostView::MouseClick(const Protocol::MouseClick &message) {
        if (message.button > MBT_RIGHT) {
            return;
        }
        const int clickCount = std::clamp(message.clickCount, 1, 3);
        _browser->GetHost()->SendMouseClickEvent(MouseEventOf(message.x, message.y, message.modifiers), static_cast<cef_mouse_button_type_t>(message.button), message.mouseUp, clickCount);
    }

    void HostView::MouseWheel(const Protocol::MouseWheel &message) {
        _browser->GetHost()->SendMouseWheelEvent(MouseEventOf(message.x, message.y, message.modifiers), message.deltaX, message.deltaY);
    }

    void HostView::Key(const Protocol::KeyEvent &message) {
        if (message.type > KEYEVENT_CHAR) {
            return;
        }
        CefKeyEvent event;
        event.type                    = static_cast<cef_key_event_type_t>(message.type);
        event.modifiers               = message.modifiers;
        event.windows_key_code        = message.windowsKeyCode;
        event.native_key_code         = message.nativeKeyCode;
        event.is_system_key           = message.isSystemKey;
        event.character               = static_cast<char16_t>(message.character);
        event.unmodified_character    = static_cast<char16_t>(message.unmodifiedCharacter);
        event.focus_on_editable_field = message.focusOnEditableField;
        _browser->GetHost()->SendKeyEvent(event);
    }

    void HostView::ImeSetComposition(const Protocol::ImeSetComposition &message) {
        _browser->GetHost()->ImeSetComposition(CefString(message.text), {}, CefRange::InvalidRange(), CefRange(message.selectionStart, message.selectionEnd));
    }

    void HostView::ImeCommitText(const Protocol::ImeCommitText &message) {
        _browser->GetHost()->ImeCommitText(CefString(message.text), CefRange::InvalidRange(), 0);
    }

    void HostView::ImeFinishComposing(bool keepSelection) {
        _browser->GetHost()->ImeFinishComposingText(keepSelection);
    }

    void HostView::CheckNavigationStarted() {
        // A navigation the renderer never commits raises nothing at all: no load
        // error, no termination. Retry it rather than leave the view blank.
        if (_navigationStarted || _navigationGaveUp) {
            return;
        }
        const auto now = GetTickCount64();
        if (_navigationIssuedAt == 0) {
            _navigationIssuedAt = now;
            return;
        }
        if (now - _navigationIssuedAt < kNavigationStartTimeoutMs) {
            return;
        }

        _navigationIssuedAt = now;
        const auto url      = _browser->GetMainFrame() ? _browser->GetMainFrame()->GetURL().ToString() : std::string();
        if (_navigationReloads >= kMaxNavigationReloads) {
            _navigationGaveUp = true;
            Framework::Logging::GetLogger("Web")->error("View {}: '{}' still has not started loading after {} reloads", _id, url, _navigationReloads);
            return;
        }
        ++_navigationReloads;
        Framework::Logging::GetLogger("Web")->warn("View {}: '{}' did not start loading within {} ms, reloading", _id, url, kNavigationStartTimeoutMs);
        _browser->Reload();
    }

    void HostView::PublishFrame() {
        const auto lock = _renderHandler->LockPixels();
        if (!_renderHandler->IsPixelDataDirty()) {
            return;
        }

        const auto &pixels = _renderHandler->GetPixelData();
        const int width    = _renderHandler->GetPixelWidth();
        const int height   = _renderHandler->GetPixelHeight();
        if (pixels.empty() || width <= 0 || height <= 0 || pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
            return;
        }

        DirtyRect dirty;
        if (!_frames.IsOpen() || _frames.GetWidth() != static_cast<uint32_t>(width) || _frames.GetHeight() != static_cast<uint32_t>(height)) {
            // A new size gets a new section; the client swaps to it when told.
            ++_generation;
            if (!_frames.Create(FrameSurfaceName(_token, _id, _generation), static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
                if (!_surfaceFailed) {
                    _surfaceFailed = true;
                    Framework::Logging::GetLogger("Web")->error("View {}: could not create a {}x{} frame surface (error {})", _id, width, height, GetLastError());
                }
                return;
            }
            _surfaceFailed = false;

            Protocol::FrameSurface surface;
            surface.viewId     = _id;
            surface.generation = _generation;
            surface.width      = width;
            surface.height     = height;
            _channel.Send(surface);
            dirty = {0, 0, static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        }
        else {
            const CefRect rect = _renderHandler->GetPixelDirtyRect();
            if (rect.IsEmpty()) {
                _renderHandler->ClearPixelDataDirty();
                return;
            }
            dirty = DirtyRect {static_cast<uint32_t>((std::max)(0, rect.x)), static_cast<uint32_t>((std::max)(0, rect.y)), static_cast<uint32_t>((std::max)(0, rect.width)), static_cast<uint32_t>((std::max)(0, rect.height))};
        }

        _frames.Publish(pixels.data(), dirty);
        _renderHandler->ClearPixelDataDirty();
    }
} // namespace Framework::GUI::Remote
