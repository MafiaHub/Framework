#include "memory.h"

namespace Framework::Utils {
    void MarkMemoryRW(uint8_t *base) {
        auto *dos = (IMAGE_DOS_HEADER *)base;
        auto *nt  = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);

        DWORD old;
        VirtualProtect((LPVOID)base, nt->OptionalHeader.SizeOfImage, PAGE_EXECUTE_READWRITE, &old);
    }
} // namespace Framework::Utils