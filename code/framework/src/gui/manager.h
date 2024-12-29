/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include <memory>
#include <mutex>
#include <vector>

#include <AppCore/Platform.h>
#include <Ultralight/Ultralight.h>

#include "clipboard.h"
#include "view.h"
#include "graphics/renderer.h"

namespace Framework::GUI {
    struct ViewportConfiguration {
        int width;
        int height;
    };

    class Manager {
      private:
        ViewportConfiguration _viewportConfiguration;
        ultralight::RefPtr<ultralight::Renderer> _renderer;

        std::recursive_mutex _renderMutex;

        std::vector<std::unique_ptr<View>> _views;
        std::unique_ptr<SystemClipboard> _clipboard;
        Graphics::Renderer* _graphicsRenderer;

      public:
        Manager();
        ~Manager();

        bool Init(const std::string&, ViewportConfiguration, Graphics::Renderer*);

        int CreateView(std::string, int, int);
        bool DestroyView(int);

        void Update();
        void Render();

        void ProcessMouseEvent(HWND, UINT, WPARAM, LPARAM) const;
        void ProcessKeyboardEvent(HWND, UINT, WPARAM, LPARAM) const;

        void SetViewportConfiguration(const ViewportConfiguration &viewportConfiguration) {
            _viewportConfiguration = viewportConfiguration;
        }

        ViewportConfiguration GetViewportConfiguration() const {
            return _viewportConfiguration;
        }

        View *GetView(int id) const {
            return _views[id].get();
        }
    };
} // namespace Framework::GUI
