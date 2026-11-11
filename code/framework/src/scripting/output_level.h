/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "script_engine.h"

#include <logging/logger.h>

namespace Framework::Scripting {
    // The level of the scripting logger a line of a script's output is written at.
    inline spdlog::level::level_enum LogLevelOf(OutputLevel level) {
        switch (level) {
        case OutputLevel::Debug: return spdlog::level::debug;
        case OutputLevel::Warning: return spdlog::level::warn;
        case OutputLevel::Error: return spdlog::level::err;
        case OutputLevel::Info: break;
        }
        return spdlog::level::info;
    }
} // namespace Framework::Scripting
