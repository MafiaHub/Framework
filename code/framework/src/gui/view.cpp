#include "view.h"
#include "logging/logger.h"

#include <unordered_map>

namespace Framework::GUI {
    View::View(ultralight::RefPtr<ultralight::Renderer> renderer, Graphics::Renderer *graphicsRenderer): _renderer(renderer), _graphicsRenderer(graphicsRenderer), _pixelData(nullptr), _width(0), _height(0) {
        _sdk = new SDK;
    }

    View::~View() {
        if (_sdk) {
            _sdk->Shutdown();
            delete _sdk;
        }
    }

    bool View::Init(std::string &path, int width, int height) {
        // Initialize a view configuration
        ultralight::ViewConfig config;
        config.is_accelerated = false;
        config.is_transparent = true;
        config.initial_focus  = false;

        // Initialize the internal view
        _internalView = _renderer->CreateView(width, height, config, nullptr);
        if (!_internalView || !_internalView.get()) {
            return false;
        }

        // Bind the listeners to the internal view
        _internalView->set_view_listener(this);
        _internalView->set_load_listener(this);

        // Load the initial URL
        _internalView->LoadURL(path.c_str());

        // Store the width/height
        _width  = width;
        _height = height;
        return true;
    }

    void View::Update() {
        if (!_internalView || !_shouldDisplay) {
            return;
        }

        std::lock_guard lock(_renderMutex);

        // Update the view content
        auto surface = (ultralight::BitmapSurface *)_internalView->surface();
        void *pixels = surface->LockPixels();
        int size     = surface->size(); // TODO: calc from res
        // TODO: Realloc if size changes
        if (!_pixelData) {
            _pixelData = new uint8_t[size];
        }
        memcpy(_pixelData, pixels, size);
        surface->UnlockPixels();
    }

    void View::ProcessMouseEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (!_internalView || !_shouldDisplay) {
            return;
        }

        if (!_internalView->HasFocus()) {
            return;
        }

        // Handle the mouse wheel event as separate from the other mouse events
        if (msg == WM_MOUSEWHEEL) {
            ultralight::ScrollEvent ev;
            ev.type    = ultralight::ScrollEvent::kType_ScrollByPixel;
            ev.delta_x = 0;
            ev.delta_y = GET_WHEEL_DELTA_WPARAM(wParam) * 0.8;
            _internalView->FireScrollEvent(ev);
            return;
        }

        // Handle other classic mouse events
        ultralight::MouseEvent ev;
        ev.x = LOWORD(lParam); // TODO: revamp this because it fails on multiple monitors setups
        ev.y = HIWORD(lParam); // TODO: revamp this because it fails on multiple monitors setups
        switch (msg) {
        case WM_MOUSEMOVE: {
            ev.type    = ultralight::MouseEvent::kType_MouseMoved;
            ev.button  = ultralight::MouseEvent::kButton_None;
            _cursorPos = {ev.x, ev.y};
        } break;
        case WM_LBUTTONDOWN: {
            ev.type   = ultralight::MouseEvent::kType_MouseDown;
            ev.button = ultralight::MouseEvent::kButton_Left;
        } break;
        case WM_LBUTTONUP: {
            ev.type   = ultralight::MouseEvent::kType_MouseUp;
            ev.button = ultralight::MouseEvent::kButton_Left;
        } break;
        case WM_RBUTTONDOWN: {
            ev.type   = ultralight::MouseEvent::kType_MouseDown;
            ev.button = ultralight::MouseEvent::kButton_Right;
        } break;
        case WM_RBUTTONUP: {
            ev.type   = ultralight::MouseEvent::kType_MouseUp;
            ev.button = ultralight::MouseEvent::kButton_Right;
        } break;
        case WM_MBUTTONDOWN: {
            ev.type   = ultralight::MouseEvent::kType_MouseDown;
            ev.button = ultralight::MouseEvent::kButton_Middle;
        } break;
        case WM_MBUTTONUP: {
            ev.type   = ultralight::MouseEvent::kType_MouseUp;
            ev.button = ultralight::MouseEvent::kButton_Middle;
        } break;
        }

        _internalView->FireMouseEvent(ev);
    }

    void View::ProcessKeyboardEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (!_internalView || !_shouldDisplay) {
            return;
        }

        if (!_internalView->HasFocus()) {
            return;
        }

        ultralight::KeyEvent ev;
        switch (msg) {
        case WM_KEYDOWN: {
            ev.type = ultralight::KeyEvent::kType_RawKeyDown;
        } break;
        case WM_KEYUP: {
            ev.type = ultralight::KeyEvent::kType_KeyUp;
        } break;
        case WM_CHAR: {
            char key[2]        = {(char)wParam, 0};
            ev.type            = ultralight::KeyEvent::kType_Char;
            ev.text            = key;
            ev.unmodified_text = ev.text;

            // Make sure that pressing enter does not trigger this event
            if (key[0] == 13) {
                return;
            }
        } break;
        }

        ev.virtual_key_code = wParam;
        ev.native_key_code  = lParam;

        const bool ctrlPressed  = GetKeyState(VK_CONTROL) & 0x8000;
        const bool shiftPressed = GetKeyState(VK_SHIFT) & 0x8000;
        const bool altPressed   = GetKeyState(VK_MENU) & 0x8000;
        ev.modifiers            = (ctrlPressed ? ultralight::KeyEvent::kMod_CtrlKey : 0) | (shiftPressed ? ultralight::KeyEvent::kMod_ShiftKey : 0) | (altPressed ? ultralight::KeyEvent::kMod_AltKey : 0);

        ultralight::GetKeyIdentifierFromVirtualKeyCode(ev.virtual_key_code, ev.key_identifier);
        _internalView->FireKeyEvent(ev);
    }

    void View::OnAddConsoleMessage(ultralight::View *caller, const ultralight::ConsoleMessage &message) {
        Framework::Logging::GetLogger("Web")->debug("Console message: {}:{}:{}:{}", message.message().utf8().data(), message.line_number(), message.column_number(), message.source_id().utf8().data());
    }

    void View::OnDOMReady(ultralight::View *caller, uint64_t frame_id, bool is_main_frame, const ultralight::String &url) {
        Framework::Logging::GetLogger("Web")->debug("DOM ready");
    }

    void View::OnWindowObjectReady(ultralight::View *caller, uint64_t frame_id, bool is_main_frame, const ultralight::String &url) {
        Framework::Logging::GetLogger("Web")->debug("Window object ready");

        // Bind the SDK
        _sdk->Init(caller);
    }

    void View::OnChangeCursor(ultralight::View *caller, ultralight::Cursor cursor) {
        _cursor = cursor;
    }
} // namespace Framework::GUI
