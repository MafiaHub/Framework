/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "rgl_bypass.h"

#include <Psapi.h>
#include <TlHelp32.h>
#include <cstring>

namespace Framework::Launcher::RGL {

    namespace {
        // How long to wait for the injected LoadLibraryW thread to finish, in ms. Bounded so a hung
        // loader can't wedge injection forever; a timeout is treated as a failed injection.
        constexpr DWORD kInjectionWaitMs = 10000;

        // Stub tail: add eax, <entry RVA> applied to the PEB-read image base, then the hand-off
        constexpr uint8_t kEntryStubTail[]  = {0x05, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xE5, 0x89, 0x44, 0x24, 0x20, 0x5D, 0x61, 0xFF, 0xE0};
        constexpr char kEntryStubTailMask[] = "x????xxxxxxxxxx";

        // Only tells a changed stub apart from an image that never had one; the tail identifies it
        constexpr char kEntryStubSection[] = ".rkstr";
    } // namespace

    EntryStub ResolveEntryStub(uintptr_t moduleBase, uintptr_t stubEntryPoint) {
        auto *dosHeader = reinterpret_cast<IMAGE_DOS_HEADER *>(moduleBase);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            return {};
        }

        auto *ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS *>(moduleBase + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
            return {};
        }

        // The stub has a section to itself, which bounds the scan
        const auto entryRva = static_cast<uint32_t>(stubEntryPoint - moduleBase);
        auto *section       = IMAGE_FIRST_SECTION(ntHeaders);
        auto *sectionsEnd   = section + ntHeaders->FileHeader.NumberOfSections;
        for (; section != sectionsEnd; ++section) {
            if (entryRva >= section->VirtualAddress && entryRva < section->VirtualAddress + section->Misc.VirtualSize) {
                break;
            }
        }

        if (section == sectionsEnd) {
            return {};
        }

        const bool stubSection = memcmp(section->Name, kEntryStubSection, sizeof(kEntryStubSection)) == 0;
        const auto scanEnd     = moduleBase + section->VirtualAddress + section->Misc.VirtualSize;
        const auto notResolved = stubSection ? EntryStubStatus::UNSUPPORTED : EntryStubStatus::NOT_PRESENT;

        if (scanEnd < stubEntryPoint + sizeof(kEntryStubTail)) {
            return {notResolved};
        }

        const auto tail = Bypass::ScanPattern(stubEntryPoint, scanEnd - stubEntryPoint, kEntryStubTail, sizeof(kEntryStubTail), kEntryStubTailMask);
        if (!tail) {
            return {notResolved};
        }

        const auto entryPointRva = *reinterpret_cast<const uint32_t *>(tail + 1);
        if (entryPointRva == 0 || entryPointRva >= ntHeaders->OptionalHeader.SizeOfImage) {
            return {EntryStubStatus::UNSUPPORTED};
        }

