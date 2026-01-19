/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <external/steam/wrapper.h>

#include <function2.hpp>
#include <string>
#include <vector>

namespace Framework::Launcher {
    enum class ProjectPlatform {
        CLASSIC,
        STEAM
    };

    enum class ProjectLaunchType {
        PE_LOADING,
        DLL_INJECTION
    };

    enum DLLInjectionResults {
        INJECT_LIBRARY_RESULT_OK,

        INJECT_LIBRARY_GET_MODULE_HANDLE_FAILED,
        INJECT_LIBRARY_GET_PROC_ADDRESS_FAILED,

        INJECT_LIBRARY_RESULT_WRITE_FAILED,
        INJECT_LIBRARY_GET_RETURN_CODE_FAILED,
        INJECT_LIBRARY_LOAD_LIBRARY_FAILED,
        INJECT_LIBRARY_THREAD_CREATION_FAILED,

        INJECT_LIBRARY_OPEN_PROCESS_FAIL
    };

    struct ProjectConfiguration {
        using DialogPromptSelectorProc = fu2::function<std::wstring(std::wstring gameExePath) const>;

        std::wstring executableName;
        std::wstring destinationDllName;
        std::wstring classicGamePath;
        std::string name;
        ProjectPlatform platform;
        ProjectLaunchType launchType = ProjectLaunchType::PE_LOADING;
        AppId_t steamAppId           = 430;
        uintptr_t loadLimit          = SIZE_MAX;

        // allows us to load client ourselves, otherwise stick to Framework's standard loading routine
        bool loadClientManually = false;

        // if promptForGameExe is true, and steam dll is found in the game's library, switch to steam platform
        bool preferSteam = false;

        // game exe integrity checks (uses CRC32 checksum)
        bool verifyGameIntegrity = false;
        std::vector<uint32_t> supportedGameVersions;

        // additional DLL search paths
        std::vector<std::wstring> additionalSearchPaths;

        // Additional arguments
        std::wstring additionalLaunchArguments = L"";

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
} // namespace Framework::Launcher
