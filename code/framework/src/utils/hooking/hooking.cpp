/*
 * MafiaHub OSS license
 * Copyright (c) 2020, CitizenFX
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "hooking.h"

namespace hook {
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
        return ptr;
    }
#endif

    ptrdiff_t baseAddressDifference;
}; // namespace hook
