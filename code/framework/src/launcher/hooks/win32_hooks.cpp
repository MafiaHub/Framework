/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "win32_hooks.h"

#include "utils/string_utils.h"

#include <Windows.h>
#include <Psapi.h>

namespace Framework::Launcher::Hooks {
    // Singleton hook context
    static HookContext gHookContext;

    HookContext &GetHookContext() {
        return gHookContext;
    }

    // Default entry point for the client DLL
    using ClientEntryPoint = void (*)(const wchar_t *projectPath);

    void WINAPI GetStartupInfoW_Stub(LPSTARTUPINFOW lpStartupInfo) {
        InitialiseClientDLL();
        return GetStartupInfoW(lpStartupInfo);
    }

    void WINAPI GetStartupInfoA_Stub(LPSTARTUPINFOA lpStartupInfo) {
        InitialiseClientDLL();
        return GetStartupInfoA(lpStartupInfo);
    }

    LPWSTR WINAPI GetCommandLineW_Stub() {
        if (!gHookContext.config->loadClientManually) {
            InitialiseClientDLL();
        }

        static wchar_t buffer[MAX_PATH] = {0};
        wcscpy_s(buffer, MAX_PATH, GetCommandLineW());
        wcscat_s(buffer, MAX_PATH, gHookContext.config->additionalLaunchArguments.c_str());

        return buffer;
    }

    LPSTR WINAPI GetCommandLineA_Stub() {
        if (!gHookContext.config->loadClientManually) {
            InitialiseClientDLL();
        }
        const auto args = Framework::Utils::StringUtils::WideToNormal(gHookContext.config->additionalLaunchArguments);

        static char buffer[MAX_PATH] = {0};
        strcpy_s(buffer, MAX_PATH, GetCommandLineA());
        strcat_s(buffer, MAX_PATH, args.c_str());

        return buffer;
    }

    DWORD WINAPI GetModuleFileNameA_Hook(HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
        if (!hModule || hModule == GetModuleHandle(nullptr)) {
            const auto gamePath = Framework::Utils::StringUtils::WideToNormal(gHookContext.imagePath);
            strcpy_s(lpFilename, nSize, gamePath.c_str());

            return (DWORD)gamePath.size();
        }

        return GetModuleFileNameA(hModule, lpFilename, nSize);
    }

    DWORD WINAPI GetModuleFileNameExA_Hook(HANDLE hProcess, HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
        if (!hModule || hModule == GetModuleHandle(nullptr)) {
            const auto gamePath = Framework::Utils::StringUtils::WideToNormal(gHookContext.imagePath);
            strcpy_s(lpFilename, nSize, gamePath.c_str());

            return (DWORD)gamePath.size();
        }

        return GetModuleFileNameExA(hProcess, hModule, lpFilename, nSize);
    }

    DWORD WINAPI GetModuleFileNameW_Hook(HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
        if (!hModule || hModule == GetModuleHandle(nullptr)) {
            wcscpy_s(lpFilename, nSize, gHookContext.imagePath);

            return (DWORD)wcslen(gHookContext.imagePath);
        }
        const auto len = GetModuleFileNameW(hModule, lpFilename, nSize);
        return len;
    }

    DWORD WINAPI GetModuleFileNameExW_Hook(HANDLE hProcess, HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
        if (!hModule || hModule == GetModuleHandle(nullptr)) {
            wcscpy_s(lpFilename, nSize, gHookContext.imagePath);

            return (DWORD)wcslen(gHookContext.imagePath);
        }

        return GetModuleFileNameExW(hProcess, hModule, lpFilename, nSize);
    }

    HMODULE WINAPI GetModuleHandleW_Hook(LPWSTR lpModuleName) {
        if (lpModuleName == nullptr) {
            return GetModuleHandle(nullptr);
        }

        return GetModuleHandleW(lpModuleName);
    }

    HMODULE WINAPI GetModuleHandleA_Hook(LPSTR lpModuleName) {
        if (lpModuleName == nullptr) {
            return GetModuleHandle(nullptr);
        }

        return GetModuleHandleA(lpModuleName);
    }

    BOOL WINAPI GetModuleHandleExW_Hook(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE *phModule) {
        if (lpModuleName == nullptr) {
            *phModule = GetModuleHandle(nullptr);
            return TRUE;
        }

        return GetModuleHandleExW(dwFlags, lpModuleName, phModule);
    }

    BOOL WINAPI GetModuleHandleExA_Hook(DWORD dwFlags, LPSTR lpModuleName, HMODULE *phModule) {
        if (lpModuleName == nullptr) {
            *phModule = GetModuleHandle(nullptr);
            return TRUE;
        }

        return GetModuleHandleExA(dwFlags, lpModuleName, phModule);
    }

    void InitialiseClientDLL() {
        static bool init = false;

        if (!init) {
            const auto mod = LoadLibraryW(gHookContext.dllName);

            if (mod) {
                const auto initFunc = reinterpret_cast<ClientEntryPoint>(GetProcAddress(mod, "InitClient"));
                if (initFunc) {
                    initFunc(gHookContext.projectDllPath);
                }
                else {
                    MessageBoxA(nullptr, "Failed to find InitClient function in client DLL", "Error", MB_ICONERROR);
                    ExitProcess(1);
                }
            }
            init = true;
        }
    }
} // namespace Framework::Launcher::Hooks
