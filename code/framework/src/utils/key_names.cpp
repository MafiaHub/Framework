/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// safe_win32 first: pulls in WinSock2 before Windows.h to avoid winsock1 conflicts.
#include "safe_win32.h"

#include "key_names.h"

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
                    t.emplace_back("f" + std::to_string(i), VK_F1 + (i - 1));
                }
                for (int i = 0; i <= 9; ++i) {
                    const int vk = VK_NUMPAD0 + i;
                    t.emplace_back("numpad" + std::to_string(i), vk);
                    t.emplace_back("num" + std::to_string(i), vk);
                }
                t.emplace_back("space", VK_SPACE);
                t.emplace_back("enter", VK_RETURN);
                t.emplace_back("return", VK_RETURN);
                t.emplace_back("escape", VK_ESCAPE);
                t.emplace_back("esc", VK_ESCAPE);
                t.emplace_back("tab", VK_TAB);
                t.emplace_back("backspace", VK_BACK);
                t.emplace_back("capslock", VK_CAPITAL);
                t.emplace_back("shift", VK_SHIFT);
                t.emplace_back("lshift", VK_LSHIFT);
                t.emplace_back("rshift", VK_RSHIFT);
                t.emplace_back("ctrl", VK_CONTROL);
                t.emplace_back("control", VK_CONTROL);
                t.emplace_back("lctrl", VK_LCONTROL);
                t.emplace_back("rctrl", VK_RCONTROL);
                t.emplace_back("alt", VK_MENU);
                t.emplace_back("lalt", VK_LMENU);
                t.emplace_back("ralt", VK_RMENU);
                t.emplace_back("up", VK_UP);
                t.emplace_back("down", VK_DOWN);
                t.emplace_back("left", VK_LEFT);
                t.emplace_back("right", VK_RIGHT);
                t.emplace_back("insert", VK_INSERT);
                t.emplace_back("delete", VK_DELETE);
                t.emplace_back("home", VK_HOME);
                t.emplace_back("end", VK_END);
                t.emplace_back("pageup", VK_PRIOR);
                t.emplace_back("pagedown", VK_NEXT);
                t.emplace_back("mouse1", VK_LBUTTON);
                t.emplace_back("mouse2", VK_RBUTTON);
                t.emplace_back("mouse3", VK_MBUTTON);
                t.emplace_back("mouse4", VK_XBUTTON1);
                t.emplace_back("mouse5", VK_XBUTTON2);
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
