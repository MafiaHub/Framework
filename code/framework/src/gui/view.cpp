/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "view.h"
#include "logging/logger.h"

#include <Windows.h>
#include <WindowsX.h>

#include "graphics/backend/d3d11.h"

namespace Framework::GUI {
    View::View(Graphics::Renderer *graphicsRenderer, Manager *manager)
        : _graphicsRenderer(graphicsRenderer)
        , _manager(manager)
        , _width(0)
        , _height(0)
        , _x(0)
        , _y(0)
        , _z(0) {
        _sdk         = std::make_unique<SDK>();
        _isMouseDown = false;
    }

    View::~View() {
        if (_browser) {
            _browser->GetHost()->CloseBrowser(true);
            _browser = nullptr;
        }

        if (_sdk) {
            _sdk->Shutdown();
        }
    }

    GUIError View::Init(std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated) {
        _gpuAccelerated = gpuAccelerated;
        _width          = width;
        _height         = height;
        _x              = offsetX;
        _y              = offsetY;

        // Create CEF handlers
        _renderHandler   = new CEF::RenderHandler();
        _lifeSpanHandler = new CEF::LifeSpanHandler();
        _loadHandler     = new CEF::LoadHandler();
        _displayHandler  = new CEF::DisplayHandler();

        _renderHandler->SetDimensions(width, height);

        // Wire up D3D11 device for shared texture support
        if (_graphicsRenderer && _graphicsRenderer->GetBackendType() == Graphics::RendererBackend::BACKEND_D3D_11) {
            auto *backend = _graphicsRenderer->GetD3D11Backend();
            if (backend) {
                _renderHandler->SetD3D11Device(backend->GetDevice());
            }
        }

        // Wire up callbacks through handlers
        _loadHandler->SetOnDOMReadyCallback([this](const std::string &frameId, bool isMainFrame, const std::string &frameUrl) {
            if (_onDOMReadyCallback) {
                _onDOMReadyCallback(frameId, isMainFrame, frameUrl);
            }
        });
        _loadHandler->SetOnWindowObjectReadyCallback([this](const std::string &frameId, bool isMainFrame, const std::string &frameUrl) {
            // Initialize SDK when window object is ready
            if (_sdk && _browser) {
                (void)_sdk->Init(_browser);
            }
            if (_onWindowObjectReadyCallback) {
                _onWindowObjectReadyCallback(frameId, isMainFrame, frameUrl);
            }
        });
        _displayHandler->SetOnConsoleMessageCallback([this](const std::string &msg, uint32_t line, uint32_t col, const std::string &source) {
            if (_onConsoleMessageCallback) {
                _onConsoleMessageCallback(msg, line, col, source);
            }
        });

        // Create CEF client
        _cefClient = new CEF::Client(_renderHandler, _lifeSpanHandler, _loadHandler, _displayHandler, _sdk.get());

        // Configure windowless rendering
        CefWindowInfo windowInfo;
        windowInfo.SetAsWindowless(nullptr);
        windowInfo.shared_texture_enabled = gpuAccelerated;

        CefBrowserSettings browserSettings;
        browserSettings.windowless_frame_rate = 60;
        browserSettings.background_color      = CefColorSetARGB(0, 0, 0, 0);

        // Create the browser synchronously
        _browser = CefBrowserHost::CreateBrowserSync(windowInfo, _cefClient, url, browserSettings, nullptr, nullptr);
        if (!_browser) {
            Framework::Logging::GetLogger("Web")->error("Failed to create CEF browser");
            return GUIError::GUI_VIEW_INIT_FAILED;
        }

        return GUIError::GUI_NONE;
    }

    void View::Update() {
        // Nothing to update at the base level for CEF; message loop is driven by Manager
    }

    void View::ProcessMouseEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (!_browser || !_shouldDisplay || !_hasFocus) {
            return;
        }

        auto host = _browser->GetHost();

        // Handle mouse wheel separately
        if (msg == WM_MOUSEWHEEL) {
            CefMouseEvent cefEvent;
            cefEvent.x = GET_X_LPARAM(lParam) - _x;
            cefEvent.y = GET_Y_LPARAM(lParam) - _y;
            cefEvent.modifiers = 0;
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            host->SendMouseWheelEvent(cefEvent, 0, delta);
            return;
        }

        CefMouseEvent cefEvent;
        cefEvent.x = GET_X_LPARAM(lParam) - _x;
        cefEvent.y = GET_Y_LPARAM(lParam) - _y;
        cefEvent.modifiers = 0;

        switch (msg) {
        case WM_MOUSEMOVE: {
            _cursorPos = {cefEvent.x, cefEvent.y};
            host->SendMouseMoveEvent(cefEvent, false);
        } break;
        case WM_LBUTTONDOWN: {
            _isMouseDown = true;
            host->SendMouseClickEvent(cefEvent, MBT_LEFT, false, 1);
        } break;
        case WM_LBUTTONUP: {
            _isMouseDown = false;
            host->SendMouseClickEvent(cefEvent, MBT_LEFT, true, 1);
        } break;
        case WM_RBUTTONDOWN: {
            host->SendMouseClickEvent(cefEvent, MBT_RIGHT, false, 1);
        } break;
        case WM_RBUTTONUP: {
            host->SendMouseClickEvent(cefEvent, MBT_RIGHT, true, 1);
        } break;
        case WM_MBUTTONDOWN: {
            host->SendMouseClickEvent(cefEvent, MBT_MIDDLE, false, 1);
        } break;
        case WM_MBUTTONUP: {
            host->SendMouseClickEvent(cefEvent, MBT_MIDDLE, true, 1);
        } break;
        }
    }

    void View::ProcessKeyboardEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (!_browser || !_shouldDisplay || !_hasFocus) {
            return;
        }

        CefKeyEvent cefEvent;

        switch (msg) {
        case WM_KEYDOWN: {
            cefEvent.type = KEYEVENT_RAWKEYDOWN;
        } break;
        case WM_KEYUP: {
            cefEvent.type = KEYEVENT_KEYUP;
        } break;
        case WM_CHAR: {
            cefEvent.type = KEYEVENT_CHAR;

            // Make sure that pressing enter does not trigger this event
            if (wParam == 13) {
                return;
            }
        } break;
        default:
            return;
        }

        cefEvent.windows_key_code = static_cast<int>(wParam);
        cefEvent.native_key_code  = static_cast<int>(lParam);

        const bool ctrlPressed  = GetKeyState(VK_CONTROL) & 0x8000;
        const bool shiftPressed = GetKeyState(VK_SHIFT) & 0x8000;
        const bool altPressed   = GetKeyState(VK_MENU) & 0x8000;
        cefEvent.modifiers      = (ctrlPressed ? EVENTFLAG_CONTROL_DOWN : 0) | (shiftPressed ? EVENTFLAG_SHIFT_DOWN : 0) | (altPressed ? EVENTFLAG_ALT_DOWN : 0);

        _browser->GetHost()->SendKeyEvent(cefEvent);
    }

    void View::SetZIndex(int z) {
        _z = z;
    }
} // namespace Framework::GUI
