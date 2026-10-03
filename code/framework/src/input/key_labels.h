/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "physical_keys.h"

#include <iterator>
#include <string>

namespace Framework::Input::KeyLabels {
    // Display text only: never feed a label back into the physical-key API.
    // An explicit layout keeps this independent of the calling thread's layout.
    inline std::string Get(int key, HKL layout) {
        switch (key) {
        case FW_KEY_LBUTTON: return "Mouse 1";
        case FW_KEY_RBUTTON: return "Mouse 2";
        case FW_KEY_MBUTTON: return "Mouse 3";
        case FW_KEY_XBUTTON1: return "Mouse 4";
        case FW_KEY_XBUTTON2: return "Mouse 5";
        case FW_KEY_BACK: return "Backspace";
        case FW_KEY_TAB: return "Tab";
        case FW_KEY_RETURN: return "Enter";
        case FW_KEY_SHIFT: return "Shift";
        case FW_KEY_LSHIFT: return "Left Shift";
        case FW_KEY_RSHIFT: return "Right Shift";
        case FW_KEY_CONTROL: return "Ctrl";
        case FW_KEY_LCONTROL: return "Left Ctrl";
        case FW_KEY_RCONTROL: return "Right Ctrl";
        case FW_KEY_MENU: return "Alt";
        case FW_KEY_LMENU: return "Left Alt";
        case FW_KEY_RMENU: return "Right Alt";
        case FW_KEY_CAPITAL: return "Caps Lock";
        case FW_KEY_ESCAPE: return "Escape";
        case FW_KEY_SPACE: return "Space";
        case FW_KEY_PRIOR: return "Page Up";
        case FW_KEY_NEXT: return "Page Down";
        case FW_KEY_END: return "End";
        case FW_KEY_HOME: return "Home";
        case FW_KEY_LEFT: return "Left";
        case FW_KEY_UP: return "Up";
        case FW_KEY_RIGHT: return "Right";
        case FW_KEY_DOWN: return "Down";
        case FW_KEY_INSERT: return "Insert";
        case FW_KEY_DELETE: return "Delete";
        }
        if (key >= FW_KEY_F1 && key <= FW_KEY_F12) {
            return "F" + std::to_string(key - FW_KEY_F1 + 1);
        }
        if (key >= FW_KEY_NUMPAD0 && key <= FW_KEY_NUMPAD9) {
            return "Numpad " + std::to_string(key - FW_KEY_NUMPAD0);
        }
        if (!PhysicalKeys::IsLayoutDependent(key)) {
            return {};
        }

        const UINT virtualKey = PhysicalKeys::ToLayoutVirtualKey(key, layout);
        if (virtualKey == 0) {
            return {};
        }
        // Neutral modifiers keep labels stable while Shift/Caps Lock is held.
        // Bit 2 preserves the keyboard's dead-key state (Windows 10 1607+).
        // GetKeyNameText/MapVirtualKey(VK_TO_CHAR) return Latin A-Z even on
        // non-Latin layouts, so use the layout's actual Unicode translation.
        BYTE state[256] {};
        wchar_t text[32] {};
        const int translated = ToUnicodeEx(virtualKey, PhysicalKeys::ToScanCode(key), state, text, static_cast<int>(std::size(text)), 4, layout);
        const int length     = translated < 0 ? 1 : translated;
        if (length <= 0 || length > static_cast<int>(std::size(text)) || text[0] < L' ') {
            return {};
        }
        CharUpperBuffW(text, length);
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
        std::string label(bytes, '\0');
        if (bytes != 0) {
            WideCharToMultiByte(CP_UTF8, 0, text, length, label.data(), bytes, nullptr, nullptr);
        }
        return label;
    }
} // namespace Framework::Input::KeyLabels
