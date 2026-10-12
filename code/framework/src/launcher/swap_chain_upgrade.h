/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <Windows.h>

#include <string>

namespace Framework::Launcher {
    // Windows upgrades a game's windowed blt-model swap chain to the flip model ("Optimizations for
    // windowed games"), but only for the game's own executable: a game mapped into a launcher keeps the
    // legacy blt present, which some engines deadlock on. These stand in for the game's DXGI factory
    // imports and make its windowed swap chains flip-model, as the unmodified executable gets them.
    // Returns nullptr for any other export.
    void *ResolveFlipModelFactoryExport(HMODULE dxgi, const std::string &exportName);
} // namespace Framework::Launcher
