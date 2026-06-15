/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <unordered_map>

namespace Framework::Networking {
    extern std::unordered_map<uint8_t, const char *> StartupResultString;
    extern std::unordered_map<uint8_t, const char *> ConnectionAttemptString;
} // namespace Framework::Networking
