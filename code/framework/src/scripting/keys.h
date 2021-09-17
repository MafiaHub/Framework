#pragma once

#include <string>

namespace Framework::Scripting {
    enum Keys {
        KEY_VECTOR_2,
        KEY_VECTOR_3,
        KEY_QUATERNION,
    };

    const std::string GetKeyName(Keys k) {
        switch (k) {
        case Keys::KEY_VECTOR_2: return std::string("Vector2");

        case Keys::KEY_VECTOR_3: return std::string("Vector3");

        case Keys::KEY_QUATERNION: return std::string("Quaternion");

        default: return std::string("Unknown");
        }
    }
} // namespace Framework::Scripting
