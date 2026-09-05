/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "library.h"

#include "utils/string_utils.h"

#include <Windows.h>
#include <algorithm>
#include <filesystem>

namespace Framework::External::Rockstar {
    namespace {
        constexpr const wchar_t *kRegistryRoot = L"SOFTWARE\\Rockstar Games";
        constexpr const wchar_t *kLauncherKey  = L"Launcher";

        // 32-bit installer, so WOW6432Node first; the native view covers a host with no redirection
        constexpr REGSAM kRegistryViews[] = {KEY_WOW64_32KEY, KEY_WOW64_64KEY};

        std::wstring ReadStringValue(HKEY key, const wchar_t *name) {
            DWORD type = REG_SZ;
            DWORD size = 0;
            if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
                return {};
            }

            std::wstring value(size / sizeof(wchar_t) + 1, L'\0');
            if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &size) != ERROR_SUCCESS) {
                return {};
            }

            value.resize(wcslen(value.c_str()));
            return value;
        }

        std::string ReadTitleValue(HKEY root, const std::wstring &subKey, const wchar_t *name, REGSAM view) {
            HKEY key = nullptr;
            if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ | view, &key) != ERROR_SUCCESS) {
                return {};
            }

            const auto value = ReadStringValue(key, name);
            RegCloseKey(key);
            return Utils::StringUtils::WideToNormal(value);
        }

        bool HoldsExecutable(const std::string &installFolder, const std::string &exeFileName) {
            if (installFolder.empty() || exeFileName.empty()) {
                return false;
            }

            std::error_code ec;
            return std::filesystem::is_regular_file(std::filesystem::path(installFolder) / exeFileName, ec);
        }
    } // namespace

    std::string GetLauncherPath() {
        for (const auto view : kRegistryViews) {
            const auto path = ReadTitleValue(HKEY_LOCAL_MACHINE, std::wstring(kRegistryRoot) + L"\\" + kLauncherKey, L"InstallFolder", view);
            if (!path.empty()) {
                return path;
            }
        }
        return {};
    }

    std::vector<InstalledTitle> EnumerateInstalledTitles() {
        std::vector<InstalledTitle> titles;

        for (const auto view : kRegistryViews) {
            HKEY root = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kRegistryRoot, 0, KEY_READ | view, &root) != ERROR_SUCCESS) {
                continue;
            }

            for (DWORD index = 0;; ++index) {
                wchar_t name[256] = {};
                DWORD nameSize    = static_cast<DWORD>(std::size(name));
                if (RegEnumKeyExW(root, index, name, &nameSize, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                    break;
                }

                // "Launcher" and friends sit next to the titles but hold no game
                InstalledTitle title;
                title.titleKey      = Utils::StringUtils::WideToNormal(name);
                title.installFolder = ReadTitleValue(root, name, L"InstallFolder", view);
                if (title.installFolder.empty() || title.titleKey == Utils::StringUtils::WideToNormal(kLauncherKey)) {
                    continue;
                }

                title.version = ReadTitleValue(root, name, L"Version", view);
                titles.push_back(std::move(title));
            }

            RegCloseKey(root);

            if (!titles.empty()) {
                break;
            }
        }

        return titles;
    }

    InstalledTitle FindInstalledTitle(const std::string &exeFileName, const std::string &titleKey) {
        const auto titles = EnumerateInstalledTitles();

        if (!titleKey.empty()) {
            const auto match = std::ranges::find_if(titles, [&](const InstalledTitle &title) { return _stricmp(title.titleKey.c_str(), titleKey.c_str()) == 0; });
            return match != titles.end() ? *match : InstalledTitle {};
        }

        const auto match = std::ranges::find_if(titles, [&](const InstalledTitle &title) { return HoldsExecutable(title.installFolder, exeFileName); });
        return match != titles.end() ? *match : InstalledTitle {};
    }
} // namespace Framework::External::Rockstar
