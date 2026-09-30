/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <array>
#include <cstddef>

namespace Framework::Input {
    // Event state: clear edges after every consumer, never between consumers.
    template <std::size_t Size>
    class ButtonState final {
      public:
        bool IsDown(int index) const {
            return Valid(index) && _down[index];
        }
        bool IsUp(int index) const {
            return Valid(index) && !_down[index];
        }
        bool IsPressed(int index) const {
            return Valid(index) && _pressed[index];
        }
        bool IsReleased(int index) const {
            return Valid(index) && _released[index];
        }

        void Set(int index, bool down, bool recordEdge = true) {
            if (!Valid(index) || _down[index] == down) {
                return;
            }
            _down[index] = down;
            if (!recordEdge) {
                return;
            }
            if (down) {
                _pressed[index] = true;
            }
            else {
                _released[index] = true;
            }
        }

        void ClearEdges() {
            _pressed.fill(false);
            _released.fill(false);
        }

        void ReleaseAll() {
            for (std::size_t index = 0; index < Size; ++index) {
                Set(static_cast<int>(index), false);
            }
        }

      private:
        static bool Valid(int index) {
            return index >= 0 && static_cast<std::size_t>(index) < Size;
        }
        std::array<bool, Size> _down {};
        std::array<bool, Size> _pressed {};
        std::array<bool, Size> _released {};
    };

    // Sampled state: seed on acquisition/resume, so keys already held do not
    // become new presses. A frozen device must not manufacture releases.
    class KeySnapshot final {
      public:
        template <typename Reader>
        void Update(Reader &&read, bool available, bool stale = false) {
            _keys.ClearEdges();
            _available = available && !stale;
            if (!available) {
                _keys.ReleaseAll();
                _keys.ClearEdges();
                _resync = true;
                return;
            }
            if (stale) {
                _resync = true;
                return;
            }
            for (int key = 0; key < 256; ++key) {
                _keys.Set(key, read(key));
            }
            if (_resync) {
                _keys.ClearEdges();
            }
            _resync = false;
        }

        bool IsDown(int key) const {
            return _available && _keys.IsDown(key);
        }
        bool IsPressed(int key) const {
            return _available && _keys.IsPressed(key);
        }
        bool IsReleased(int key) const {
            return _available && _keys.IsReleased(key);
        }

      private:
        ButtonState<256> _keys;
        bool _resync    = true;
        bool _available = false;
    };
} // namespace Framework::Input