        return {EntryStubStatus::RESOLVED, moduleBase + entryPointRva};
    }

    // =========================================================================
    // Bypass Implementation
    // =========================================================================

    Bypass::Bypass(uintptr_t moduleBase, const BypassConfig &config)
        : _moduleBase(moduleBase), _config(config) {
    }

    void Bypass::InitializeForCurrentProcess() {
        _moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    }

    void Bypass::SetConfig(const BypassConfig &config) {
        _config = config;
    }

    LONG WINAPI Bypass::WinVerifyTrustHook(HWND hwnd, GUID *pgActionID, LPVOID pWVTData) {
        return 0; // S_OK - signature valid
    }

    uintptr_t Bypass::ScanPattern(uintptr_t start, size_t size,
                                   const uint8_t *pattern, size_t patternSize,
                                   const char *mask) {
        if (size < patternSize) return 0;

        for (size_t i = 0; i <= size - patternSize; ++i) {
            bool found = true;
            for (size_t j = 0; j < patternSize; ++j) {
                if (mask && mask[j] == '?') continue;
                if (*reinterpret_cast<const uint8_t *>(start + i + j) != pattern[j]) {
                    found = false;
                    break;
                }
            }
            if (found) return start + i;
        }
        return 0;
    }

    bool Bypass::PatchMemory(uintptr_t address, const uint8_t *data, size_t size) {
        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void *>(address), size,
                            PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }

        memcpy(reinterpret_cast<void *>(address), data, size);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(address), size);

        DWORD temp;
        VirtualProtect(reinterpret_cast<void *>(address), size, oldProtect, &temp);
        return true;
    }

    BypassResult Bypass::ApplyAll() {
        if (!_moduleBase) {
            InitializeForCurrentProcess();
        }

        // Try IAT hook first
        auto result = ApplyIATHook();
        if (result == BypassResult::SUCCESS) return result;

        // Try conditional patch
        result = ApplyConditionalPatch();
        if (result == BypassResult::SUCCESS) return result;

        // Last resort: full function patch
        return ApplyVerificationPatch();
    }

    BypassResult Bypass::ApplyIATHook() {
        if (!_moduleBase) return BypassResult::INVALID_MODULE;

        auto *dosHeader = reinterpret_cast<IMAGE_DOS_HEADER *>(_moduleBase);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return BypassResult::INVALID_MODULE;

        auto *ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS *>(_moduleBase + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return BypassResult::INVALID_MODULE;

        auto &importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!importDir.VirtualAddress) return BypassResult::PATTERN_NOT_FOUND;

        auto *importDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(_moduleBase + importDir.VirtualAddress);

        while (importDesc->Name) {
            auto *moduleName = reinterpret_cast<const char *>(_moduleBase + importDesc->Name);

            if (_stricmp(moduleName, "wintrust.dll") == 0) {
                auto *thunk = reinterpret_cast<IMAGE_THUNK_DATA *>(_moduleBase + importDesc->FirstThunk);
                auto *origThunk = reinterpret_cast<IMAGE_THUNK_DATA *>(_moduleBase + importDesc->OriginalFirstThunk);

                while (thunk->u1.Function) {
                    if (!(origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                        auto *importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(
                            _moduleBase + origThunk->u1.AddressOfData);

                        if (strcmp(importByName->Name, "WinVerifyTrust") == 0) {
                            DWORD oldProtect;
                            if (!VirtualProtect(&thunk->u1.Function, sizeof(void *),
                                                PAGE_EXECUTE_READWRITE, &oldProtect)) {
                                return BypassResult::MEMORY_PROTECTION_FAILED;
                            }

                            thunk->u1.Function = reinterpret_cast<uintptr_t>(WinVerifyTrustHook);

                            DWORD temp;
                            VirtualProtect(&thunk->u1.Function, sizeof(void *), oldProtect, &temp);
                            return BypassResult::SUCCESS;
                        }
                    }
                    ++thunk;
                    ++origThunk;
                }
            }
            ++importDesc;
        }

        return BypassResult::PATTERN_NOT_FOUND;
    }

    BypassResult Bypass::ApplyVerificationPatch() {
        if (!_moduleBase) return BypassResult::INVALID_MODULE;

        uintptr_t verifyFunc = 0;

        // Try using configured offset first
        if (_config.verifyFuncOffset != 0) {
            verifyFunc = _moduleBase + _config.verifyFuncOffset;

            // Verify prologue: 48 89 5C 24 08
            const uint8_t expectedPrologue[] = {0x48, 0x89, 0x5C, 0x24, 0x08};
            bool prologueMatch = true;
            for (size_t i = 0; i < sizeof(expectedPrologue); ++i) {
                if (*reinterpret_cast<uint8_t *>(verifyFunc + i) != expectedPrologue[i]) {
                    prologueMatch = false;
                    break;
                }
            }

            if (!prologueMatch) {
                verifyFunc = 0; // Fall back to pattern scan
            }
        }

        // Fall back to pattern scan if offset didn't work
        if (verifyFunc == 0 && _config.verifyCallPattern != nullptr) {
            MODULEINFO modInfo;
            if (!GetModuleInformation(GetCurrentProcess(),
                                      reinterpret_cast<HMODULE>(_moduleBase),
                                      &modInfo, sizeof(modInfo))) {
                return BypassResult::PATTERN_NOT_FOUND;
            }

            uintptr_t patternAddr = ScanPattern(_moduleBase, modInfo.SizeOfImage,
                _config.verifyCallPattern, _config.verifyCallPatternSize,
                _config.verifyCallPatternMask);

            if (!patternAddr) return BypassResult::PATTERN_NOT_FOUND;

            int32_t relativeOffset = *reinterpret_cast<int32_t *>(patternAddr + 1);
            verifyFunc = patternAddr + 5 + relativeOffset;
        }

        if (verifyFunc == 0) return BypassResult::PATTERN_NOT_FOUND;

        // Patch: mov eax, 1; ret
        const uint8_t patch[] = {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3};

        if (!PatchMemory(verifyFunc, patch, sizeof(patch))) {
            return BypassResult::MEMORY_PROTECTION_FAILED;
        }

        return BypassResult::SUCCESS;
    }

    BypassResult Bypass::ApplyConditionalPatch() {
        if (!_moduleBase) return BypassResult::INVALID_MODULE;

        MODULEINFO modInfo;
        if (!GetModuleInformation(GetCurrentProcess(),
                                  reinterpret_cast<HMODULE>(_moduleBase),
                                  &modInfo, sizeof(modInfo))) {
            return BypassResult::INVALID_MODULE;
        }

        uintptr_t patchAddr = 0;

        // Try using configured offset first
        if (_config.conditionalJmpOffset != 0) {
            patchAddr = _moduleBase + _config.conditionalJmpOffset;

            if (*reinterpret_cast<uint8_t *>(patchAddr) != 0x74) {
                patchAddr = 0; // Fall back to pattern scan
            }
        }

        // Fall back to pattern scan
        if (patchAddr == 0 && _config.verifyCallPattern != nullptr) {
            uintptr_t patternAddr = ScanPattern(_moduleBase, modInfo.SizeOfImage,
                _config.verifyCallPattern, _config.verifyCallPatternSize,
                _config.verifyCallPatternMask);

            if (!patternAddr) return BypassResult::PATTERN_NOT_FOUND;
            patchAddr = patternAddr + 7;
        }

        if (patchAddr == 0 || *reinterpret_cast<uint8_t *>(patchAddr) != 0x74) {
            return BypassResult::PATTERN_NOT_FOUND;
        }

        // NOP the conditional jump
        const uint8_t patch[] = {0x90, 0x90};

        if (!PatchMemory(patchAddr, patch, sizeof(patch))) {
            return BypassResult::MEMORY_PROTECTION_FAILED;
        }

        return BypassResult::SUCCESS;
    }

    // =========================================================================
    // ProcessMonitor Implementation
    // =========================================================================

    DWORD ProcessMonitor::FindProcessByName(const wchar_t *processName) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);

        DWORD pid = 0;
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, processName) == 0) {
                    pid = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return pid;
    }

    HANDLE ProcessMonitor::WaitForProcess(const wchar_t *processName, DWORD timeoutMs) {
        DWORD startTime = GetTickCount();

        while (true) {
            DWORD pid = FindProcessByName(processName);
            if (pid != 0) {
                HANDLE hProcess = OpenProcess(
                    PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                    PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                    FALSE, pid);

                if (hProcess != nullptr) {
                    // Wait a bit for the process to initialize
                    Sleep(500);
                    return hProcess;
                }
            }

            if (timeoutMs > 0 && (GetTickCount() - startTime) >= timeoutMs) {
                return INVALID_HANDLE_VALUE;
            }

            Sleep(100);
        }
    }

    BypassResult ProcessMonitor::InjectDLL(HANDLE processHandle, const wchar_t *dllPath) {
        if (processHandle == INVALID_HANDLE_VALUE || processHandle == nullptr) {
            return BypassResult::INVALID_MODULE;
        }

        size_t pathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);

        // Allocate memory in target process for DLL path
        void *remotePath = VirtualAllocEx(processHandle, nullptr, pathSize,
                                          MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remotePath) {
            return BypassResult::INJECTION_FAILED;
        }

        // Write DLL path to target process
        SIZE_T bytesWritten;
        if (!WriteProcessMemory(processHandle, remotePath, dllPath, pathSize, &bytesWritten) ||
            bytesWritten != pathSize) {
            VirtualFreeEx(processHandle, remotePath, 0, MEM_RELEASE);
            return BypassResult::INJECTION_FAILED;
        }

        // Get LoadLibraryW address
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (!kernel32) {
            VirtualFreeEx(processHandle, remotePath, 0, MEM_RELEASE);
            return BypassResult::INJECTION_FAILED;
        }

        auto loadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(kernel32, "LoadLibraryW"));
        if (!loadLibraryW) {
            VirtualFreeEx(processHandle, remotePath, 0, MEM_RELEASE);
            return BypassResult::INJECTION_FAILED;
        }

        // Create remote thread to load the DLL
        HANDLE hThread = CreateRemoteThread(processHandle, nullptr, 0,
                                            loadLibraryW, remotePath, 0, nullptr);
        if (!hThread) {
            VirtualFreeEx(processHandle, remotePath, 0, MEM_RELEASE);
            return BypassResult::INJECTION_FAILED;
        }

        // Wait for the loader thread; a timeout means it is still running (LoadLibraryW hung), so
        // treat it as a failed injection rather than reading a bogus exit code.
        const DWORD waitResult = WaitForSingleObject(hThread, kInjectionWaitMs);

        DWORD exitCode = 0;
        if (waitResult == WAIT_OBJECT_0) {
            GetExitCodeThread(hThread, &exitCode);
        }

        CloseHandle(hThread);
        VirtualFreeEx(processHandle, remotePath, 0, MEM_RELEASE);

        return (exitCode != 0) ? BypassResult::SUCCESS : BypassResult::INJECTION_FAILED;
    }

    BypassResult ProcessMonitor::WaitAndInject(const wchar_t *processName,
                                                const wchar_t *dllPath,
                                                DWORD timeoutMs) {
        HANDLE hProcess = WaitForProcess(processName, timeoutMs);
        if (hProcess == INVALID_HANDLE_VALUE) {
            return BypassResult::TIMEOUT;
        }

        BypassResult result = InjectDLL(hProcess, dllPath);
        CloseHandle(hProcess);
        return result;
    }

} // namespace Framework::Launcher::RGL
