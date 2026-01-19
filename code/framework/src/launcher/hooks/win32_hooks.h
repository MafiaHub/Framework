/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../project_config.h"

#include <Windows.h>
#include <Psapi.h>

namespace Framework::Launcher::Hooks {
    /**
     * Context structure holding global state needed by Win32 hooks.
     * This encapsulates the previously scattered global variables.
     */
    struct HookContext {
        const wchar_t *imagePath               = nullptr;
        const wchar_t *dllName                 = nullptr;
        wchar_t projectDllPath[32768]          = {};
        ProjectConfiguration *config           = nullptr;
    };

    /**
     * Get the singleton hook context instance.
     */
    HookContext &GetHookContext();

    // Startup info stubs - intercept startup info calls to initialize client DLL
    void WINAPI GetStartupInfoW_Stub(LPSTARTUPINFOW lpStartupInfo);
    void WINAPI GetStartupInfoA_Stub(LPSTARTUPINFOA lpStartupInfo);

    // Command line stubs - append additional launch arguments
    LPWSTR WINAPI GetCommandLineW_Stub();
    LPSTR WINAPI GetCommandLineA_Stub();

    // Module file name hooks - redirect queries for the main module to the game executable
    DWORD WINAPI GetModuleFileNameA_Hook(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
    DWORD WINAPI GetModuleFileNameExA_Hook(HANDLE hProcess, HMODULE hModule, LPSTR lpFilename, DWORD nSize);
    DWORD WINAPI GetModuleFileNameW_Hook(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
    DWORD WINAPI GetModuleFileNameExW_Hook(HANDLE hProcess, HMODULE hModule, LPWSTR lpFilename, DWORD nSize);

    // Module handle hooks - ensure queries for null module return the correct handle
    HMODULE WINAPI GetModuleHandleW_Hook(LPWSTR lpModuleName);
    HMODULE WINAPI GetModuleHandleA_Hook(LPSTR lpModuleName);
    BOOL WINAPI GetModuleHandleExW_Hook(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE *phModule);
    BOOL WINAPI GetModuleHandleExA_Hook(DWORD dwFlags, LPSTR lpModuleName, HMODULE *phModule);

    /**
     * Initialize the client DLL. Called from stubs when the game starts.
     * This loads the destination DLL and calls its InitClient function.
     */
    void InitialiseClientDLL();
} // namespace Framework::Launcher::Hooks
