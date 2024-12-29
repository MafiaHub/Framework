/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include <d3d11.h>
#include <function2.hpp>
#include <map>
#include <mutex>
#include <string>

#include <Ultralight/Ultralight.h>

#include <glm/glm.hpp>

#include "graphics/renderer.h"
#include "sdk.h"

namespace Framework::GUI {

    class View
        : public ultralight::ViewListener
        , ultralight::LoadListener {
      protected:
        ultralight::RefPtr<ultralight::Renderer> _renderer;
        ultralight::RefPtr<ultralight::View> _internalView = nullptr;
        Graphics::Renderer *_graphicsRenderer              = nullptr;

        SDK *_sdk = nullptr;

        uint8_t *_pixelData;
        int _width;
        int _height;
        bool _shouldDisplay = false;

        std::recursive_mutex _renderMutex;
        ultralight::Cursor _cursor = ultralight::kCursor_Pointer;
        glm::vec2 _cursorPos {};

      protected:
        void OnAddConsoleMessage(ultralight::View *caller, const ultralight::ConsoleMessage &message) override;
        void OnDOMReady(ultralight::View *, uint64_t, bool, const ultralight::String &) override;
        void OnWindowObjectReady(ultralight::View *, uint64_t, bool, const ultralight::String &) override;
        void OnChangeCursor(ultralight::View *caller, ultralight::Cursor cursor) override;

      public:
        View(ultralight::RefPtr<ultralight::Renderer>, Graphics::Renderer*);
        virtual ~View();

        virtual bool Init(std::string &, int, int);

        virtual void Update();
        virtual void Render() = 0;

        void ProcessMouseEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
        void ProcessKeyboardEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        void Focus(bool enable) {
            if (!_internalView) {
                return;
            }

            if (enable) {
                _internalView->Focus();
                ShowCursor(true);
            }
            else {
                _internalView->Unfocus();
                ShowCursor(false);
            }
        }

        void Display(bool enable) {
            _shouldDisplay = enable;
        }

        inline void AddEventListener(std::string eventName, const EventCallbackProc &proc) {
            if (!_sdk) {
                return;
            }

            _sdk->AddEventListener(eventName, proc);
        }

        inline void RemoveEventListener(std::string eventName) {
            if (!_sdk) {
                return;
            }

            _sdk->RemoveEventListener(eventName);
        }
    };
} // namespace Framework::GUI
