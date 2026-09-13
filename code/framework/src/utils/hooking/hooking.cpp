/*
 * MafiaHub OSS license
 * Copyright (c) 2020, CitizenFX
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// Must precede hooking.h: that pulls in <windows.h>, which drags in the legacy winsock.h
// and then collides with the WinSock2.h safe_win32.h wants.
#include <utils/safe_win32.h>

#include "hooking.h"

namespace hook {
    void set_preferred_image_end(uintptr_t base) {
        if (!base) {
            return;
        }

        const auto *dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            return;
        }

        const auto *ntHeader = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dosHeader->e_lfanew);
        if (ntHeader->Signature != IMAGE_NT_SIGNATURE) {
            return;
        }

        preferredImageEnd = kPreferredImageBase + ntHeader->OptionalHeader.SizeOfImage;
    }

#ifndef _M_AMD64
    void inject_hook::inject() {
        inject_hook_frontend fe(this);
        m_assembly = std::make_shared<FunctionAssembly>(fe);

        put<uint8_t>(m_address, 0xE9);
        put<int>(m_address + 1, (uintptr_t)m_assembly->GetCode() - (uintptr_t)get_adjusted(m_address) - 5);
    }

    void inject_hook::injectCall() {
        inject_hook_frontend fe(this);
        m_assembly = std::make_shared<FunctionAssembly>(fe);

        put<uint8_t>(m_address, 0xE8);
        put<int>(m_address + 1, (uintptr_t)m_assembly->GetCode() - (uintptr_t)get_adjusted(m_address) - 5);
    }
#else
    void *AllocateFunctionStub(void *ptr, int type) {
        (void)type;
        return ptr;
    }
#endif

    ptrdiff_t baseAddressDifference;

    // Conservative default for a process that never called set_base(): the historical
    // base + 96 MB window, so behaviour is unchanged until set_base() widens it.
    uintptr_t preferredImageEnd = kPreferredImageBase + 0x6000000;
} // namespace hook
