#pragma once

#include <cstdint>

namespace Framework::Utils::Hashing {
    uint32_t CalculateCRC32(const char *data, size_t len);
} // namespace Framework::Utils::Hashing
