/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <input/input_keymap.h>
#include <utils/safe_win32.h>

namespace Framework::Input::PhysicalKeys {
    namespace detail {
        struct KeyPosition {
            UINT scanCode;
            UINT virtualKey;
        };

        // Standard PC scan-set-1 positions for the printable US keys. Keep
        // this independent of LoadKeyboardLayout: Wine may return the active
        // host layout even when a different reference layout is requested.
        inline constexpr KeyPosition kPositions[] {
            {0x02, '1'},
            {0x03, '2'},
            {0x04, '3'},
            {0x05, '4'},
            {0x06, '5'},
            {0x07, '6'},
            {0x08, '7'},
            {0x09, '8'},
            {0x0A, '9'},
            {0x0B, '0'},
            {0x0C, VK_OEM_MINUS},
            {0x0D, VK_OEM_PLUS},
            {0x10, 'Q'},
            {0x11, 'W'},
            {0x12, 'E'},
            {0x13, 'R'},
            {0x14, 'T'},
            {0x15, 'Y'},
            {0x16, 'U'},
            {0x17, 'I'},
            {0x18, 'O'},
            {0x19, 'P'},
            {0x1A, VK_OEM_4},
            {0x1B, VK_OEM_6},
            {0x1E, 'A'},
            {0x1F, 'S'},
            {0x20, 'D'},
            {0x21, 'F'},
            {0x22, 'G'},
            {0x23, 'H'},
            {0x24, 'J'},
            {0x25, 'K'},
            {0x26, 'L'},
            {0x27, VK_OEM_1},
            {0x28, VK_OEM_7},
            {0x29, VK_OEM_3},
            {0x2B, VK_OEM_5},
            {0x2C, 'Z'},
            {0x2D, 'X'},
            {0x2E, 'C'},
            {0x2F, 'V'},
            {0x30, 'B'},
            {0x31, 'N'},
            {0x32, 'M'},
            {0x33, VK_OEM_COMMA},
            {0x34, VK_OEM_PERIOD},
            {0x35, VK_OEM_2},
            {0x56, VK_OEM_102},
        };
    } // namespace detail

    inline bool IsLayoutDependent(UINT virtualKey) {
        for (const auto &position : detail::kPositions) {
            if (position.virtualKey == virtualKey) {
                return true;
            }
        }
        return false;
    }

