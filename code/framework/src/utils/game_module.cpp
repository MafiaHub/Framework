/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "game_module.h"

#include "hooking/hooking.h"
#include "logging/logger.h"
#include "string_utils.h"

namespace Framework::Utils {
    namespace {
        // Prefix of the documented DLL load-notification payload, declared locally so
        // the client does not have to link ntdll to observe one module load.
        struct LoaderString {
            USHORT Length;
            USHORT MaximumLength;
            PWSTR Buffer;
        };

        struct DllNotificationData {
            ULONG Flags;
            const LoaderString *FullDllName;
            const LoaderString *BaseDllName;
            void *DllBase;
            ULONG SizeOfImage;
        };

        using DllNotification      = void(NTAPI *)(ULONG reason, const DllNotificationData *data, void *context);
        using RegisterNotifyProc   = LONG(NTAPI *)(ULONG flags, DllNotification callback, void *context, void **cookie);
        using UnregisterNotifyProc = LONG(NTAPI *)(void *cookie);

        constexpr ULONG kReasonLoaded   = 1;
        constexpr DWORD kPollIntervalMs = 50;

        struct Watch {
            std::wstring moduleName;
            GameModule::ReadyProc onReady;
            HANDLE loaded            = nullptr;
            void *notificationCookie = nullptr;
        };

        Watch gWatch;

        bool NameMatches(const LoaderString *name, const std::wstring &expected) {
            if (!name || !name->Buffer) {
                return false;
            }

            const size_t length = name->Length / sizeof(wchar_t);
            return length == expected.size() && _wcsnicmp(name->Buffer, expected.c_str(), length) == 0;
        }

        // Runs under the loader lock, so it only signals: the callback installs hooks,
        // and hooking libraries suspend threads.
        void NTAPI OnDllNotification(ULONG reason, const DllNotificationData *data, void *) {
            if (reason == kReasonLoaded && NameMatches(data ? data->BaseDllName : nullptr, gWatch.moduleName)) {
                SetEvent(gWatch.loaded);
            }
        }

        void *RegisterNotification() {
            const auto ntdll        = GetModuleHandleW(L"ntdll.dll");
            const auto registerProc = ntdll ? reinterpret_cast<RegisterNotifyProc>(GetProcAddress(ntdll, "LdrRegisterDllNotification")) : nullptr;
            if (!registerProc) {
                return nullptr;
            }

            void *cookie = nullptr;
            return registerProc(0, &OnDllNotification, nullptr, &cookie) == 0 ? cookie : nullptr;
        }

        void UnregisterNotification() {
            if (!gWatch.notificationCookie) {
                return;
            }

            const auto ntdll          = GetModuleHandleW(L"ntdll.dll");
            const auto unregisterProc = ntdll ? reinterpret_cast<UnregisterNotifyProc>(GetProcAddress(ntdll, "LdrUnregisterDllNotification")) : nullptr;
            if (unregisterProc) {
                unregisterProc(gWatch.notificationCookie);
            }
            gWatch.notificationCookie = nullptr;
        }

        bool AwaitModule() {
            if (gWatch.notificationCookie) {
                return WaitForSingleObject(gWatch.loaded, GameModule::kWaitTimeoutMs) == WAIT_OBJECT_0;
            }

            // Polling can observe a module marginally before the loader considers it
            // initialised, so it is only used when notifications are unavailable.
            for (DWORD waited = 0; waited < GameModule::kWaitTimeoutMs; waited += kPollIntervalMs) {
                if (WaitForSingleObject(gWatch.loaded, kPollIntervalMs) == WAIT_OBJECT_0 || GetModuleHandleW(gWatch.moduleName.c_str())) {
                    return true;
                }
            }
            return false;
        }

        void Dispatch(HMODULE gameModule) {
            hook::set_base(reinterpret_cast<uintptr_t>(gameModule));
            gWatch.onReady(gameModule);
        }

        DWORD WINAPI ModuleWaiter(void *) {
            const bool signalled = AwaitModule();
            UnregisterNotification();

            const auto logger     = Logging::GetLogger(FRAMEWORK_INNER_CLIENT);
            const auto moduleName = StringUtils::WideToUTF8(gWatch.moduleName);
            const auto gameModule = signalled ? GetModuleHandleW(gWatch.moduleName.c_str()) : nullptr;
            if (!gameModule) {
                logger->error("Game module {} did not load within {} ms, the game SDK will not be initialised", moduleName, GameModule::kWaitTimeoutMs);
                return 1;
            }

            logger->info("Game module {} mapped at {:#x}, initialising the game SDK", moduleName, reinterpret_cast<uintptr_t>(gameModule));
            Dispatch(gameModule);
            return 0;
        }
    } // namespace

    void GameModule::WhenReady(const std::wstring &moduleName, ReadyProc onReady) {
        if (!onReady) {
            return;
        }

        gWatch.moduleName = moduleName;
        gWatch.onReady    = std::move(onReady);

        if (moduleName.empty()) {
            Dispatch(GetModuleHandleW(nullptr));
            return;
        }

        const auto logger = Logging::GetLogger(FRAMEWORK_INNER_CLIENT);

        gWatch.loaded = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!gWatch.loaded) {
            logger->error("Failed to create the game module event, the game SDK will not be initialised");
            return;
        }

        // Register before testing for an already-mapped module, so a load racing this
        // call cannot slip between the two checks.
        gWatch.notificationCookie = RegisterNotification();
        if (GetModuleHandleW(moduleName.c_str())) {
            SetEvent(gWatch.loaded);
        }

        const auto waiter = CreateThread(nullptr, 0, &ModuleWaiter, nullptr, 0, nullptr);
        if (!waiter) {
            UnregisterNotification();
            logger->error("Failed to spawn the game module waiter, the game SDK will not be initialised");
            return;
        }
        CloseHandle(waiter);
    }
} // namespace Framework::Utils
