/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <windows.h>

#include <cstdlib>
#include <string>

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "renderer_app.h"

namespace {
    // Self-exit when the parent (game) process dies, so an abrupt quit or crash
    // that skips CEF teardown doesn't leave this helper orphaned. The parent is
    // named by the switch App::OnBeforeChildProcessLaunch adds: querying it from
    // ntdll is a pattern antivirus heuristics flag the helper for.
    DWORD WINAPI MonitorParentProcess(LPVOID param) {
        HANDLE parent = static_cast<HANDLE>(param);
        if (WaitForSingleObject(parent, INFINITE) == WAIT_OBJECT_0) {
            ExitProcess(0);
        }
        CloseHandle(parent);
        return 0;
    }
} // namespace

int main(int argc, char *argv[]) {
    CefRefPtr<CefCommandLine> commandLine = CefCommandLine::CreateCommandLine();
    commandLine->InitFromString(GetCommandLineW());
    const std::string parentPid = commandLine->GetSwitchValue(Framework::GUI::CEF::kParentProcessSwitch).ToString();
    if (const DWORD pid = std::strtoul(parentPid.c_str(), nullptr, 10)) {
        if (HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
            if (HANDLE monitor = CreateThread(nullptr, 0, MonitorParentProcess, parent, 0, nullptr)) {
                CloseHandle(monitor);
            }
            else {
                CloseHandle(parent);
            }
        }
    }

    CefMainArgs mainArgs(GetModuleHandle(nullptr));
    CefRefPtr<Framework::GUI::CEF::RendererApp> app(new Framework::GUI::CEF::RendererApp);
    return CefExecuteProcess(mainArgs, app, nullptr);
}