    inline UINT ToScanCode(UINT virtualKey) {
        for (const auto &position : detail::kPositions) {
            if (position.virtualKey == virtualKey) {
                return position.scanCode;
            }
        }
        return MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX);
    }

    // Native device codes are fixed by the DirectInput SDK, not by an active
    // layout or MapVirtualKey's navigation/numpad interpretation.
    inline UINT ToDirectInputCode(UINT key) {
        for (const auto &position : detail::kPositions) {
            if (position.virtualKey == key)
                return position.scanCode;
        }
        switch (key) {
        case FW_KEY_ESCAPE: return 0x01;
        case FW_KEY_BACK: return 0x0E;
        case FW_KEY_TAB: return 0x0F;
        case FW_KEY_RETURN: return 0x1C;
        case FW_KEY_CONTROL:
        case FW_KEY_LCONTROL: return 0x1D;
        case FW_KEY_SHIFT:
        case FW_KEY_LSHIFT: return 0x2A;
        case FW_KEY_RSHIFT: return 0x36;
        case FW_KEY_MULTIPLY: return 0x37;
        case FW_KEY_MENU:
        case FW_KEY_LMENU: return 0x38;
        case FW_KEY_SPACE: return 0x39;
        case FW_KEY_CAPITAL: return 0x3A;
        case FW_KEY_F1: return 0x3B;
        case FW_KEY_F2: return 0x3C;
        case FW_KEY_F3: return 0x3D;
        case FW_KEY_F4: return 0x3E;
        case FW_KEY_F5: return 0x3F;
        case FW_KEY_F6: return 0x40;
        case FW_KEY_F7: return 0x41;
        case FW_KEY_F8: return 0x42;
        case FW_KEY_F9: return 0x43;
        case FW_KEY_F10: return 0x44;
        case FW_KEY_NUMLOCK: return 0x45;
        case FW_KEY_SCROLL: return 0x46;
        case FW_KEY_NUMPAD7: return 0x47;
        case FW_KEY_NUMPAD8: return 0x48;
        case FW_KEY_NUMPAD9: return 0x49;
        case FW_KEY_SUBTRACT: return 0x4A;
        case FW_KEY_NUMPAD4: return 0x4B;
        case FW_KEY_CLEAR:
        case FW_KEY_NUMPAD5: return 0x4C;
        case FW_KEY_NUMPAD6: return 0x4D;
        case FW_KEY_ADD: return 0x4E;
        case FW_KEY_NUMPAD1: return 0x4F;
        case FW_KEY_NUMPAD2: return 0x50;
        case FW_KEY_NUMPAD3: return 0x51;
        case FW_KEY_NUMPAD0: return 0x52;
        case FW_KEY_DECIMAL: return 0x53;
        case FW_KEY_F11: return 0x57;
        case FW_KEY_F12: return 0x58;
        case FW_KEY_F13: return 0x64;
        case FW_KEY_F14: return 0x65;
        case FW_KEY_F15: return 0x66;
        case FW_KEY_KANA: return 0x70;
        case FW_KEY_CONVERT: return 0x79;
        case FW_KEY_NONCONVERT: return 0x7B;
        case FW_KEY_OEM_NEC_EQUAL: return 0x8D;
        case FW_KEY_MEDIA_PREV_TRACK: return 0x90;
        case FW_KEY_KANJI: return 0x94;
        case FW_KEY_MEDIA_NEXT_TRACK: return 0x99;
        case FW_KEY_RCONTROL: return 0x9D;
        case FW_KEY_VOLUME_MUTE: return 0xA0;
        case FW_KEY_MEDIA_PLAY_PAUSE: return 0xA2;
        case FW_KEY_MEDIA_STOP: return 0xA4;
        case FW_KEY_VOLUME_DOWN: return 0xAE;
        case FW_KEY_VOLUME_UP: return 0xB0;
        case FW_KEY_BROWSER_HOME: return 0xB2;
        case FW_KEY_DIVIDE: return 0xB5;
        case FW_KEY_SNAPSHOT: return 0xB7;
        case FW_KEY_RMENU: return 0xB8;
        case FW_KEY_PAUSE: return 0xC5;
        case FW_KEY_HOME: return 0xC7;
        case FW_KEY_UP: return 0xC8;
        case FW_KEY_PRIOR: return 0xC9;
        case FW_KEY_LEFT: return 0xCB;
        case FW_KEY_RIGHT: return 0xCD;
        case FW_KEY_END: return 0xCF;
        case FW_KEY_DOWN: return 0xD0;
        case FW_KEY_NEXT: return 0xD1;
        case FW_KEY_INSERT: return 0xD2;
        case FW_KEY_DELETE: return 0xD3;
        case FW_KEY_LWIN: return 0xDB;
        case FW_KEY_RWIN: return 0xDC;
        case FW_KEY_APPS: return 0xDD;
        case FW_KEY_SLEEP: return 0xDF;
        case FW_KEY_BROWSER_SEARCH: return 0xE5;
        case FW_KEY_BROWSER_FAVORITES: return 0xE6;
        case FW_KEY_BROWSER_REFRESH: return 0xE7;
        case FW_KEY_BROWSER_STOP: return 0xE8;
        case FW_KEY_BROWSER_FORWARD: return 0xE9;
        case FW_KEY_BROWSER_BACK: return 0xEA;
        case FW_KEY_LAUNCH_MAIL: return 0xEC;
        case FW_KEY_LAUNCH_MEDIA_SELECT: return 0xED;
        default: return 0;
        }
    }

    inline UINT FromScanCode(UINT scanCode) {
        for (const auto &position : detail::kPositions) {
            if (position.scanCode == scanCode) {
                return position.virtualKey;
            }
        }
        return MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
    }

    // Preserve non-printing VK semantics (notably NumLock-dependent numpad
    // navigation and Pause/NumLock's shared scan code), and generic modifiers.
    inline UINT FromKeyMessage(WPARAM virtualKey, LPARAM flags) {
        UINT scanCode = (static_cast<ULONG_PTR>(flags) >> 16) & 0xFFU;
        if (scanCode == 0) {
            return static_cast<UINT>(virtualKey);
        }
        if ((flags & (1L << 24)) != 0) {
            scanCode |= 0xE000U;
        }
        const UINT physicalKey = FromScanCode(scanCode);
        return IsLayoutDependent(physicalKey) ? physicalKey : static_cast<UINT>(virtualKey);
    }

    // GetAsyncKeyState takes a VK in the foreground window's layout. Translate
    // the requested position through its scan code before polling that VK.
    inline UINT ToLayoutVirtualKey(UINT virtualKey, HKL layout) {
        if (!IsLayoutDependent(virtualKey)) {
            return virtualKey;
        }
        return MapVirtualKeyExW(ToScanCode(virtualKey), MAPVK_VSC_TO_VK_EX, layout);
    }
} // namespace Framework::Input::PhysicalKeys
