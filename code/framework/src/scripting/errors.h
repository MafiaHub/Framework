/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Scripting {
    enum class ScriptingError {
        SCRIPTING_NONE,
        SCRIPTING_ENGINE_INIT_FAILED,
        SCRIPTING_SDK_INIT_FAILED,
        SCRIPTING_PLATFORM_INIT_FAILED
    };
} // namespace Framework::Scripting
