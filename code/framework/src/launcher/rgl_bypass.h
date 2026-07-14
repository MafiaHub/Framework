/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>

#include <function2.hpp>

namespace Framework::Launcher::RGL {

    // Result codes for bypass operations
    enum class BypassResult {
        SUCCESS,
        PATTERN_NOT_FOUND,
        MEMORY_PROTECTION_FAILED,
        INVALID_MODULE,
        HOOK_FAILED,
        PROCESS_NOT_FOUND,
        INJECTION_FAILED,
        TIMEOUT
    };

    /**
     * Game-specific configuration for RGL bypass
     * Projects must provide their own offsets/patterns
     */
    struct BypassConfig {
        // Optional: Known offset to verification function (0 = use pattern scan)
        uintptr_t verifyFuncOffset = 0;

        // Optional: Known offset to conditional jump after verify call (0 = use pattern scan)
        uintptr_t conditionalJmpOffset = 0;

        // Optional: Pattern to find verification call site
        const uint8_t *verifyCallPattern = nullptr;
        size_t verifyCallPatternSize = 0;
        const char *verifyCallPatternMask = nullptr;
    };

    /**
     * RGL (Rockstar Games Launcher) Bypass
     *
     * Provides methods to bypass PE signature verification in RGL-protected games.
     *
     * For Steam+RGL games, the bypass must run INSIDE the game process because:
     * - Steam launches RGL
     * - RGL validates and spawns the game as a NEW process
     * - The launcher process is separate from the game process
     *
     * Usage patterns:
     * 1. In-process (PE Loading): Call Apply* methods directly
     * 2. Cross-process (DLL Injection): Use ProcessMonitor to inject a DLL that calls Apply*
     */
    class Bypass final {
      public:
        explicit Bypass(uintptr_t moduleBase = 0, const BypassConfig &config = {});

        /**
         * Initialize with current process module base
         * Call this from within the game process (e.g., from injected DLL)
         */
        void InitializeForCurrentProcess();

        /**
         * Set game-specific configuration
         */
        void SetConfig(const BypassConfig &config);

        // === In-Process Bypass Methods ===
        // These must be called from WITHIN the game process

        BypassResult ApplyIATHook();
        BypassResult ApplyVerificationPatch();
        BypassResult ApplyConditionalPatch();

        /**
         * Apply all bypass methods with fallback
         * Tries each method until one succeeds
         */
        BypassResult ApplyAll();

        // === Static Hook Functions ===
        static LONG WINAPI WinVerifyTrustHook(HWND hwnd, GUID *pgActionID, LPVOID pWVTData);

        static uintptr_t ScanPattern(uintptr_t start, size_t size,
                                     const uint8_t *pattern, size_t patternSize,
                                     const char *mask = nullptr);

      private:
        uintptr_t _moduleBase;
        BypassConfig _config;
        bool PatchMemory(uintptr_t address, const uint8_t *data, size_t size);
    };

    /**
     * Process Monitor for cross-process injection
     *
     * Monitors for game process creation and injects the client DLL
     * when the game starts. The injected DLL should call Bypass::ApplyAll()
     * during its initialization.
     */
    class ProcessMonitor final {
      public:
        using ProcessCallback = fu2::function<void(DWORD processId, HANDLE processHandle)>;

        /**
         * Wait for a process to start and get a handle to it
         * @param processName Name of the executable (e.g., "GTA5.exe")
         * @param timeoutMs Maximum time to wait in milliseconds (0 = infinite)
         * @return Process handle or INVALID_HANDLE_VALUE on failure
         */
        static HANDLE WaitForProcess(const wchar_t *processName, DWORD timeoutMs = 30000);

        /**
         * Inject a DLL into a running process
         * @param processHandle Handle to the target process
         * @param dllPath Full path to the DLL to inject
         * @return BypassResult indicating success or failure
         */
        static BypassResult InjectDLL(HANDLE processHandle, const wchar_t *dllPath);

        /**
         * Combined: Wait for process and inject DLL
         * @param processName Name of the executable
         * @param dllPath Full path to the DLL to inject
         * @param timeoutMs Maximum time to wait for process
         * @return BypassResult indicating success or failure
         */
        static BypassResult WaitAndInject(const wchar_t *processName,
                                          const wchar_t *dllPath,
                                          DWORD timeoutMs = 30000);

        /**
         * Find process ID by name
         * @param processName Name of the executable
         * @return Process ID or 0 if not found
         */
        static DWORD FindProcessByName(const wchar_t *processName);
    };

} // namespace Framework::Launcher::RGL
