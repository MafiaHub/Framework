/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include "frame_buffer.h"
#include "remote_session.h"

#include "gui/backend/d3d8_view_surface.h"
#include "gui/view.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Framework::GUI::Remote {
    // A View whose browser lives in the CEF host process. Keeps the state a page needs on the
    // client - URL, origin lock, size, focus - and replays it when a restarted host has to create
    // the browser again. Copies the frames the host publishes into the same managed D3D8 texture
    // ViewD3D8 uses. Never touches libcef: in remote mode the client does not load it at all.
    class RemoteView final: public View {
      private:
        RemoteSession *_session;

        // What a restarted host navigates to: the last URL loaded, and the URL the origin lock
        // came from (LoadURL re-locks a locked view, so it follows navigation).
        std::string _url;
        std::string _lockedUrl;

        // Created on the current host. Cleared when that host dies.
        bool _onHost                  = false;
        cef_cursor_type_t _cursorType = CT_POINTER;

        // Under _renderMutex: the render thread reads frames while the game thread swaps surfaces.
        FrameReader _frames;
        uint32_t _uploadedSeq = 0;
        D3D8ViewSurface _surface;

        template <class T>
        void Post(T message) {
            if (HasBrowser()) {
                message.viewId = _id;
                _session->Send(message);
            }
        }

        void CreateOnHost();
        bool UploadFrame(Graphics::D3D8::Device *device);

      public:
        RemoteView(int id, Graphics::Renderer *graphicsRenderer, Manager *manager, RemoteSession *session);
        ~RemoteView() override;

        [[nodiscard]] Utils::Result<void, Framework::Error> Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated = false) override;

        void Render() override;

        [[nodiscard]] void *GetNativeTexture() const override {
            return _surface.GetTexture();
        }

        bool HasBrowser() const override;
        void RequestBeginFrame() override;
        void CheckNavigationStarted() override {}

        void SendMouseMove(const CefMouseEvent &event, bool mouseLeave) override;
        void SendMouseClick(const CefMouseEvent &event, cef_mouse_button_type_t button, bool mouseUp, int clickCount) override;
        void SendMouseWheel(const CefMouseEvent &event, int deltaX, int deltaY) override;
        void SendKeyEvent(const CefKeyEvent &event) override;
        void ImeSetComposition(const std::wstring &text, int selectionStart, int selectionEnd) override;
        void ImeCommitText(const std::wstring &text) override;
        void ImeFinishComposingText(bool keepSelection) override;

        void Focus(bool enable) override;
        void SetAudioMuted(bool muted) override;
        void Resize(int width, int height) override;
        void LockToOrigin(const std::string &url) override;
        void LoadURL(const std::string &url) override;
        void EvaluateScript(const std::string &script) override;

        cef_cursor_type_t GetCursorType() const override {
            return _cursorType;
        }

        // From the host, through the Manager, on the thread that updates it.
        void OnHostEvent(const ViewEventData &data);
        void OnHostPageEvent(const std::string &name, const std::string &payload);
        void OnHostSurface(uint32_t generation, int width, int height);
        void OnHostLost();
        void OnHostReady();

        // A copy of the newest frame, whole, for a caller with no device to composite on.
        bool SnapshotFrame(std::vector<uint8_t> &pixels, int &width, int &height);
    };
} // namespace Framework::GUI::Remote
