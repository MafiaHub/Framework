/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// A stand-in for the manager of the web views, for FrameworkClientScriptingTests only. See view.h next to
// this file, which says why it exists and why it must stay in that one binary.

#include "view.h"

#include <utils/lifecycle.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Framework::GUI {
    struct ViewportConfiguration {
        int width;
        int height;
    };

    class Manager final: public Framework::Lifecycle {
      public:
        // What a test decides: the size of the client area, and whether a view can be made.
        ViewportConfiguration viewport {1920, 1080};
        bool failCreate = false;
        // The ids of the views that were destroyed, in order.
        std::vector<int> destroyed;

        // Not initialized until a test says so, as the real one before its Init.
        void Start() {
            _initialized = true;
        }

        // As the real one: 0x0 fills the viewport and follows it; -1 when no view can be made.
        int CreateView(const std::string &url, int width, int height, int offsetX = 0, int offsetY = 0) {
            if (!_initialized || failCreate) {
                return -1;
            }
            auto view        = std::make_unique<View>();
            view->id         = ++_id;
            view->url        = url;
            view->autoResize = width == 0 && height == 0;
            view->width      = width == 0 ? viewport.width : width;
            view->height     = height == 0 ? viewport.height : height;
            view->x          = offsetX;
            view->y          = offsetY;
            _views.push_back(std::move(view));
            return _id;
        }

        bool DestroyView(int id) {
            const auto it = Find(id);
            if (it == _views.end()) {
                return false;
            }
            _views.erase(it);
            destroyed.push_back(id);
            return true;
        }

        View *GetView(int id) const {
            const auto it = Find(id);
            return it != _views.end() ? it->get() : nullptr;
        }

        ViewportConfiguration GetViewportConfiguration() const {
            return viewport;
        }

        size_t ViewCount() const {
            return _views.size();
        }

      private:
        std::vector<std::unique_ptr<View>>::const_iterator Find(int id) const {
            return std::find_if(_views.begin(), _views.end(), [id](const std::unique_ptr<View> &view) {
                return view->id == id;
            });
        }

        std::vector<std::unique_ptr<View>> _views;
        int _id = 0;
    };
} // namespace Framework::GUI
