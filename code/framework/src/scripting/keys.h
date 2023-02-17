/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

namespace Framework::Scripting {
    enum Keys {
        KEY_VECTOR_2,
        KEY_VECTOR_3,
        KEY_QUATERNION,

        KEY_ENTITY,
        KEY_PLAYER,

        NUM_KEYS,
    };

    inline std::string GetKeyName(Keys k) {
        constexpr const char *keys[] = {
            "Vector2",
            "Vector3",
            "Quaternion",
            "Entity",
            "Player",
        };

        static_assert(sizeof(keys) / sizeof(keys[0]) == static_cast<size_t>(Keys::NUM_KEYS), "Keys enum is not complete");
        return keys[k];
    }
} // namespace Framework::Scripting
