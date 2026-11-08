/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// Identity values shared by the admission and gate tests.
namespace TestIdentities {
    // A well-formed Epic account id: 32 lowercase hex chars, what the sanitizer keeps.
    inline constexpr const char *kEpicId = "0123456789abcdef0123456789abcdef";
} // namespace TestIdentities
