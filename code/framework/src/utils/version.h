/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

namespace Framework::Utils::Version {
    extern const char *gitLong;
    extern const char *git;
    extern const char *rel;

    // Major component of a MAJOR.MINOR.PATCH version, or the whole string when it carries no dot.
    inline std::string Major(const std::string &version) {
        const auto dot = version.find('.');
        return dot == std::string::npos ? version : version.substr(0, dot);
    }
} // namespace Framework::Utils::Version
