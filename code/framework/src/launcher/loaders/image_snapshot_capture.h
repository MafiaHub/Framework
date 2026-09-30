/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "image_snapshot.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Framework::Launcher::Loaders {
    /**
     * Starts the game once so its store authorises the run, then captures the code that run
     * decrypted into `snapshot`, and ends it.
     *
     * The only launcher code that enumerates, opens, reads and terminates another process, kept in
     * a unit of its own: a launcher links it only by naming it in
     * ProjectConfiguration::captureImageSnapshot, so every other launcher ships without those
     * imports and the malware-like profile they add.
     */
    bool CaptureImageSnapshot(ImageSnapshot &snapshot, const std::wstring &gamePath, const std::wstring &executableName, const std::vector<uint8_t> &sourceImage);
} // namespace Framework::Launcher::Loaders
