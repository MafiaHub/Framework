/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <utils/safe_win32.h>

#include "host.h"

#include "gui/remote/launch.h"

#include <logging/logger.h>

#include <shellapi.h>

#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <string>
#include <string_view>

// The CEF host a client starts from <client bin>/cef64/ (see gui/remote/remote_session.cpp). Not a
// CEF subprocess itself: its children are cef_subprocess.exe next to it.

namespace {
    // Exit with the client, however it went: a crash or a kill skips the shutdown message.
    DWORD WINAPI MonitorParentProcess(LPVOID param) {
        const HANDLE parent = static_cast<HANDLE>(param);
        if (WaitForSingleObject(parent, INFINITE) == WAIT_OBJECT_0) {
            ExitProcess(0);
        }
        CloseHandle(parent);
        return 0;
    }

    std::wstring SwitchValue(int argc, wchar_t **argv, std::wstring_view name) {
        for (int i = 1; i < argc; ++i) {
            const std::wstring_view arg = argv[i];
            if (arg.size() > name.size() + 3 && arg.substr(0, 2) == L"--" && arg.substr(2, name.size()) == name && arg[name.size() + 2] == L'=') {
                return std::wstring(arg.substr(name.size() + 3));
            }
        }
        return {};
    }

    std::filesystem::path ExecutableDirectory() {
        wchar_t path[MAX_PATH] = {};
        const DWORD length     = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return {};
        }
        return std::filesystem::path(std::wstring(path, length)).parent_path();
    }
} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    using namespace Framework::GUI::Remote;

    // The wide command line, whatever the entry point was handed: the root may not be ANSI.
    int argc       = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return 1;
    }
    const std::wstring pipe   = SwitchValue(argc, argv, kHostPipeSwitch);
    const std::wstring token  = SwitchValue(argc, argv, kHostTokenSwitch);
    const std::wstring root   = SwitchValue(argc, argv, kHostRootSwitch);
    const std::wstring parent = SwitchValue(argc, argv, kHostParentSwitch);
    LocalFree(argv);

    const std::filesystem::path moduleDir = ExecutableDirectory();
    if (pipe.empty() || token.empty() || root.empty() || moduleDir.empty()) {
        return 1;
    }

    if (const DWORD pid = std::wcstoul(parent.c_str(), nullptr, 10)) {
        if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
            if (HANDLE monitor = CreateThread(nullptr, 0, MonitorParentProcess, process, 0, nullptr)) {
                CloseHandle(monitor);
            }
            else {
                CloseHandle(process);
            }
        }
        else {
            // The client is already gone.
            return 0;
        }
    }

    const std::filesystem::path rootDir = root;
    auto *logging                       = Framework::Logging::GetInstance();
    logging->SetLogName("cef_host");
    logging->SetLogFolder((rootDir / "logs").string());

    int exitCode = 0;
    {
        Host host(token, rootDir.string(), moduleDir);
        exitCode = host.Run(pipe);
    }
    logging->Shutdown();
    return exitCode;
}
