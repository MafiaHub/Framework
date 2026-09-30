/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <input/physical_keys.h>

#include <initializer_list>

MODULE(physical_keys, {
    IT("uses physical W and T positions independently of the message virtual key", {
        // A layout may assign a different VK to a physical key. The adapter
        // must use the scan code for both down and up, whatever that VK is.
        for (UINT virtualKey = 0; virtualKey < 256; ++virtualKey) {
            EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(virtualKey, 0x11L << 16), 'W');
            EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(virtualKey, 0x14L << 16), 'T');
        }
    });

    IT("polls the same positions through real English, Russian and AZERTY layouts", {
        const HKL original = GetKeyboardLayout(0);
        struct LayoutRestore {
            HKL original;

            void Restore() const {
                ActivateKeyboardLayout(original, 0);
            }

            ~LayoutRestore() {
                Restore();
            }
        } layoutRestore {original};
        const HKL english = LoadKeyboardLayoutW(L"00000409", KLF_NOTELLSHELL);
        const HKL russian = LoadKeyboardLayoutW(L"00000419", KLF_NOTELLSHELL);
        const HKL french  = LoadKeyboardLayoutW(L"0000040C", KLF_NOTELLSHELL);
        // Headless Wine can return the host layout for every request. Do not
        // call that a successful multi-layout test; run this case on Windows.
        if (LOWORD(reinterpret_cast<ULONG_PTR>(english)) != 0x0409 || LOWORD(reinterpret_cast<ULONG_PTR>(russian)) != 0x0419 || LOWORD(reinterpret_cast<ULONG_PTR>(french)) != 0x040C) {
            printf("real layout coverage unavailable: English/Russian/French layouts were not loaded\n");
            SKIP();
        }
        for (HKL layout : {english, russian, french}) {
            EQUALS(Framework::Input::PhysicalKeys::ToLayoutVirtualKey('W', layout), MapVirtualKeyExW(0x11, MAPVK_VSC_TO_VK_EX, layout));
            EQUALS(Framework::Input::PhysicalKeys::ToLayoutVirtualKey('T', layout), MapVirtualKeyExW(0x14, MAPVK_VSC_TO_VK_EX, layout));
            // A layout-aware IInput provider converts a layout VK back into
            // a DirectInput scan code. Preserve that existing API contract.
            EQUALS(MapVirtualKeyExW(Framework::Input::PhysicalKeys::ToLayoutVirtualKey('W', layout), MAPVK_VK_TO_VSC_EX, layout), 0x11U);
            EQUALS(MapVirtualKeyExW(Framework::Input::PhysicalKeys::ToLayoutVirtualKey('T', layout), MAPVK_VK_TO_VSC_EX, layout), 0x14U);
        }
        EQUALS(Framework::Input::PhysicalKeys::ToLayoutVirtualKey('W', french), 'Z');
        layoutRestore.Restore();
        EQUALS(GetKeyboardLayout(0), original);
    });

    IT("adapts a layout-aware device provider without changing its native key API", {
        const HKL layout = GetKeyboardLayout(0);
        for (UINT key : {'W', 'T', 'A', 'Z', '1', '9'}) {
            const UINT layoutKey = Framework::Input::PhysicalKeys::ToLayoutVirtualKey(key, layout);
            // Match GameInput::MapKey: it uses the current thread's layout.
            EQUALS(MapVirtualKeyW(layoutKey, MAPVK_VK_TO_VSC_EX), Framework::Input::PhysicalKeys::ToScanCode(key));
        }
    });

    IT("releases the same key after a layout change", {
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage('W', 0x11L << 16), 'W');
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage('Z', (0x11L << 16) | (1ULL << 30) | (1ULL << 31)), 'W');
        EQUALS(Framework::Input::PhysicalKeys::ToScanCode('W'), 0x11U);
        EQUALS(Framework::Input::PhysicalKeys::ToScanCode('T'), 0x14U);
    });

    IT("round-trips every letter, digit and punctuation position", {
        for (UINT key = 'A'; key <= 'Z'; ++key) {
            const UINT scan = Framework::Input::PhysicalKeys::ToScanCode(key);
            NEQUALS(scan, 0U);
            EQUALS(Framework::Input::PhysicalKeys::FromScanCode(scan), key);
        }
        for (UINT key = '0'; key <= '9'; ++key) {
            EQUALS(Framework::Input::PhysicalKeys::FromScanCode(Framework::Input::PhysicalKeys::ToScanCode(key)), key);
        }
        for (UINT key : {VK_OEM_1, VK_OEM_PLUS, VK_OEM_COMMA, VK_OEM_MINUS, VK_OEM_PERIOD, VK_OEM_2, VK_OEM_3, VK_OEM_4, VK_OEM_5, VK_OEM_6, VK_OEM_7, VK_OEM_102}) {
            EQUALS(Framework::Input::PhysicalKeys::FromScanCode(Framework::Input::PhysicalKeys::ToScanCode(key)), key);
        }
        EQUALS(Framework::Input::PhysicalKeys::FromScanCode(0x10), 'Q');
        EQUALS(Framework::Input::PhysicalKeys::FromScanCode(0x1E), 'A');
        EQUALS(Framework::Input::PhysicalKeys::FromScanCode(0x2C), 'Z');
    });

    IT("normalizes digit and punctuation positions without changing extended keys", {
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(VK_OEM_1, 0x02L << 16), '1');
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(VK_OEM_4, 0x27L << 16), VK_OEM_1);
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(VK_DELETE, (0x53L << 16) | (1L << 24)), VK_DELETE);
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(VK_DECIMAL, 0x53L << 16), VK_DECIMAL);
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(VK_DELETE, 0x53L << 16), VK_DELETE);
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(VK_PAUSE, 0x45L << 16), VK_PAUSE);
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(VK_NUMLOCK, (0x45L << 16) | (1L << 24)), VK_NUMLOCK);
    });

    IT("preserves modifiers, mouse and function keys and scanless events", {
        const HKL layout = GetKeyboardLayout(0);
        for (UINT key : {VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU, VK_LBUTTON, VK_XBUTTON2, VK_F5, VK_LEFT, VK_NUMPAD1}) {
            EQUALS(Framework::Input::PhysicalKeys::ToLayoutVirtualKey(key, layout), key);
        }
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(VK_SHIFT, 0x36L << 16), VK_SHIFT);
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(VK_CONTROL, (0x1DL << 16) | (1L << 24)), VK_CONTROL);
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(VK_MENU, (0x38L << 16) | (1L << 24)), VK_MENU);
        EQUALS(Framework::Input::PhysicalKeys::FromKeyMessage(VK_F5, 0), VK_F5);
    });
});
