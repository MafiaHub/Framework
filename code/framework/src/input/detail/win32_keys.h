/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../physical_keys.h"

namespace Framework::Input::detail {
    inline bool IsForeground() {
        DWORD pid = 0;
        ::GetWindowThreadProcessId(::GetForegroundWindow(), &pid);
        return pid == ::GetCurrentProcessId();
    }

    inline bool ReadWin32Key(int key) {
        DWORD pid          = 0;
        const DWORD thread = ::GetWindowThreadProcessId(::GetForegroundWindow(), &pid);
        if (pid != ::GetCurrentProcessId()) {
            return false;
        }
        const UINT code = PhysicalKeys::ToLayoutVirtualKey(static_cast<UINT>(key), ::GetKeyboardLayout(thread));
        return code != 0 && (::GetAsyncKeyState(static_cast<int>(code)) & 0x8000) != 0;
    }
} // namespace Framework::Input::detail
