/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

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
