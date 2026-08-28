/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/error.h>
#include <utils/lifecycle.h>
#include <utils/result.h>
#include <utils/safe_win32.h>

#include <atomic>
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
        std::atomic<bool> _cefInitialized = false;

        // Latched once the pump faults; stops further pumping and the CefShutdown.
        std::atomic<bool> _cefPumpFailed = false;

        // mutable so const readers can lock
        mutable std::recursive_mutex _renderMutex;

        std::vector<std::unique_ptr<View>> _views;

        // Retired views, held a few ticks before freeing so in-flight draw data
        // can stop referencing their textures. <view, age-in-ticks>
        std::vector<std::pair<std::unique_ptr<View>, int>> _dyingViews;

        std::unique_ptr<SystemClipboard> _clipboard;
        Graphics::Renderer *_graphicsRenderer {};
        HANDLE _cacheProfileLock = INVALID_HANDLE_VALUE;
        bool _gpuAccelerated = false;
        int _id              = 0;

        // Callers must hold _renderMutex
        std::vector<GUI::View *> GetViewsByZIndex() const;
        void RetireView(std::unique_ptr<View> view);

      public:
        Manager();
        ~Manager();

        [[nodiscard]] Utils::Result<void, Error> Init(const std::string &rootDir, ViewportConfiguration initialViewport, Graphics::Renderer *renderer, bool gpuAccelerated = false);
        void Shutdown() override;

        int CreateView(const std::string &url, int width, int height, int offsetX = 0, int offsetY = 0);
        bool DestroyView(int id);

        void CleanupViews();
        bool IsAnyViewFocused() const;
        bool IsAnyGCViewFocused() const;

        std::vector<GUI::View *> GetAllViews() const;
        std::vector<GUI::View *> GetGCViews() const;

        void Update() override;
        void Render();

        // Match the viewport and every fullscreen view to a new client size.
        void Resize(int width, int height);

        // Game-thread companion to Render(); must be called inside an ImGui frame.
        void SubmitImGuiDraws();

        void ProcessMouseEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const;
        void ProcessKeyboardEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const;

        void SetViewportConfiguration(const ViewportConfiguration &viewportConfiguration) {
            std::scoped_lock lock(_renderMutex);
            _viewportConfiguration = viewportConfiguration;
        }

        ViewportConfiguration GetViewportConfiguration() const {
            std::scoped_lock lock(_renderMutex);
            return _viewportConfiguration;
        }

        View *GetView(int id) const;

        void RegisterSchemeHandlerFactory(const std::string &schema, const std::string &domain, Framework::GUI::CEF::SchemaHandlerFactoryCallback callback);
    };
} // namespace Framework::GUI
