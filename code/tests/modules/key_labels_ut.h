/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <input/key_labels.h>
#include <utils/key_names.h>

#include <algorithm>
#include <vector>

namespace KeyLabelTests {
    struct Layout {
        HKL original = GetKeyboardLayout(0);
        HKL value;
        bool alreadyLoaded;

        explicit Layout(const wchar_t *name) {
            std::vector<HKL> before(GetKeyboardLayoutList(0, nullptr));
            GetKeyboardLayoutList(static_cast<int>(before.size()), before.data());
            value         = LoadKeyboardLayoutW(name, KLF_NOTELLSHELL);
            alreadyLoaded = std::find(before.begin(), before.end(), value) != before.end();
        }

        ~Layout() {
            ActivateKeyboardLayout(original, 0);
            if (value && !alreadyLoaded) {
                UnloadKeyboardLayout(value);
            }
        }
    };
} // namespace KeyLabelTests

MODULE(key_labels, {
    IT("labels the fixed Y/Z positions using QWERTY and QWERTZ", {
        KeyLabelTests::Layout english(L"00000409");
        KeyLabelTests::Layout german(L"00000407");
        EQUALS(LOWORD(reinterpret_cast<ULONG_PTR>(english.value)), 0x0409);
        EQUALS(LOWORD(reinterpret_cast<ULONG_PTR>(german.value)), 0x0407);
        EQUALS(Framework::Input::KeyLabels::Get('Y', english.value), std::string("Y"));
        EQUALS(Framework::Input::KeyLabels::Get('Z', english.value), std::string("Z"));
        EQUALS(Framework::Input::KeyLabels::Get('Y', german.value), std::string("Z"));
        EQUALS(Framework::Input::KeyLabels::Get('Z', german.value), std::string("Y"));
        EQUALS(Framework::Input::PhysicalKeys::ToLayoutVirtualKey('Y', german.value), 'Z');
        EQUALS(Framework::Input::PhysicalKeys::ToScanCode('Y'), 0x15U);
    });

    IT("returns AZERTY and Cyrillic labels as UTF-8", {
        KeyLabelTests::Layout french(L"0000040C");
        KeyLabelTests::Layout russian(L"00000419");
        EQUALS(LOWORD(reinterpret_cast<ULONG_PTR>(french.value)), 0x040C);
        EQUALS(LOWORD(reinterpret_cast<ULONG_PTR>(russian.value)), 0x0419);
        EQUALS(Framework::Input::KeyLabels::Get('W', french.value), std::string("Z"));
        EQUALS(Framework::Input::KeyLabels::Get('A', french.value), std::string("Q"));
        EQUALS(Framework::Input::KeyLabels::Get('1', french.value), std::string("&"));
        EQUALS(Framework::Input::KeyLabels::Get('W', russian.value), std::string("\xD0\xA6")); // Ц
        EQUALS(Framework::Input::KeyLabels::Get('Y', russian.value), std::string("\xD0\x9D")); // Н
    });

    IT("keeps mouse, numpad, navigation and modifier labels distinct", {
        const HKL layout = GetKeyboardLayout(0);
        EQUALS(Framework::Input::KeyLabels::Get(FW_KEY_LBUTTON, layout), std::string("Mouse 1"));
        EQUALS(Framework::Input::KeyLabels::Get(FW_KEY_MBUTTON, layout), std::string("Mouse 3"));
        EQUALS(Framework::Input::KeyLabels::Get(FW_KEY_XBUTTON2, layout), std::string("Mouse 5"));
        EQUALS(Framework::Input::KeyLabels::Get(FW_KEY_NUMPAD1, layout), std::string("Numpad 1"));
        EQUALS(Framework::Input::KeyLabels::Get(FW_KEY_END, layout), std::string("End"));
        EQUALS(Framework::Input::KeyLabels::Get(FW_KEY_SHIFT, layout), std::string("Shift"));
        EQUALS(Framework::Input::KeyLabels::Get(FW_KEY_LSHIFT, layout), std::string("Left Shift"));
        EQUALS(Framework::Input::KeyLabels::Get(FW_KEY_RCONTROL, layout), std::string("Right Ctrl"));
        EQUALS(Framework::Input::KeyLabels::Get(FW_KEY_F12, layout), std::string("F12"));
    });

    IT("labels every script key and resolves aliases without changing identifiers", {
        for (const int key : Framework::Utils::KeyNames::All()) {
            EQUALS(Framework::Utils::KeyNames::GetLabel(key).empty(), false);
        }
        const int key = Framework::Utils::KeyNames::ToVirtualKey("ReTuRn");
        EQUALS(Framework::Utils::KeyNames::GetLabel(key), std::string("Enter"));
        EQUALS(Framework::Utils::KeyNames::FromVirtualKey(key), std::string("enter"));
        EQUALS(Framework::Utils::KeyNames::GetLabel(-1).empty(), true);
        EQUALS(Framework::Utils::KeyNames::GetLabel(256).empty(), true);
    });

    IT("reads layout changes without changing the selected layout", {
        KeyLabelTests::Layout english(L"00000409");
        KeyLabelTests::Layout german(L"00000407");
        ActivateKeyboardLayout(english.value, 0);
        EQUALS(Framework::Utils::KeyNames::GetLabel('Y'), std::string("Y"));
        EQUALS(GetKeyboardLayout(0), english.value);
        ActivateKeyboardLayout(german.value, 0);
        EQUALS(Framework::Utils::KeyNames::GetLabel('Y'), std::string("Z"));
        EQUALS(GetKeyboardLayout(0), german.value);
    });

    IT("does not leave a dead key pending after looking up its label", {
        KeyLabelTests::Layout german(L"00000407");
        EQUALS(LOWORD(reinterpret_cast<ULONG_PTR>(german.value)), 0x0407);
        EQUALS(Framework::Input::KeyLabels::Get(FW_KEY_OEM_PLUS, german.value), std::string("\xC2\xB4")); // acute accent
        BYTE state[256] {};
        wchar_t text[8] {};
        const int count = ToUnicodeEx('E', 0x12, state, text, 8, 4, german.value);
        EQUALS(count, 1);
        EQUALS(text[0], L'e');
    });

    IT("does not consume an accent the player has already typed", {
        KeyLabelTests::Layout german(L"00000407");
        EQUALS(LOWORD(reinterpret_cast<ULONG_PTR>(german.value)), 0x0407);
        BYTE state[256] {};
        wchar_t text[8] {};
        const UINT accent       = Framework::Input::PhysicalKeys::ToLayoutVirtualKey(FW_KEY_OEM_PLUS, german.value);
        const int dead          = ToUnicodeEx(accent, 0x0D, state, text, 8, 0, german.value);
        const std::string label = Framework::Input::KeyLabels::Get('E', german.value);
        // Consume the composition before asserting, including on a failed test.
        const int count = ToUnicodeEx('E', 0x12, state, text, 8, 0, german.value);
        EQUALS(dead, -1);
        EQUALS(label.empty(), false);
        EQUALS(count, 1);
        EQUALS(text[0], L'\u00E9');
    });
});
