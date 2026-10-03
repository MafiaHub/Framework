/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <input/input_keymap.h>

#include "key_names.h"

#ifdef _WIN32
#include <input/physical_keys.h>
#include <iterator>
#endif

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

namespace Framework::Utils::KeyNames {
    namespace {
#ifdef _WIN32
        std::string GetWindowsLabel(int key) {
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
            if (!Input::PhysicalKeys::IsLayoutDependent(key)) {
                return {};
            }

            DWORD process      = 0;
            const DWORD thread = GetWindowThreadProcessId(GetForegroundWindow(), &process);
            // Match input polling in the foreground. Otherwise use our own
            // thread's layout, not another application's layout.
            const HKL layout      = GetKeyboardLayout(process == GetCurrentProcessId() ? thread : 0);
            const UINT virtualKey = Input::PhysicalKeys::ToLayoutVirtualKey(key, layout);
            if (virtualKey == 0) {
                return {};
            }
            // Neutral modifiers keep labels stable while Shift/Caps Lock is held.
            // Bit 2 preserves the keyboard's dead-key state (Windows 10 1607+).
            // GetKeyNameText/MapVirtualKey(VK_TO_CHAR) return Latin A-Z even on
            // non-Latin layouts, so use the layout's actual Unicode translation.
            BYTE state[256] {};
            wchar_t text[32] {};
            const int translated = ToUnicodeEx(virtualKey, Input::PhysicalKeys::ToScanCode(key), state, text, static_cast<int>(std::size(text)), 4, layout);
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
#endif

        std::string ToLower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        // Aliases follow the name they alias, so the reverse map keeps the first spelling.
        const std::vector<std::pair<std::string, int>> &Table() {
            static const std::vector<std::pair<std::string, int>> table = [] {
                std::vector<std::pair<std::string, int>> t;
                for (char c = 'a'; c <= 'z'; ++c) {
                    t.emplace_back(std::string(1, c), 'A' + (c - 'a'));
                }
                for (char c = '0'; c <= '9'; ++c) {
                    t.emplace_back(std::string(1, c), c);
                }
                for (int i = 1; i <= 12; ++i) {
                    t.emplace_back("f" + std::to_string(i), FW_KEY_F1 + (i - 1));
                }
                for (int i = 0; i <= 9; ++i) {
                    const int vk = FW_KEY_NUMPAD0 + i;
                    t.emplace_back("numpad" + std::to_string(i), vk);
                    t.emplace_back("num" + std::to_string(i), vk);
                }
                t.emplace_back("space", FW_KEY_SPACE);
                t.emplace_back("enter", FW_KEY_RETURN);
                t.emplace_back("return", FW_KEY_RETURN);
                t.emplace_back("escape", FW_KEY_ESCAPE);
                t.emplace_back("esc", FW_KEY_ESCAPE);
                t.emplace_back("tab", FW_KEY_TAB);
                t.emplace_back("backspace", FW_KEY_BACK);
                t.emplace_back("capslock", FW_KEY_CAPITAL);
                t.emplace_back("shift", FW_KEY_SHIFT);
                t.emplace_back("lshift", FW_KEY_LSHIFT);
                t.emplace_back("rshift", FW_KEY_RSHIFT);
                t.emplace_back("ctrl", FW_KEY_CONTROL);
                t.emplace_back("control", FW_KEY_CONTROL);
                t.emplace_back("lctrl", FW_KEY_LCONTROL);
                t.emplace_back("rctrl", FW_KEY_RCONTROL);
                t.emplace_back("alt", FW_KEY_MENU);
                t.emplace_back("lalt", FW_KEY_LMENU);
                t.emplace_back("ralt", FW_KEY_RMENU);
                t.emplace_back("up", FW_KEY_UP);
                t.emplace_back("down", FW_KEY_DOWN);
                t.emplace_back("left", FW_KEY_LEFT);
                t.emplace_back("right", FW_KEY_RIGHT);
                t.emplace_back("insert", FW_KEY_INSERT);
                t.emplace_back("delete", FW_KEY_DELETE);
                t.emplace_back("home", FW_KEY_HOME);
                t.emplace_back("end", FW_KEY_END);
                t.emplace_back("pageup", FW_KEY_PRIOR);
                t.emplace_back("pagedown", FW_KEY_NEXT);
                t.emplace_back("mouse1", FW_KEY_LBUTTON);
                t.emplace_back("mouse2", FW_KEY_RBUTTON);
                t.emplace_back("mouse3", FW_KEY_MBUTTON);
                t.emplace_back("mouse4", FW_KEY_XBUTTON1);
                t.emplace_back("mouse5", FW_KEY_XBUTTON2);
                return t;
            }();
            return table;
        }

        const std::unordered_map<std::string, int> &Forward() {
            static const std::unordered_map<std::string, int> map = [] {
                std::unordered_map<std::string, int> m;
                for (const auto &[name, vk] : Table()) {
                    m.emplace(name, vk);
                }
                return m;
            }();
            return map;
        }

        const std::unordered_map<int, std::string> &Reverse() {
            static const std::unordered_map<int, std::string> map = [] {
                std::unordered_map<int, std::string> m;
                for (const auto &[name, vk] : Table()) {
                    m.emplace(vk, name);
                }
                return m;
            }();
            return map;
        }
    } // namespace

    int ToVirtualKey(const std::string &name) {
        const auto &map = Forward();
        const auto it   = map.find(ToLower(name));
        return it == map.end() ? -1 : it->second;
    }

    std::string FromVirtualKey(int virtualKey) {
        const auto &map = Reverse();
        const auto it   = map.find(virtualKey);
        return it == map.end() ? std::string() : it->second;
    }

    std::string GetLabel(int virtualKey) {
        std::string fallback = FromVirtualKey(virtualKey);
        if (fallback.empty()) {
            return {};
        }
#ifdef _WIN32
        const std::string label = GetWindowsLabel(virtualKey);
        if (!label.empty()) {
            return label;
        }
#endif
        std::transform(fallback.begin(), fallback.end(), fallback.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        return fallback;
    }

    const std::vector<int> &All() {
        static const std::vector<int> keys = [] {
            std::vector<int> out;
            out.reserve(Reverse().size());
            for (const auto &[vk, name] : Reverse()) {
                out.push_back(vk);
            }
            return out;
        }();
        return keys;
    }
} // namespace Framework::Utils::KeyNames
