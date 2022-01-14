/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

namespace Framework::Scripting {
    enum class Keys {
        KEY_VECTOR_2,
        KEY_VECTOR_3,
        KEY_QUATERNION,
    };

    const inline std::string GetKeyName(Keys k) {
        switch (k) {
        case Keys::KEY_VECTOR_2: return std::string("Vector2");

        case Keys::KEY_VECTOR_3: return std::string("Vector3");

        case Keys::KEY_QUATERNION: return std::string("Quaternion");

        default: return std::string("Unknown");
        }
    }
} // namespace Framework::Scripting
