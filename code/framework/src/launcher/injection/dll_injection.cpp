/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "dll_injection.h"

#include <cassert>

namespace Framework::Launcher::Injection {
    DLLInjectionResults InjectLibraryIntoProcess(HANDLE hProcess, const wchar_t *szLibraryPath) {
        DLLInjectionResults result = INJECT_LIBRARY_RESULT_OK;

        // Get the length of the library path
        const size_t sLibraryPathLen = (wcslen(szLibraryPath) + 1) * sizeof(WCHAR);

        // Allocate the a block of memory in our target process for the library path
        void *pRemoteLibraryPath = VirtualAllocEx(hProcess, NULL, sLibraryPathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        // Write our library path to the allocated block of memory
        SIZE_T sBytesWritten     = 0;
        const BOOL bWriteSuccess = WriteProcessMemory(hProcess, pRemoteLibraryPath, szLibraryPath, sLibraryPathLen, &sBytesWritten);

        if (!bWriteSuccess || sBytesWritten != sLibraryPathLen) {
            result = INJECT_LIBRARY_RESULT_WRITE_FAILED;
        }
        else {
            // Get the handle of Kernel32.dll
            const HMODULE hKernel32 = GetModuleHandle("kernel32.dll");
            if (hKernel32 == NULL) {
                result = INJECT_LIBRARY_GET_MODULE_HANDLE_FAILED;
            }
            else {
                // Get the address of the LoadLibraryA function from Kernel32.dll
                const FARPROC pfnLoadLibraryW = GetProcAddress(hKernel32, "LoadLibraryW");
                if (pfnLoadLibraryW == NULL) {
                    result = INJECT_LIBRARY_GET_PROC_ADDRESS_FAILED;
                }
                else {
                    // Create a thread inside the target process to load our library
                    const HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pfnLoadLibraryW, pRemoteLibraryPath, 0, NULL);

                    if (hThread) {
                        // Wait for the created thread to end
                        WaitForSingleObject(hThread, INFINITE);

                        DWORD dwExitCode = 0;
                        if (GetExitCodeThread(hThread, &dwExitCode)) {
                            // Should never happen as we wait for the thread to be finished.
                            assert(dwExitCode != STILL_ACTIVE);
                        }
                        else {
                            result = INJECT_LIBRARY_GET_RETURN_CODE_FAILED;
                        }

                        // In case LoadLibrary returns handle equal to zero there was some problem.
                        if (dwExitCode == 0) {
                            result = INJECT_LIBRARY_LOAD_LIBRARY_FAILED;
                        }

                        // Close our thread handle
                        CloseHandle(hThread);
                    }
                    else {
                        // Thread creation failed
                        result = INJECT_LIBRARY_THREAD_CREATION_FAILED;
                    }
                }
            }
        }

        // Free the allocated block of memory inside the target process
        VirtualFreeEx(hProcess, pRemoteLibraryPath, 0, MEM_RELEASE);
        return result;
    }

    DLLInjectionResults InjectLibraryIntoProcess(DWORD dwProcessId, const wchar_t *szLibraryPath) {
        // Open our target process
        const HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwProcessId);

        if (!hProcess) {
            // Failed to open the process
            return INJECT_LIBRARY_OPEN_PROCESS_FAIL;
        }

        // Inject the library into the process
        const DLLInjectionResults result = InjectLibraryIntoProcess(hProcess, szLibraryPath);

        // Close the process handle
        CloseHandle(hProcess);
        return result;
    }

    const char *InjectLibraryResultToString(DLLInjectionResults result) {
        switch (result) {
        case INJECT_LIBRARY_RESULT_OK: return "Ok";
        case INJECT_LIBRARY_RESULT_WRITE_FAILED: return "Failed to write memory into process";
        case INJECT_LIBRARY_GET_RETURN_CODE_FAILED: return "Failed to get return code of the load call";
        case INJECT_LIBRARY_LOAD_LIBRARY_FAILED: return "Failed to load library";
        case INJECT_LIBRARY_THREAD_CREATION_FAILED: return "Failed to create remote thread";
        case INJECT_LIBRARY_GET_MODULE_HANDLE_FAILED: return "Failed to get kernel32 module handle";
        case INJECT_LIBRARY_GET_PROC_ADDRESS_FAILED: return "Failed to get LoadLibraryW address";
        case INJECT_LIBRARY_OPEN_PROCESS_FAIL: return "Open of the process failed";
        default: return "Unknown error";
        }
    }
} // namespace Framework::Launcher::Injection
