#pragma once

#include <Windows.h>
#include <stdint.h>

namespace Framework::Utils {
    void MarkMemoryRW(uint8_t *base);
}