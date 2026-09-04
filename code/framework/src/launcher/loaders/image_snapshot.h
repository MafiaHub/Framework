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
     * Some stores ship a title whose code sections are encrypted on disk and only decrypted at
     * runtime, by a wrapper the store's own launcher has to authorise (the Rockstar Games Launcher
     * does this through MTLX.dll). A mapped image can never satisfy that wrapper - it refuses any
     * process the launcher did not itself start - so PE loading such a title needs the decrypted
     * code from somewhere else.
     *
     * This is that somewhere else: the executable sections are read once out of a run the store
     * authorised, cached next to the launcher, and laid back over the mapped image on later runs.
     * The cache is keyed by the executable's checksum, so a game update invalidates it and the
     * capture happens again. It only ever holds code the player's own installation decrypted on
     * the player's own machine.
     *
     * Modelled on the executable snapshot FiveM uses for the same class of wrapper.
     */
    class ImageSnapshot final {
      public:
        ImageSnapshot(std::filesystem::path cachePath, uint32_t executableChecksum);

        // A cached snapshot for exactly this executable exists and can be applied.
        bool IsAvailable() const;

        // Lay the cached sections back over an image mapped at `module`. The sections must already
        // be mapped (this overwrites them), and imports must not be resolved yet.
        bool Apply(HMODULE module) const;

        /**
         * Read the executable sections out of a running, store-authorised game and cache them.
         *
         * `sourceImage` is the game executable on disk, used both to decide which sections to read
         * and to tell a decrypted section from one the wrapper has not touched yet. Returns false
         * while the wrapper still has not decrypted, so the caller can keep polling.
         */
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
