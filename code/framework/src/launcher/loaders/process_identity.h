/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

namespace Framework::Launcher::Loaders {
    /**
     * Rewrites the loader data so the process reports itself as the mapped game
     * executable instead of the launcher.
     *
     * The import-table redirects only reach the mapped image; rewriting the loader
     * data answers the query at its source, so modules the game loads later see the
     * game path too. Does not cover GetCommandLineW/A, which kernel32 caches at init.
     *
     * See docs/cryengine_games.md.
     */
    bool ApplyMappedImageIdentity(const std::wstring &imagePath);
} // namespace Framework::Launcher::Loaders
