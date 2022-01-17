/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Utils::Version {
    extern const char *gitLong;
    extern const char *git;
    extern const char *rel;

    extern bool VersionSatisfies(const char *a, const char *b);
};
