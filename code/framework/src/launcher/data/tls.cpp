/*
 * MafiaHub OSS license
 * Copyright (c) 2020, CitizenFX
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <windows.h>

#include <atomic>
#include <cstdint>

__declspec(thread) uint8_t tls1[sizeof(int) * 17600];

extern "C" uint8_t _tls_start;
extern "C" uint32_t _tls_index;

class TlsActivation {
  public:
    TlsActivation() {
        tls1[5] = 0;
    }
};

[[maybe_unused]] static TlsActivation TlsActivateItEarly;

extern "C" __declspec(dllexport) void GetThreadLocalStorage(void **base, uint32_t *index) {
    *base  = &_tls_start;
    *index = _tls_index;
}

namespace {
    std::atomic<const PIMAGE_TLS_CALLBACK *> gGameTlsCallbacks {nullptr};
    PVOID gGameModule = nullptr;

    // Marks the threads that got an attach. A dynamic slot: the game's TLS lives in a static block, this DLL's or
    // the launcher's.
    DWORD gAttachedSlot = TLS_OUT_OF_INDEXES;

    void NTAPI ForwardGameTlsCallbacks(PVOID, DWORD reason, PVOID reserved) {
        const auto callbacks = gGameTlsCallbacks.load(std::memory_order_acquire);
        if ((reason != DLL_THREAD_ATTACH && reason != DLL_THREAD_DETACH) || !callbacks) {
            return;
        }
        // A thread already running when forwarding started never saw an attach, so it gets no detach either.
        if (reason == DLL_THREAD_DETACH && !TlsGetValue(gAttachedSlot)) {
            return;
        }
        TlsSetValue(gAttachedSlot, reason == DLL_THREAD_ATTACH ? reinterpret_cast<LPVOID>(1) : nullptr);
        // Walked on every call, as Windows does, so a callback the image adds later is reached too.
        for (auto callback = callbacks; *callback; ++callback) {
            (*callback)(gGameModule, reason, reserved);
        }
    }
} // namespace

// Without it the CRT's default entry point calls DisableThreadLibraryCalls, which only the static TLS above keeps
// from muting the thread notifications the relay below needs.
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}

// Windows calls a module's TLS callbacks on every thread only for images it loaded itself. The game was mapped by
// hand, so its callbacks ride on this DLL's.
#pragma section(".CRT$XLY", long, read)
extern "C" __declspec(allocate(".CRT$XLY")) const PIMAGE_TLS_CALLBACK CoreRT_GameTlsForwarder = ForwardGameTlsCallbacks;
#if defined(_M_IX86)
#pragma comment(linker, "/INCLUDE:__tls_used")
#pragma comment(linker, "/INCLUDE:_CoreRT_GameTlsForwarder")
#else
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:CoreRT_GameTlsForwarder")
#endif

// Starts relaying thread attach/detach to the game's null-terminated TLS callback array.
extern "C" __declspec(dllexport) BOOL CoreRT_ForwardGameTlsCallbacks(PVOID module, const PIMAGE_TLS_CALLBACK *callbacks) {
    if (!callbacks || gGameTlsCallbacks.load(std::memory_order_acquire)) {
        return callbacks != nullptr;
    }
    if ((gAttachedSlot = TlsAlloc()) == TLS_OUT_OF_INDEXES) {
        return FALSE;
    }
    gGameModule = module;
    gGameTlsCallbacks.store(callbacks, std::memory_order_release);
    return TRUE;
}
