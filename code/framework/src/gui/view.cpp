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
    namespace {
        // CEF windowless (off-screen) render cap in FPS. Kept high so the browser paints at the
        // game's frame rate rather than CEF's 30 FPS default; external_begin_frame still drives the
        // actual cadence.
        constexpr int kWindowlessFrameRate = 240;
    } // namespace

    View::View(int id, Graphics::Renderer *graphicsRenderer, Manager *manager)
        : _id(id)
        , _graphicsRenderer(graphicsRenderer)
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

    Utils::Result<void, Framework::Error> View::Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated) {
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

        const auto forward = [this](const ViewEventData &data) {
            EmitViewEvent(data);
        };
        _loadHandler->SetViewEventCallback(forward);
        _displayHandler->SetViewEventCallback(forward);
        _lifeSpanHandler->SetViewEventCallback(forward);

        // Create CEF client
        _cefClient = new CEF::Client(_renderHandler, _lifeSpanHandler, _loadHandler, _displayHandler, _sdk.get());
        _cefClient->SetViewEventCallback(forward);

        // Configure windowless rendering
        CefWindowInfo windowInfo;
        windowInfo.SetAsWindowless(nullptr);
        windowInfo.shared_texture_enabled       = gpuAccelerated;
        windowInfo.external_begin_frame_enabled = true;

        CefBrowserSettings browserSettings;
        browserSettings.windowless_frame_rate = kWindowlessFrameRate;
        browserSettings.background_color      = CefColorSetARGB(0, 0, 0, 0);

        // Create the browser synchronously
        _browser = CefBrowserHost::CreateBrowserSync(windowInfo, _cefClient, url, browserSettings, nullptr, nullptr);
        if (!_browser) {
            Framework::Logging::GetLogger("Web")->error("Failed to create CEF browser");
            return Framework::Error {"Failed to create CEF browser"};
        }

        return {};
    }

    void View::Update() {
        if (!_browser || !_shouldDisplay) {
            return;
        }

        // Nothing else to update for CEF (the message loop is driven by Manager); the lock
        // synchronizes the update tick against a concurrent Render.
        std::scoped_lock lock(_renderMutex);
    }

    void View::RequestBeginFrame() {
        if (_browser) {
            _browser->GetHost()->SendExternalBeginFrame();
        }
    }

    void View::EmitViewEvent(const ViewEventData &data) {
        if (data.event == ViewEvent::Created) {
            _created = true;
        }

        // window object exists from main-frame load start; bind before anything talks to the page
        if (data.event == ViewEvent::LoadingStart && data.isMainFrame && _sdk && _browser) {
            (void)_sdk->Init(_browser);
        }

        if (_onViewEventCallback) {
            _onViewEventCallback(data);
        }
    }

    void View::LockToOrigin(const std::string &url) {
        if (!_lifeSpanHandler) {
            return;
        }
        std::string origin = CEF::LifeSpanHandler::OriginFromURL(url);
        if (origin.empty()) {
            // unparsable origin (data:, about:): lock everything out rather than open up
            Framework::Logging::GetLogger("Web")->warn("View {}: cannot derive origin from '{}', locking view down", _id, url);
            origin = "null";
        }

        if (_lifeSpanHandler->GetAllowedOrigin() == origin) {
            return;
        }
        _lifeSpanHandler->SetAllowedOrigin(origin);

        ViewEventData data;
        data.event  = ViewEvent::OriginChange;
        data.origin = std::move(origin);
        data.url    = url;
        EmitViewEvent(data);
    }

    void View::LoadURL(const std::string &url) {
        if (!_browser || !_browser->GetMainFrame()) {
            return;
        }
        if (_lifeSpanHandler && !_lifeSpanHandler->GetAllowedOrigin().empty()) {
            LockToOrigin(url);
        }
        _browser->GetMainFrame()->LoadURL(url);
    }

    void View::Resize(int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }

        // lock so the render thread sees a consistent _width/_height
        std::scoped_lock lock(_renderMutex);
        _width  = width;
        _height = height;
        if (_renderHandler) {
            _renderHandler->SetDimensions(width, height);
        }
        if (_browser) {
            _browser->GetHost()->WasResized();
        }
    }

    // Ctrl/Shift/Alt from the live keyboard state. Mouse messages only carry
    // Ctrl/Shift (there is no MK_ALT), so both input paths read modifiers here
    // uniformly instead of off wParam, otherwise DOM e.altKey never fires.
    static uint32_t CefKeyboardModifiers() {
        uint32_t mods = 0;
        if (GetKeyState(VK_CONTROL) & 0x8000) mods |= EVENTFLAG_CONTROL_DOWN;
        if (GetKeyState(VK_SHIFT) & 0x8000) mods |= EVENTFLAG_SHIFT_DOWN;
        if (GetKeyState(VK_MENU) & 0x8000) mods |= EVENTFLAG_ALT_DOWN;
        return mods;
    }

    // MK_* button state from a mouse message's wParam, as CEF event flags. Blink
    // sustains drags (scrollbar thumb, text selection) off the button flags
    // carried by the move events, so these must reflect the live state.
    static uint32_t CefMouseButtons(WPARAM wParam) {
        uint32_t mods = 0;
        if (wParam & MK_LBUTTON) mods |= EVENTFLAG_LEFT_MOUSE_BUTTON;
        if (wParam & MK_MBUTTON) mods |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
        if (wParam & MK_RBUTTON) mods |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
        return mods;
    }

    void View::ProcessMouseEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (!_browser || !_shouldDisplay || !_hasFocus) {
            return;
        }

        auto host = _browser->GetHost();

        // Handle mouse wheel separately. Unlike the client-relative messages below,
        // WM_MOUSEWHEEL reports the cursor in screen coordinates, and packs the
        // button/modifier state into the low word of wParam.
        if (msg == WM_MOUSEWHEEL) {
            POINT pt {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ::ScreenToClient(hWnd, &pt);
            CefMouseEvent cefEvent;
            cefEvent.x = pt.x - _x;
            cefEvent.y = pt.y - _y;
            cefEvent.modifiers = CefKeyboardModifiers() | CefMouseButtons(GET_KEYSTATE_WPARAM(wParam));
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            host->SendMouseWheelEvent(cefEvent, 0, delta);
            return;
        }

        CefMouseEvent cefEvent;
        cefEvent.x = GET_X_LPARAM(lParam) - _x;
        cefEvent.y = GET_Y_LPARAM(lParam) - _y;
        cefEvent.modifiers = CefKeyboardModifiers() | CefMouseButtons(wParam);

        switch (msg) {
        case WM_MOUSEMOVE: {
            _cursorPos = {cefEvent.x, cefEvent.y};
            host->SendMouseMoveEvent(cefEvent, false);
        } break;
        case WM_LBUTTONDOWN: {
            _isMouseDown = true;
            host->SendMouseClickEvent(cefEvent, MBT_LEFT, false, 1);
        } break;
        case WM_LBUTTONDBLCLK: {
            _isMouseDown = true;
            host->SendMouseClickEvent(cefEvent, MBT_LEFT, false, 2);
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
        case WM_SYSKEYDOWN:
            cefEvent.is_system_key = true;
            [[fallthrough]];
        case WM_KEYDOWN: {
            cefEvent.type             = KEYEVENT_RAWKEYDOWN;
            cefEvent.windows_key_code = static_cast<int>(wParam);
        } break;
        case WM_SYSKEYUP:
            cefEvent.is_system_key = true;
            [[fallthrough]];
        case WM_KEYUP: {
            cefEvent.type             = KEYEVENT_KEYUP;
            cefEvent.windows_key_code = static_cast<int>(wParam);
        } break;
        case WM_CHAR: {
            cefEvent.type = KEYEVENT_CHAR;

            if (wParam == VK_RETURN) {
                return;
            }

            // ANSI window: widen the codepage byte to UTF-16 for CEF.
            wchar_t wide = static_cast<wchar_t>(wParam);
            if (!IsWindowUnicode(hWnd)) {
                const HKL layout = GetKeyboardLayout(0);
                UINT codePage    = CP_ACP;
                GetLocaleInfoA(MAKELCID(HIWORD(layout), SORT_DEFAULT), LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, reinterpret_cast<LPSTR>(&codePage), sizeof(codePage));
                wide = 0;
                MultiByteToWideChar(codePage, MB_PRECOMPOSED, reinterpret_cast<const char *>(&wParam), 2, &wide, 1);
            }

            cefEvent.windows_key_code     = wide;
            cefEvent.character            = static_cast<char16_t>(wide);
            cefEvent.unmodified_character = static_cast<char16_t>(wide);
        } break;
        default:
            return;
        }

        cefEvent.native_key_code = static_cast<int>(lParam);
        cefEvent.modifiers       = CefKeyboardModifiers();

        _browser->GetHost()->SendKeyEvent(cefEvent);
    }

    void View::SetZIndex(int z) {
        _z = z;
    }
} // namespace Framework::GUI
