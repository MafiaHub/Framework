/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "memory.h"

namespace Framework::Utils {
    void MarkMemoryRW(uint8_t *base) {
        const auto *dos = (IMAGE_DOS_HEADER *)base;
        const auto *nt  = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);

        DWORD old;
        VirtualProtect(base, nt->OptionalHeader.SizeOfImage, PAGE_EXECUTE_READWRITE, &old);
    }
} // namespace Framework::Utils
