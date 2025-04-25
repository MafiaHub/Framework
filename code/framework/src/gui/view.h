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

#include <vector>

namespace Framework::GUI {
    using OnConsoleMessageCallback     = fu2::function<void(std::string,uint32_t,uint32_t,std::string)>;
    using OnDOMReadyCallback           = fu2::function<void(uint64_t, bool, std::string)>;
    using OnWindowObjectReadyCallback  = fu2::function<void(uint64_t, bool, std::string)>;

    class Manager;

    class View: public ultralight::ViewListener, ultralight::LoadListener {
      private:
        OnConsoleMessageCallback _onConsoleMessageCallback;
        OnDOMReadyCallback _onDOMReadyCallback;
        OnWindowObjectReadyCallback _onWindowObjectReadyCallback;

      protected:
        ultralight::RefPtr<ultralight::Renderer> _renderer;
        ultralight::RefPtr<ultralight::View> _internalView = nullptr;
        Graphics::Renderer *_graphicsRenderer              = nullptr;
        Manager *_manager                                  = nullptr;

        SDK *_sdk = nullptr;

        // CPU renderer
        std::vector<uint8_t> _pixelData;

        bool _gpuAccelerated = false;
        int _x;
        int _y;
        int _z;
        int _width;
        int _height;
        bool _shouldDisplay = false;
        bool _garbageCollected = false;

        std::recursive_mutex _renderMutex;
        ultralight::Cursor _cursor = ultralight::kCursor_Pointer;
        glm::vec2 _cursorPos {};
        bool _isMouseDown = false;

      protected:
        void OnAddConsoleMessage(ultralight::View *caller, const ultralight::ConsoleMessage &message) override;
        void OnDOMReady(ultralight::View *, uint64_t, bool, const ultralight::String &) override;
        void OnWindowObjectReady(ultralight::View *, uint64_t, bool, const ultralight::String &) override;
        void OnChangeCursor(ultralight::View *caller, ultralight::Cursor cursor) override;

      public:
        View(ultralight::RefPtr<ultralight::Renderer>, Graphics::Renderer*, Manager*);
        virtual ~View();

        virtual bool Init(std::string &, int, int, int, int, bool gpu_accelerated = false);

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
            }
            else {
                _internalView->Unfocus();
            }
        }

        bool HasFocus() const {
            return _internalView->HasFocus();
        }

        void Display(bool enable) {
            _shouldDisplay = enable;
        }

        bool ShouldDisplay() const {
            return _shouldDisplay;
        }

        void SetPosition(int x, int y) {
            _x = x;
            _y = y;
        }

        glm::vec2 GetPosition() const {
            return {_x, _y};
        }

        void SetZIndex(int z);

        int GetZIndex() const {
            return _z;
        }

        void SetGarbageCollected(bool garbageCollected) {
            _garbageCollected = garbageCollected;
        }

        bool IsGarbageCollected() const {
            return _garbageCollected;
        }

        ultralight::Cursor GetCursor() const {
            return _cursor;
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

        inline std::string EvaluateScript(const std::string& script) {
            if (!_internalView) {
                return "";
            }

            return _internalView->EvaluateScript(ultralight::String(script.c_str())).utf8().data();
        }

        inline ultralight::View *GetInternalView() {
            return _internalView.get();
        }

        inline GUI::SDK *GetSDK() {
            return _sdk;
        }

        inline void SetOnConsoleMessageCallback(OnConsoleMessageCallback proc) {
            _onConsoleMessageCallback = std::move(proc);
        }

        inline void SetOnDOMReadyCallback(OnDOMReadyCallback proc) {
            _onDOMReadyCallback = std::move(proc);
        }

        inline void SetOnWindowObjectReadyCallback(OnWindowObjectReadyCallback proc) {
            _onWindowObjectReadyCallback = std::move(proc);
        }
    };
} // namespace Framework::GUI
