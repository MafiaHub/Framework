/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once


#include "utils/minidump.h"
#include "utils/config.h"
#include <external/steam/wrapper.h>

#include <Windows.h>

#include <function2.hpp>
#include <string>
#include <utility>
#include <vector>

namespace Framework::Launcher {
    enum class ProjectPlatform { CLASSIC, STEAM };

    struct ProjectConfiguration {
        using DialogPromptSelectorProc = fu2::function<std::wstring(std::wstring gameExePath) const>;

        std::wstring executableName;
        std::wstring destinationDllName;
        std::wstring classicGamePath;
        std::string name;
        ProjectPlatform platform;
        AppId_t steamAppId  = 430;
        uintptr_t loadLimit = SIZE_MAX;

        // allows us to load client ourselves, otherwise stick to Framework's standard loading routine
        bool loadClientManually = false;

        // if promptForGameExe is true, and steam dll is found in the game's library, switch to steam platform
        bool preferSteam = false;

        // game exe integrity checks (uses CRC32 checksum)
        bool verifyGameIntegrity = false;
        std::vector<uint32_t> supportedGameVersions;

        // additional DLL search paths
        std::vector<std::wstring> additionalSearchPaths;

        // alternative game working directory
        bool useAlternativeWorkDir = false; // Uses the game's root directory by default
        std::wstring alternativeWorkDir;

        // prompt for game exe (only if CLASSIC platform is set)
        bool promptForGameExe        = false;
        std::string promptTitle      = "Select your game's executable";
        std::string promptFilter     = "Game.exe";
        std::string promptFilterName = "Your Game.exe";
        std::string promptExtension  = "*.exe";
        DialogPromptSelectorProc promptSelectionFunctor;

        // JSON config project settings
        bool disablePersistentConfig = false;
        bool overrideConfigFileName  = false; // Uses <config.name>_launcher.json by default
        std::string configFileName   = "launcher.json";

        // Console allocation
        bool allocateDeveloperConsole      = false;
        std::wstring developerConsoleTitle = L"framework: dev-console";
    };

    class Project {
      public:
        using FunctionResolverProc = fu2::function<LPVOID(HMODULE, const char *) const>;
        using LibraryLoaderProc    = fu2::function<HMODULE(const char *) const>;
        using PreLaunchProc        = fu2::function<void() const>;

      private:
        ProjectConfiguration _config;
        std::unique_ptr<Utils::Config> _fileConfig;
        std::wstring _gamePath;
        std::unique_ptr<External::Steam::Wrapper> _steamWrapper;
        std::unique_ptr<Utils::MiniDump> _minidump;

        LibraryLoaderProc _libraryLoader;
        FunctionResolverProc _functionResolver;
        PreLaunchProc _preLaunchFunctor;

      public:
        explicit Project(ProjectConfiguration &);
        ~Project() = default;

        bool Launch();

        inline void SetLibraryLoader(LibraryLoaderProc loader) {
            _libraryLoader = std::move(loader);
        }

        inline void SetFunctionResolver(FunctionResolverProc functionResolver) {
            _functionResolver = std::move(functionResolver);
        }

        inline void SetPreLaunchFunctor(PreLaunchProc preLaunchFunctor) {
            _preLaunchFunctor = std::move(preLaunchFunctor);
        }

        ProjectConfiguration &GetConfig() {
            return _config;
        }

        static void InitialiseClientDLL();

      private:
        static bool EnsureFilesExist(const std::vector<std::string> &);
        static bool EnsureAtLeastOneFileExists(const std::vector<std::string> &);
        bool EnsureGameExecutableIsCompatible();

        bool RunInnerSteamChecks();
        bool RunInnerClassicChecks();

        bool LoadJSONConfig();
        void SaveJSONConfig();

        static void InvokeEntryPoint(void (*entryPoint)());

        void AllocateDeveloperConsole() const;

        bool Run();
    };
} // namespace Framework::Launcher
