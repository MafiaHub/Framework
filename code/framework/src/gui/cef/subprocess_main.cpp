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

    // Self-exit when the parent (game) process dies, so an abrupt quit or crash
    // that skips CEF teardown doesn't leave this helper orphaned.
    DWORD WINAPI MonitorParentProcess(LPVOID) {
        const DWORD parentPid = GetParentProcessId();
        if (!parentPid) {
            return 0;
        }
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
        if (!parent) {
            return 0;
        }
        if (WaitForSingleObject(parent, INFINITE) == WAIT_OBJECT_0) {
            ExitProcess(0);
        }
        CloseHandle(parent);
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
