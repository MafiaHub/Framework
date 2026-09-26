/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <windows.h>
#include <winternl.h>

#include "include/cef_app.h"
#include "renderer_app.h"

#include <cwchar>
#include <string>

namespace {
    DWORD GetParentProcessId() {
        using NtQueryInformationProcess_t = NTSTATUS(NTAPI *)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
        const auto ntQuery = reinterpret_cast<NtQueryInformationProcess_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
        if (!ntQuery) {
            return 0;
        }
        PROCESS_BASIC_INFORMATION pbi {};
        if (ntQuery(GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi), nullptr) < 0) {
            return 0;
        }
        return static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(pbi.Reserved3)); // InheritedFromUniqueProcessId
    }

    DWORD GetBrowserProcessId() {
        auto commandLine = CefCommandLine::CreateCommandLine();
        commandLine->InitFromString(GetCommandLineW());
        const std::wstring value = commandLine->GetSwitchValue("framework-browser-pid").ToWString();
        if (value.empty()) {
            return GetParentProcessId();
        }
        wchar_t *end = nullptr;
        const unsigned long pid = std::wcstoul(value.c_str(), &end, 10);
        return end != value.c_str() && *end == L'\0' ? static_cast<DWORD>(pid) : 0;
    }

    // Self-exit when the browser (game) process dies, including an abrupt
    // termination that skips the normal CefShutdown path.
    DWORD WINAPI MonitorParentProcess(LPVOID) {
        const DWORD browserPid = GetBrowserProcessId();
        if (!browserPid || browserPid == GetCurrentProcessId()) {
            return 0;
        }
        HANDLE browser = OpenProcess(SYNCHRONIZE, FALSE, browserPid);
        if (!browser) {
            ExitProcess(0);
        }
        if (WaitForSingleObject(browser, INFINITE) == WAIT_OBJECT_0) {
            ExitProcess(0);
        }
        CloseHandle(browser);
        return 0;
    }
} // namespace

int main(int argc, char *argv[]) {
    if (HANDLE monitor = CreateThread(nullptr, 0, MonitorParentProcess, nullptr, 0, nullptr)) {
        CloseHandle(monitor);
    }

    CefMainArgs mainArgs(GetModuleHandle(nullptr));
    CefRefPtr<Framework::GUI::CEF::RendererApp> app(new Framework::GUI::CEF::RendererApp);
    return CefExecuteProcess(mainArgs, app, nullptr);
}
