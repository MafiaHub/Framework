/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstddef>
#include <string>

namespace hook {
    // Seeds the pattern hint map from a table produced by scripts/build_pattern_table.py, so a
    // project ships its resolved addresses instead of scanning the game image for each pattern.
    //
    // Returns how many addresses were seeded, or zero when the table is absent, malformed, or
    // was built against a different game image. Every seeded address is still verified against
    // the bytes in the loaded image before it is used and anything that fails falls back to a
    // scan, so an unusable table only costs the scan it was meant to save.
    size_t load_pattern_table(const std::string &path);
} // namespace hook
