/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

/*
 * Based on FiveM's FileMapping.Win32.cpp:
 * https://github.com/citizenfx/fivem/blob/master/code/client/citicore/FileMapping.Win32.cpp
 * See: https://github.com/citizenfx/fivem/blob/master/code/LICENSE
 */

#include "gpu_preference.h"

#include "logging/logger.h"

#include <Windows.h>
#include <minhook.h>

namespace Framework::Launcher {
    namespace {
        constexpr const char *kNvidiaExport = "NvOptimusEnablement";
        constexpr const char *kAmdExport    = "AmdPowerXpressRequestHighPerformance";

        // The driver reads through this after the lookup returns.
        const DWORD kEnabled = 1;

        // An ordinal lookup passes the ordinal itself where the name pointer goes.
        bool IsGPUPreferenceExport(const char *name) {
            if (!name || IS_INTRESOURCE(name)) {
                return false;
            }

            return _stricmp(name, kNvidiaExport) == 0 || _stricmp(name, kAmdExport) == 0;
        }

        FARPROC(WINAPI *gOrigGetProcAddressForCaller)(HMODULE module, LPCSTR procName, void *caller) = nullptr;

        FARPROC WINAPI GetProcAddressForCallerStub(HMODULE module, LPCSTR procName, void *caller) {
            if (IsGPUPreferenceExport(procName)) {
                return reinterpret_cast<FARPROC>(const_cast<DWORD *>(&kEnabled));
            }

            return gOrigGetProcAddressForCaller(module, procName, caller);
        }

        struct AnsiString {
            USHORT Length;
            USHORT MaximumLength;
            char *Buffer;
        };

        bool AnsiMatches(const AnsiString *name, const char *expected) {
            const size_t length = strlen(expected);
            return name->Length == length && _strnicmp(name->Buffer, expected, length) == 0;
        }

        NTSTATUS(NTAPI *gOrigLdrGetProcedureAddress)(HMODULE module, AnsiString *name, WORD ordinal, void **address) = nullptr;

        NTSTATUS NTAPI LdrGetProcedureAddressStub(HMODULE module, AnsiString *name, WORD ordinal, void **address) {
            if (name && name->Buffer && (AnsiMatches(name, kNvidiaExport) || AnsiMatches(name, kAmdExport))) {
                *address = const_cast<DWORD *>(&kEnabled);
                return 0;
            }

            return gOrigLdrGetProcedureAddress(module, name, ordinal, address);
        }

        // The exports above are only a hint; this is the decision the driver acts on.
        HRESULT(WINAPI *gOrigQueryDListForApplication1)(BOOL *defaultToDiscrete, HANDLE adapter, void *escapeCallback) = nullptr;

        HRESULT WINAPI QueryDListForApplication1Stub(BOOL *defaultToDiscrete, HANDLE adapter, void *escapeCallback) {
            const HRESULT hr = gOrigQueryDListForApplication1(defaultToDiscrete, adapter, escapeCallback);

            if (SUCCEEDED(hr)) {
                if (!*defaultToDiscrete) {
                    Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("Overriding the driver's integrated-GPU choice; forcing the discrete GPU.");
                }
                *defaultToDiscrete = TRUE;
            }

            return hr;
        }

        struct LoaderString {
            USHORT Length;
            USHORT MaximumLength;
            wchar_t *Buffer;
        };

        struct DllNotificationData {
            ULONG Flags;
            const LoaderString *FullDllName;
            const LoaderString *BaseDllName;
            void *DllBase;
            ULONG SizeOfImage;
        };

        using DllNotification    = void(NTAPI *)(ULONG reason, const DllNotificationData *data, void *context);
        using RegisterNotifyProc = LONG(NTAPI *)(ULONG flags, DllNotification callback, void *context, void **cookie);

        constexpr ULONG kReasonLoaded = 1;

        bool NameMatches(const LoaderString *name, const wchar_t *expected) {
            if (!name || !name->Buffer) {
                return false;
            }

            const size_t length = name->Length / sizeof(wchar_t);
            return length == wcslen(expected) && _wcsnicmp(name->Buffer, expected, length) == 0;
        }

        // Under the loader lock, but deferring loses the race against the driver's first
        // query. The module has just been mapped, so nothing is executing inside it yet.
        void NTAPI OnDllNotification(ULONG reason, const DllNotificationData *data, void *) {
            if (reason != kReasonLoaded || !data) {
                return;
            }

            if (!NameMatches(data->BaseDllName, L"nvdlistx.dll") && !NameMatches(data->BaseDllName, L"amdhdl64.dll")) {
                return;
            }

            void *proc = GetProcAddress(reinterpret_cast<HMODULE>(data->DllBase), "QueryDListForApplication1");
            if (!proc || gOrigQueryDListForApplication1) {
                return;
            }

            if (MH_CreateHook(proc, reinterpret_cast<void *>(&QueryDListForApplication1Stub), reinterpret_cast<void **>(&gOrigQueryDListForApplication1)) == MH_OK) {
                MH_EnableHook(proc);
            }
        }
    } // namespace

    void ForceHighPerformanceGPU() {
        static bool installed = false;
        if (installed) {
            return;
        }
        installed = true;

        // FrameworkLoaderData links its own copy, so this module needs its own init.
        MH_Initialize();

        // GetProcAddress funnels through kernelbase, but a driver may call ntdll directly.
        MH_CreateHookApi(L"kernelbase.dll", "GetProcAddressForCaller", reinterpret_cast<void *>(&GetProcAddressForCallerStub), reinterpret_cast<void **>(&gOrigGetProcAddressForCaller));
        MH_CreateHookApi(L"ntdll.dll", "LdrGetProcedureAddress", reinterpret_cast<void *>(&LdrGetProcedureAddressStub), reinterpret_cast<void **>(&gOrigLdrGetProcedureAddress));
        MH_EnableHook(MH_ALL_HOOKS);

        const auto ntdll        = GetModuleHandleW(L"ntdll.dll");
        const auto registerProc = ntdll ? reinterpret_cast<RegisterNotifyProc>(GetProcAddress(ntdll, "LdrRegisterDllNotification")) : nullptr;
        if (registerProc) {
            void *cookie = nullptr;
            registerProc(0, &OnDllNotification, nullptr, &cookie);
        }
    }
} // namespace Framework::Launcher
