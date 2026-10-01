#pragma once

#include "button_state.h"
#include "input.h"

#include <array>

namespace Framework::Input {
    // Process-local virtual devices. Cursor operations never reach the OS.
    class VirtualInput final: public IInput {
      public:
        void Apply(const std::array<bool, 256> &keys, int x = 0, int y = 0) {
            // Preserve every edge until the owning frame calls Update().
            for (int key = 0; key < 256; ++key) _keys.Set(key, keys[key]);
            _x = x;
            _y = y;
        }
        void Update() override {
            _keys.ClearEdges();
        }
        void SetMousePosition(int x, int y) override {
            _x = x;
            _y = y;
        }
        void GetMousePosition(int &x, int &y) const override {
            x = _x;
            y = _y;
        }
        void SetMouseVisible(bool value) override {
            _visible = value;
        }
        bool IsMouseVisible() const override {
            return _visible;
        }
        void SetMouseLocked(bool value) override {
            _mouseLocked = value;
        }
        bool IsMouseLocked() const override {
            return _mouseLocked;
        }
        void SetInputLocked(bool value) override {
            _locked = value;
        }
        bool IsInputLocked() const override {
            return _locked;
        }
        bool IsAvailable() const override {
            return true;
        }
        bool IsKeyDown(int key) const override {
            return _keys.IsDown(key);
        }
        bool IsKeyUp(int key) const override {
            return key >= 0 && key < 256 && !_keys.IsDown(key);
        }
        bool IsKeyPressed(int key) const override {
            return _keys.IsPressed(key);
        }
        bool IsKeyReleased(int key) const override {
            return _keys.IsReleased(key);
        }
        bool IsMouseButtonDown(int button) const override {
            return ValidButton(button) && IsKeyDown(kButtons[button]);
        }
        bool IsMouseButtonUp(int button) const override {
            return ValidButton(button) && IsKeyUp(kButtons[button]);
        }
        bool IsMouseButtonPressed(int button) const override {
            return ValidButton(button) && IsKeyPressed(kButtons[button]);
        }
        bool IsMouseButtonReleased(int button) const override {
            return ValidButton(button) && IsKeyReleased(kButtons[button]);
        }

      private:
        static bool ValidButton(int button) {
            return button >= 0 && button < 5;
        }
        static constexpr int kButtons[] {FW_KEY_LBUTTON, FW_KEY_RBUTTON, FW_KEY_MBUTTON, FW_KEY_XBUTTON1, FW_KEY_XBUTTON2};
        ButtonState<256> _keys;
        int _x = 0, _y = 0;
        bool _visible = false, _mouseLocked = false, _locked = false;
    };
} // namespace Framework::Input
