/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace Framework::Utils::Hashing {
    uint32_t CalculateCRC32(const char *data, size_t len);
} // namespace Framework::Utils::Hashing
