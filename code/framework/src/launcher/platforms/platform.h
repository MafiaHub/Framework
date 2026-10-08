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
#include <span>
#include <string>
#include <vector>

namespace Framework::Launcher {
    struct ProjectConfiguration;

    // UNAVAILABLE: the platform could not resolve the game, the next one in the list can still recover.
    // HANDED_OFF: the launch goes on in another process this one started, which has nothing left to do.
    enum class PlatformCheckStatus {
        OK,
        ABORT,
        UNAVAILABLE,
        HANDED_OFF
    };

    struct PlatformResolution {
        std::wstring gameRoot; // the game's root folder, the work dir is resolved from it
        bool manual = false;   // the player picked it: remembered, and it wins over every store later on
    };

    // What the launcher offers a platform while it resolves and starts the game
    class PlatformHost {
      public:
        virtual ~PlatformHost() = default;

        virtual const ProjectConfiguration &GetConfig() const = 0;

        // The launcher's own folder, and its executable
        virtual const std::filesystem::path &GetProjectPath() const = 0;
        virtual std::wstring GetLauncherExecutablePath() const = 0;

        // The game layouts: whether a game root holds the executable, and where it does
        virtual bool GameExecutableExistsIn(const std::wstring &gameRoot) const = 0;
        virtual std::wstring GetGameWorkDir(const std::wstring &gameRoot) const = 0;
        virtual std::vector<std::wstring> GetAlternativeWorkDirCandidates() const = 0;

        // Sets a variable for this process and the game's CRT alike
        virtual void SetProcessVariable(const wchar_t *name, const std::wstring &value) const = 0;

        // Logs why `platform` could not resolve the game, and tells the player when `reportErrors`
        virtual PlatformCheckStatus ReportUnavailable(const char *platform, const std::string &reason, bool reportErrors) const = 0;
        virtual void ReportError(const std::string &message) const = 0;
    };

    // Where the game comes from: a store, or a folder the player selects. A launcher lists the ones
    // it supports in ProjectConfiguration::platforms, and the first that resolves the game starts it.
    class Platform {
      public:
        virtual ~Platform() = default;

        virtual const char *GetName() const = 0;

        // Finds the game. Only the last platform in the list reports its failure to the player.
        virtual PlatformCheckStatus Resolve(const PlatformHost &host, PlatformResolution &resolution, bool reportErrors) = 0;

        // The game root the player picked may still be this platform's copy; returning true makes this
        // the platform the game starts with (it may point `resolution` at its own copy).
        virtual bool AdoptManualCopy(const PlatformHost &host, PlatformResolution &resolution) {
            return false;
        }

        // The folder the player selects rather than a store: its remembered pick is tried first
        virtual bool IsManualSelection() const {
            return false;
        }

        // In-process preparation once the DLL search paths are in place, before the game is mapped
        virtual bool PrepareLaunch(const PlatformHost &host) {
            return true;
        }

        // PE loading: the executable's file bytes before mapping, its sections once mapped (code
        // that is not on disk can be put back there), and the entry point to enter it at
        virtual bool PrepareImage(const PlatformHost &host, const std::wstring &executablePath, std::span<const uint8_t> image) {
            return true;
        }
        virtual void OnSectionsMapped(HMODULE module) {}
        virtual uintptr_t ResolveEntryPoint(uintptr_t imageBase, uintptr_t entryPoint) {
            return entryPoint;
        }
    };
} // namespace Framework::Launcher
