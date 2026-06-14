/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <utils/error.h>
#include <utils/lifecycle.h>
#include <utils/result.h>
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

        std::recursive_mutex _renderMutex;

        std::vector<std::unique_ptr<View>> _views;
        std::unique_ptr<SystemClipboard> _clipboard;
        Graphics::Renderer *_graphicsRenderer {};
        bool _gpuAccelerated = false;
        int _id              = 0;

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
