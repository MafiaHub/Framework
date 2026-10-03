/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "policy.h"

namespace Framework::Utils::StreamedAssets {
    bool Policy::HasLane(std::string_view lane) const {
        for (const std::string &known : GetLanes()) {
            if (known == lane) {
                return true;
            }
        }
        return false;
    }

    bool IsValidResourceName(std::string_view name) {
        if (name.empty() || name.size() > kMaxResourceName) {
            return false;
        }
        for (const char c : name) {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
                return false;
            }
        }
        return true;
    }

    bool IsSha256(std::string_view text) {
        if (text.size() != 64) {
            return false;
        }
        for (const char c : text) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return false;
            }
        }
        return true;
    }
} // namespace Framework::Utils::StreamedAssets
