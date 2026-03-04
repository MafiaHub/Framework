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
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "errors.h"
#include "graphics/renderer.h"
#include "sdk.h"

#include "include/cef_browser.h"

#include "cef/client.h"
#include "cef/display_handler.h"
#include "cef/life_span_handler.h"
#include "cef/load_handler.h"
#include "cef/render_handler.h"

namespace Framework::GUI {
    using OnConsoleMessageCallback    = fu2::function<void(std::string, uint32_t, uint32_t, std::string)>;
    using OnDOMReadyCallback          = fu2::function<void(std::string, bool, std::string)>;
    using OnWindowObjectReadyCallback = fu2::function<void(std::string, bool, std::string)>;

    class Manager;

    class View {
      private:
        OnConsoleMessageCallback _onConsoleMessageCallback;
        OnDOMReadyCallback _onDOMReadyCallback;
        OnWindowObjectReadyCallback _onWindowObjectReadyCallback;

      protected:
        CefRefPtr<CefBrowser> _browser;
        CefRefPtr<CEF::Client> _cefClient;
        CefRefPtr<CEF::RenderHandler> _renderHandler;
        CefRefPtr<CEF::LifeSpanHandler> _lifeSpanHandler;
        CefRefPtr<CEF::LoadHandler> _loadHandler;
        CefRefPtr<CEF::DisplayHandler> _displayHandler;

        Graphics::Renderer *_graphicsRenderer = nullptr;
        Manager *_manager                     = nullptr;

        std::unique_ptr<SDK> _sdk;

        // CPU renderer fallback
        std::vector<uint8_t> _pixelData;

        bool _gpuAccelerated  = false;
        bool _hasFocus        = false;
        int _x;
        int _y;
        int _z;
        int _width;
        int _height;
        bool _shouldDisplay   = false;
        bool _garbageCollected = false;

        std::recursive_mutex _renderMutex;
        glm::vec2 _cursorPos {};
        bool _isMouseDown = false;

      public:
        View(Graphics::Renderer *graphicsRenderer, Manager *manager);
        virtual ~View();

        [[nodiscard]] virtual GUIError Init(std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated = false);

        virtual void Update();
        virtual void Render() = 0;

        void ProcessMouseEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
        void ProcessKeyboardEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        void Focus(bool enable) {
            _hasFocus = enable;
            if (_browser) {
                _browser->GetHost()->SetFocus(enable);
            }
        }

        bool HasFocus() const {
            return _hasFocus;
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

        inline void AddEventListener(const std::string &eventName, const EventCallbackProc &proc) {
            if (!_sdk) {
                return;
            }
            _sdk->AddEventListener(eventName, proc);
        }

        inline void RemoveEventListener(const std::string &eventName) {
            if (!_sdk) {
                return;
            }
            _sdk->RemoveEventListener(eventName);
        }

        inline void EvaluateScript(const std::string &script) {
            if (!_browser || !_browser->GetMainFrame()) {
                return;
            }
            _browser->GetMainFrame()->ExecuteJavaScript(script, "", 0);
        }

        CefRefPtr<CefBrowser> GetBrowser() const {
            return _browser;
        }

        inline GUI::SDK *GetSDK() {
            return _sdk.get();
        }

        CEF::RenderHandler *GetRenderHandler() const {
            return _renderHandler.get();
        }

        cef_cursor_type_t GetCursorType() const {
            if (_displayHandler) {
                return _displayHandler->GetCursorType();
            }
            return CT_POINTER;
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
