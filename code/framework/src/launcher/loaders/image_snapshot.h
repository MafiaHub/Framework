/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Framework::Launcher::Loaders {
    /**
     * Some stores ship a title whose code is encrypted on disk and only decrypted at runtime, by a
     * wrapper that authorises no process their launcher did not start (the Rockstar Games Launcher
     * does this through MTLX.dll). A mapped image can never satisfy that wrapper, so the decrypted
     * code is captured once from a run the store did authorise and replayed afterwards.
     *
     * Keyed by the executable's checksum, so a game update invalidates the cache. Modelled on the
     * executable snapshot FiveM uses for the same class of wrapper.
     */
    class ImageSnapshot final {
      public:
        ImageSnapshot(std::filesystem::path cachePath, uint32_t executableChecksum);

        bool IsAvailable() const;

        // Sections must already be mapped and imports not yet resolved.
        bool Apply(HMODULE module) const;

        // False while the wrapper has not finished decrypting, so the caller can keep polling.
        bool CaptureFrom(HANDLE process, const std::vector<uint8_t> &sourceImage);

      private:
        struct Section {
            uint32_t virtualAddress = 0;
            std::vector<uint8_t> data;
        };

        std::filesystem::path _cachePath;
        uint32_t _executableChecksum = 0;
        mutable std::vector<Section> _sections;
        mutable bool _loaded = false;

        bool Load() const;
        bool Store() const;
    };
} // namespace Framework::Launcher::Loaders
