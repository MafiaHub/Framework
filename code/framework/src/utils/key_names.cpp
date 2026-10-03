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
#include <input/key_labels.h>
#endif

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

namespace Framework::Utils::KeyNames {
    namespace {
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
        DWORD process      = 0;
        const DWORD thread = GetWindowThreadProcessId(GetForegroundWindow(), &process);
        // Match input polling while the game is foregrounded. In the background,
        // use our own thread's layout rather than another application's layout.
        const HKL layout        = GetKeyboardLayout(process == GetCurrentProcessId() ? thread : 0);
        const std::string label = Input::KeyLabels::Get(virtualKey, layout);
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
