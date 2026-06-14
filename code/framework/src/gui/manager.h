/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <utils/lifecycle.h>
#include <utils/safe_win32.h>

#include <memory>
#include <mutex>
#include <vector>

#include "include/cef_app.h"

#include "cef/app.h"
#include "clipboard.h"
#include "graphics/renderer.h"
#include "view.h"

namespace Framework::GUI {
    struct ViewportConfiguration {
        int width;
        int height;
    };

    class Manager final : public Framework::Lifecycle {
      private:
        ViewportConfiguration _viewportConfiguration {};
        CefRefPtr<CEF::App> _cefApp;
        bool _cefInitialized = false;

        // Guards _views/_dyingViews across the game thread, the render thread
        // and reentrant message dispatch; mutable so const readers can lock
        mutable std::recursive_mutex _renderMutex;

        std::vector<std::unique_ptr<View>> _views;

        // Destroyed views are parked here for a few ticks before their GPU
        // resources are actually freed: draw data built on the game thread can
        // reference a view's texture for a couple of frames after removal
        std::vector<std::pair<std::unique_ptr<View>, int>> _dyingViews;

        std::unique_ptr<SystemClipboard> _clipboard;
        std::string _rootDir;
        Graphics::Renderer *_graphicsRenderer {};
        bool _gpuAccelerated = false;
        int _id              = 0;

        // Callers must hold _renderMutex
        std::vector<GUI::View *> GetViewsByZIndex() const;
        void RetireView(std::unique_ptr<View> view);

      public:
        Manager();
        ~Manager();

        [[nodiscard]] GUIError Init(const std::string &rootDir, ViewportConfiguration initialViewport, Graphics::Renderer *renderer, bool gpuAccelerated = false);
        void Shutdown() override;

        int CreateView(const std::string &url, int width, int height, int offsetX = 0, int offsetY = 0);
        bool DestroyView(int id);

        void CleanupViews();
        bool IsAnyViewFocused() const;
        bool IsAnyGCViewFocused() const;

        std::vector<GUI::View *> GetAllViews() const;
        std::vector<GUI::View *> GetGCViews() const;

        size_t GetDyingViewCount() const {
            return _dyingViews.size();
        }

        void Update() override;
        void Render();

        // Match the viewport (and every fullscreen view) to a new client size,
        // e.g. after a swapchain resize / fullscreen toggle.
        void Resize(int width, int height);

        // Game-thread companion to Render(): must be called inside an active
        // ImGui frame so D3D12 views can blit through the background draw list
        void SubmitImGuiDraws();

        void ProcessMouseEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const;
        void ProcessKeyboardEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const;

        void SetViewportConfiguration(const ViewportConfiguration &viewportConfiguration) {
            _viewportConfiguration = viewportConfiguration;
        }

        ViewportConfiguration GetViewportConfiguration() const {
            return _viewportConfiguration;
        }

        View *GetView(int id) const;

        void RegisterSchemeHandlerFactory(const std::string &schema, const std::string &domain, Framework::GUI::CEF::SchemaHandlerFactoryCallback callback);
    };
} // namespace Framework::GUI
