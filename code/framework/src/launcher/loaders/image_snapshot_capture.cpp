/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "image_snapshot_capture.h"

#include "logging/logger.h"

#include <TlHelp32.h>
#include <filesystem>

namespace Framework::Launcher::Loaders {
    namespace {
        // Generous: the store may have to start its own launcher first
        constexpr uint64_t kSnapshotCaptureTimeoutMs = 120000;

        std::vector<DWORD> FindProcessesByName(const wchar_t *executableName) {
            std::vector<DWORD> processes;

            const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE) {
                return processes;
            }

            PROCESSENTRY32W entry {};
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    if (_wcsicmp(entry.szExeFile, executableName) == 0) {
                        processes.push_back(entry.th32ProcessID);
                    }
                } while (Process32NextW(snapshot, &entry));
            }

            CloseHandle(snapshot);
            return processes;
        }
    } // namespace

    bool CaptureImageSnapshot(ImageSnapshot &snapshot, const std::wstring &gamePath, const std::wstring &executableName, const std::vector<uint8_t> &sourceImage) {
        const auto logger = Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER);

        // Enough for an authorised run: the wrapper refuses us and hands the launch to the launcher
        auto commandLine   = L"\"" + gamePath + L"\"";
        const auto workDir = std::filesystem::path(gamePath).parent_path().wstring();

        STARTUPINFOW startupInfo {};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo {};

        if (!CreateProcessW(gamePath.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &startupInfo, &processInfo)) {
            logger->error("Could not start the game to capture its decrypted code, error {}", GetLastError());
            return false;
        }

        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);

        const auto deadline = GetTickCount64() + kSnapshotCaptureTimeoutMs;

        while (GetTickCount64() < deadline) {
            // The capture rejects a run the wrapper has not decrypted, so just try each in turn
            for (const auto processId : FindProcessesByName(executableName.c_str())) {
                const auto process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE, FALSE, processId);
                if (!process) {
                    continue;
                }

                const auto captured = snapshot.CaptureFrom(
                    [process](uintptr_t address, void *buffer, size_t size) {
                        SIZE_T read = 0;
                        return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer, size, &read) && read == size;
                    },
                    sourceImage);
                if (captured) {
                    TerminateProcess(process, 0);
                    CloseHandle(process);
                    return true;
                }

                CloseHandle(process);
            }

            Sleep(500);
        }

        logger->error("No authorised run of the game appeared in time, its decrypted code could not be captured");
        return false;
    }
} // namespace Framework::Launcher::Loaders
