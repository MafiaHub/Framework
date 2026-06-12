/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "safe_win32.h"

namespace Framework::Utils {
    // True once the OS has begun tearing the process down (CRT/DLL detach).
    // Blocking work (thread joins, GPU fences, CefShutdown) must be skipped
    // then: the threads it would wait on are already dead and the wait only
    // stalls process exit.
    inline bool IsProcessShutdownInProgress() {
#ifdef WIN32
        using RtlDllShutdownInProgress_t = BOOLEAN(NTAPI *)();
        static const auto fn             = reinterpret_cast<RtlDllShutdownInProgress_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlDllShutdownInProgress"));
        return fn && fn();
#else
        return false;
#endif
    }
} // namespace Framework::Utils
