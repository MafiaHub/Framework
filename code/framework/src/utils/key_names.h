/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>
#include <vector>

// Friendly key name <-> Win32 virtual-key code, shared by everything that lets a player or a
// script name a key. Canonical names are lowercase and unspaced: "f5", "lshift", "mouse4".
namespace Framework::Utils::KeyNames {
    // -1 for an unsupported name. Case-insensitive.
    int ToVirtualKey(const std::string &name);

    // Empty for an unsupported key. Aliases ("return", "control", "num0") resolve back to the
    // preferred spelling.
    std::string FromVirtualKey(int virtualKey);

    // Every bindable key, deduplicated, for a settings UI polling to capture a press.
    const std::vector<int> &All();
} // namespace Framework::Utils::KeyNames
