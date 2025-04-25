#include "view.h"
#include "logging/logger.h"
#include <Windows.h>
#include <WindowsX.h>

#include <unordered_map>

namespace Framework::GUI {
    View::View(ultralight::RefPtr<ultralight::Renderer> renderer, Graphics::Renderer *graphicsRenderer, Manager *manager): _renderer(renderer), _graphicsRenderer(graphicsRenderer), _manager(manager), _width(0), _height(0), _x(0), _y(0), _z(0) {
        _sdk = new SDK;
        _isMouseDown = false;
    }

    View::~View() {
        // Leak the reference since Ultralight asserts on shutdown if it still holds onto some GPU renderer related data
        // TODO: re-visit later since closing views might actually leak them!
        _internalView.LeakRef();

        if (_sdk) {
            _sdk->Shutdown();
            delete _sdk;
        }
    }

    bool View::Init(std::string &path, int width, int height, int offset_x, int offset_y, bool gpu_accelerated) {
        // Initialize a view configuration
        ultralight::ViewConfig config;
        config.is_accelerated = gpu_accelerated;
        config.is_transparent = true;
        config.initial_focus  = false;
        _gpuAccelerated       = gpu_accelerated;

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

        // Store the offsets
        _x = offset_x;
        _y = offset_y;
        return true;
    }

    void View::Update() {
        if (!_internalView || !_shouldDisplay) {
            return;
        }

        std::lock_guard lock(_renderMutex);

        // Update the view content (CPU renderer)
        if (!_gpuAccelerated) {
            auto surface = dynamic_cast<ultralight::BitmapSurface *>(_internalView->surface());
            void *pixels = surface->LockPixels();
            int size     = surface->size();
            if (_pixelData.size() != size) {
                _pixelData.clear();
                _pixelData.resize(size);
            }
            std::memcpy(_pixelData.data(), pixels, size);
            surface->UnlockPixels();
        }
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

        ev.x = GET_X_LPARAM(lParam) - _x;
        ev.y = GET_Y_LPARAM(lParam) - _y;

        switch (msg) {
        case WM_MOUSEMOVE: {
            ev.type = ultralight::MouseEvent::kType_MouseMoved;
            ev.button = _isMouseDown ? ultralight::MouseEvent::kButton_Left : ultralight::MouseEvent::kButton_None;
            _cursorPos = {ev.x, ev.y};
        } break;
        case WM_LBUTTONDOWN: {
            ev.type = ultralight::MouseEvent::kType_MouseDown;
            ev.button = ultralight::MouseEvent::kButton_Left;
            _isMouseDown = true;
        } break;
        case WM_LBUTTONUP: {
            ev.type = ultralight::MouseEvent::kType_MouseUp;
            ev.button = ultralight::MouseEvent::kButton_Left;
            _isMouseDown = false;
        } break;
        case WM_RBUTTONDOWN: {
            ev.type = ultralight::MouseEvent::kType_MouseDown;
            ev.button = ultralight::MouseEvent::kButton_Right;
        } break;
        case WM_RBUTTONUP: {
            ev.type = ultralight::MouseEvent::kType_MouseUp;
            ev.button = ultralight::MouseEvent::kButton_Right;
        } break;
        case WM_MBUTTONDOWN: {
            ev.type = ultralight::MouseEvent::kType_MouseDown;
            ev.button = ultralight::MouseEvent::kButton_Middle;
        } break;
        case WM_MBUTTONUP: {
            ev.type = ultralight::MouseEvent::kType_MouseUp;
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
            ev.type = ultralight::KeyEvent::kType_Char;
            
            // Handle UTF-16 input (including emojis which are surrogate pairs)
            static std::wstring utf16Buffer;
            wchar_t currentChar = static_cast<wchar_t>(wParam);
            
            // Check if this is a surrogate pair
            if (IS_HIGH_SURROGATE(currentChar)) {
                // Start collecting a new surrogate pair
                utf16Buffer = currentChar;
                return; // Wait for the low surrogate
            }
            else if (IS_LOW_SURROGATE(currentChar) && !utf16Buffer.empty()) {
                // Complete the surrogate pair
                utf16Buffer += currentChar;
                
                // Convert from UTF-16 to UTF-8
                int utf8Length = WideCharToMultiByte(CP_UTF8, 0, utf16Buffer.c_str(), static_cast<int>(utf16Buffer.length()), nullptr, 0, nullptr, nullptr);
                
                std::string utf8Text(utf8Length, 0);
                WideCharToMultiByte(CP_UTF8, 0, utf16Buffer.c_str(), static_cast<int>(utf16Buffer.length()), &utf8Text[0], utf8Length, nullptr, nullptr);

                ev.text = utf8Text.c_str();
                ev.unmodified_text = ev.text;
                utf16Buffer.clear();
            }
            else {
                // Regular UTF-16 character (not a surrogate pair)
                utf16Buffer.clear();
                
                // Convert from UTF-16 to UTF-8
                wchar_t wc = static_cast<wchar_t>(wParam);
                int utf8Length = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, nullptr, 0, nullptr, nullptr);
                
                std::string utf8Text(utf8Length, 0);
                WideCharToMultiByte(CP_UTF8, 0, &wc, 1, &utf8Text[0], utf8Length, nullptr, nullptr);
                
                ev.text = utf8Text.c_str();
                ev.unmodified_text = ev.text;
            }

            // Make sure that pressing enter does not trigger this event
            if (wParam == 13) {
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
        const auto msg = std::string(message.message().utf8().data());
        const auto lineNumber = message.line_number();
        const auto columnNumber = message.column_number();
        const auto sourceUrl = std::string(message.source_id().utf8().data());
        if (_onConsoleMessageCallback) {
            _onConsoleMessageCallback(msg, lineNumber, columnNumber, sourceUrl);
        }
    }

    void View::OnDOMReady(ultralight::View *caller, uint64_t frame_id, bool is_main_frame, const ultralight::String &url) {
        if (_onDOMReadyCallback) {
            _onDOMReadyCallback(frame_id, is_main_frame, std::string(url.utf8().data()));
        }
    }

    void View::OnWindowObjectReady(ultralight::View *caller, uint64_t frame_id, bool is_main_frame, const ultralight::String &url) {
        _sdk->Init(caller);
        if (_onWindowObjectReadyCallback) {
            _onWindowObjectReadyCallback(frame_id, is_main_frame, std::string(url.utf8().data()));
        }
    }

    void View::OnChangeCursor(ultralight::View *caller, ultralight::Cursor cursor) {
        _cursor = cursor;
    }

    void View::SetZIndex(int z) {
        _z = z;
    }

} // namespace Framework::GUI
