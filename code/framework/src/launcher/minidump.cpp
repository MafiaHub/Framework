/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "minidump.h"
#include "utils/safe_win32.h"
#include <winnt.h>

typedef void(__cdecl *CoreSetExceptionOverride)(LONG (*handler)(EXCEPTION_POINTERS *));

namespace Framework::Launcher {

    static LONG WINAPI MiniDumpExceptionHandler(EXCEPTION_POINTERS *exceptionInfo) {
        return true;
    }

    void MiniDump::InitExceptionOverride() {
        const auto coreSetExceptionOverride = reinterpret_cast<CoreSetExceptionOverride>(GetProcAddress(GetModuleHandleA("FrameworkLoaderData.dll"), "CoreSetExceptionOverride"));
        if (coreSetExceptionOverride) {
            coreSetExceptionOverride(MiniDumpExceptionHandler);
        }
    }

    MiniDump::MiniDump() {
        InitExceptionOverride();
    }
};
