/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "remote_view.h"

#include "graphics/backend/d3d8.h"
#include "logging/logger.h"

#include <cstring>

namespace Framework::GUI::Remote {
    namespace {
        std::u16string ToUtf16(const std::wstring &text) {
            static_assert(sizeof(wchar_t) == sizeof(char16_t));
            return std::u16string(text.begin(), text.end());
        }
    } // namespace

    RemoteView::RemoteView(int id, Graphics::Renderer *graphicsRenderer, Manager *manager, RemoteSession *session): View(id, graphicsRenderer, manager), _session(session) {}

    RemoteView::~RemoteView() {
        if (HasBrowser()) {
            Protocol::DestroyView message;
            message.viewId = _id;
            _session->Send(message);
        }
    }

    Utils::Result<void, Framework::Error> RemoteView::Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated) {
        (void)gpuAccelerated;
        _width  = width;
        _height = height;
        _x      = offsetX;
        _y      = offsetY;
        _url    = url;
        // Asynchronous: Created and the load events arrive through the Manager like any others.
        CreateOnHost();
        return {};
    }

    void RemoteView::CreateOnHost() {
        Protocol::CreateView create;
        create.viewId = _id;
        create.url    = _url;
        create.width  = _width;
        create.height = _height;
        if (!_session->Send(create)) {
            return;
        }
        _onHost = true;

        if (!_lockedUrl.empty()) {
            Protocol::LockOrigin lock;
            lock.url = _lockedUrl;
            Post(lock);
        }
        if (_hasFocus) {
            Protocol::SetFocus focus;
            focus.focused = true;
            Post(focus);
        }
        // A new browser starts unmuted; the Manager mutes it again on its next tick if it must.
        _audioMuted = false;
    }

    bool RemoteView::HasBrowser() const {
        return _onHost && _session->IsReady();
    }

    void RemoteView::RequestBeginFrame() {
        if (!IsOnScreen() && !_offscreen) {
            return;
        }
        Post(Protocol::BeginFrame {});
    }

    void RemoteView::SendMouseMove(const CefMouseEvent &event, bool mouseLeave) {
        Protocol::MouseMove message;
        message.x          = event.x;
        message.y          = event.y;
        message.modifiers  = event.modifiers;
        message.mouseLeave = mouseLeave;
        Post(message);
    }

    void RemoteView::SendMouseClick(const CefMouseEvent &event, cef_mouse_button_type_t button, bool mouseUp, int clickCount) {
        Protocol::MouseClick message;
        message.x          = event.x;
        message.y          = event.y;
        message.modifiers  = event.modifiers;
        message.button     = static_cast<uint8_t>(button);
        message.mouseUp    = mouseUp;
        message.clickCount = clickCount;
        Post(message);
    }

    void RemoteView::SendMouseWheel(const CefMouseEvent &event, int deltaX, int deltaY) {
        Protocol::MouseWheel message;
        message.x         = event.x;
        message.y         = event.y;
        message.modifiers = event.modifiers;
        message.deltaX    = deltaX;
        message.deltaY    = deltaY;
        Post(message);
    }

    void RemoteView::SendKeyEvent(const CefKeyEvent &event) {
        Protocol::KeyEvent message;
        message.type                 = static_cast<uint8_t>(event.type);
        message.modifiers            = event.modifiers;
        message.windowsKeyCode       = event.windows_key_code;
        message.nativeKeyCode        = event.native_key_code;
        message.isSystemKey          = event.is_system_key != 0;
        message.character            = static_cast<uint16_t>(event.character);
        message.unmodifiedCharacter  = static_cast<uint16_t>(event.unmodified_character);
        message.focusOnEditableField = event.focus_on_editable_field != 0;
        Post(message);
    }

    void RemoteView::ImeSetComposition(const std::wstring &text, int selectionStart, int selectionEnd) {
        Protocol::ImeSetComposition message;
        message.text           = ToUtf16(text);
        message.selectionStart = selectionStart;
        message.selectionEnd   = selectionEnd;
        Post(message);
    }

    void RemoteView::ImeCommitText(const std::wstring &text) {
        Protocol::ImeCommitText message;
        message.text = ToUtf16(text);
        Post(message);
    }

    void RemoteView::ImeFinishComposingText(bool keepSelection) {
        Protocol::ImeFinishComposing message;
        message.keepSelection = keepSelection;
        Post(message);
    }

    void RemoteView::Focus(bool enable) {
        _hasFocus = enable;
        Protocol::SetFocus message;
        message.focused = enable;
        Post(message);
    }

    void RemoteView::SetAudioMuted(bool muted) {
        if (!HasBrowser() || _audioMuted == muted) {
            return;
        }
        _audioMuted = muted;
        Protocol::SetAudioMuted message;
        message.muted = muted;
        Post(message);
    }

    void RemoteView::Resize(int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        {
            std::scoped_lock lock(_renderMutex);
            _width  = width;
            _height = height;
        }
        Protocol::ResizeView message;
        message.width  = width;
        message.height = height;
        Post(message);
    }

    void RemoteView::LockToOrigin(const std::string &url) {
        // The host derives the origin and reports it with OriginChange, as View does in process.
        _lockedUrl = url;
        Protocol::LockOrigin message;
        message.url = url;
        Post(message);
    }

    void RemoteView::LoadURL(const std::string &url) {
        _url = url;
        if (!_lockedUrl.empty()) {
            _lockedUrl = url;
        }
        Protocol::LoadUrl message;
        message.url = url;
        Post(message);
    }

    void RemoteView::EvaluateScript(const std::string &script) {
        Protocol::EvaluateScript message;
        message.script = script;
        Post(message);
    }

    void RemoteView::OnHostEvent(const ViewEventData &data) {
        if (data.event == ViewEvent::CursorChange) {
            _cursorType = data.cursorType >= 0 && data.cursorType < CT_NUM_VALUES ? static_cast<cef_cursor_type_t>(data.cursorType) : CT_POINTER;
        }
        // A restarted host creates the browser again; to scripts that is a reload, not a new view.
        if (data.event == ViewEvent::Created && IsCreated()) {
            return;
        }
        EmitViewEvent(data);
    }

    void RemoteView::OnHostPageEvent(const std::string &name, const std::string &payload) {
        if (_sdk) {
            _sdk->BroadcastEvent(name, payload);
        }
    }

    void RemoteView::OnHostSurface(uint32_t generation, int width, int height) {
        std::scoped_lock lock(_renderMutex);
        _uploadedSeq = 0;
        if (!_frames.Open(FrameSurfaceName(_session->GetToken(), _id, generation))) {
            // Already replaced by a newer one, whose own message follows.
            Framework::Logging::GetLogger("Web")->debug("View {}: frame surface {} ({}x{}) is gone or invalid", _id, generation, width, height);
            return;
        }
        if (_frames.GetWidth() != static_cast<uint32_t>(width) || _frames.GetHeight() != static_cast<uint32_t>(height)) {
            Framework::Logging::GetLogger("Web")->error("View {}: frame surface {} is {}x{}, announced as {}x{}", _id, generation, _frames.GetWidth(), _frames.GetHeight(), width, height);
            _frames.Close();
        }
    }

    void RemoteView::OnHostLost() {
        std::scoped_lock lock(_renderMutex);
        // The texture keeps the last frame until the new host paints.
        _frames.Close();
        _uploadedSeq = 0;
        _onHost      = false;
    }

    void RemoteView::OnHostReady() {
        // Views made after the host came up are on it already.
        if (!_onHost) {
            CreateOnHost();
        }
    }

    bool RemoteView::UploadFrame(Graphics::D3D8::Device *device) {
        // resize in flight: keep the last frame until the host paints at the current size
        if (!_frames.IsOpen() || _frames.GetWidth() != static_cast<uint32_t>(_width) || _frames.GetHeight() != static_cast<uint32_t>(_height)) {
            return _surface.HasContent(_width, _height);
        }

        bool fullUpload = false;
        if (!_surface.Prepare(device, _width, _height, _id, fullUpload)) {
            return false;
        }

        FrameReader::Access access;
        if (!_frames.Acquire(fullUpload ? 0 : _uploadedSeq, access)) {
            return _surface.HasContent(_width, _height);
        }
        const RECT region {static_cast<LONG>(access.dirty.x), static_cast<LONG>(access.dirty.y), static_cast<LONG>(access.dirty.x + access.dirty.width), static_cast<LONG>(access.dirty.y + access.dirty.height)};
        const bool uploaded = access.dirty.IsEmpty() || _surface.Upload(access.pixels, access.stride, _width, _height, region);
        _frames.Release();
        if (uploaded) {
            _uploadedSeq = access.seq;
        }
        return _surface.HasContent(_width, _height);
    }

    void RemoteView::Render() {
        if (!IsOnScreen() && !_offscreen) {
            return;
        }

        std::scoped_lock lock(_renderMutex);
        if (!_graphicsRenderer || _graphicsRenderer->GetBackendType() != Graphics::RendererBackend::BACKEND_D3D_8) {
            return;
        }
        auto *backend = _graphicsRenderer->GetD3D8Backend();
        if (!backend || !backend->IsDeviceReady()) {
            return;
        }
        auto *device = backend->GetDevice();

        if (!UploadFrame(device) || !IsOnScreen()) {
            return;
        }
        _surface.Draw(device, _x, _y, _width, _height);
    }

    bool RemoteView::SnapshotFrame(std::vector<uint8_t> &pixels, int &width, int &height) {
        std::scoped_lock lock(_renderMutex);
        FrameReader::Access access;
        if (!_frames.IsOpen() || !_frames.Acquire(0, access)) {
            return false;
        }
        width  = static_cast<int>(_frames.GetWidth());
        height = static_cast<int>(_frames.GetHeight());
        pixels.resize(static_cast<std::size_t>(access.stride) * static_cast<std::size_t>(height));
        std::memcpy(pixels.data(), access.pixels, pixels.size());
        _frames.Release();
        return true;
    }
} // namespace Framework::GUI::Remote
