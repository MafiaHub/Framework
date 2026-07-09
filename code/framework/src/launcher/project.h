/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/config.h"
#include "utils/minidump.h"
#include <external/steam/wrapper.h>

#include <Windows.h>

#include <function2.hpp>
#include <string>
#include <utility>
#include <vector>

namespace Framework::Launcher {
    enum class ProjectPlatform {
        CLASSIC,
        STEAM,
        EPIC
    };
    enum class ProjectLaunchType {
        PE_LOADING,
        DLL_INJECTION
    };

    enum class DLLInjectionResult {
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

        // EPIC platform: Epic catalog id ("AppName") of the destination game. Optional — when
        // empty the Epic manifest is matched by the launch executable's file name instead.
        std::wstring epicAppName;

        // game exe integrity checks (uses CRC32 checksum)
        bool verifyGameIntegrity = false;
        std::vector<uint32_t> supportedGameVersions;

        // additional DLL search paths, resolved relative to the game directory
        std::vector<std::wstring> additionalSearchPaths;

        // absolute DLL search dirs added verbatim via AddDllDirectory; for runtimes
        // outside the game tree whose deps are only reachable via PATH
        std::vector<std::wstring> additionalDllDirectories;

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

        // TLS handling for PE loading
        // When true, copies game TLS directly to slot 0 (requires sacrificial TLS buffer in launcher EXE)
        // When false, uses framework's allocated TLS slot (traditional approach)
        bool useDirectTlsSlot0 = false;

        // Some launchers already own ucrtbase's process-wide EXE TLS-destructor slot.
        // Suppress the mapped game's second registration when that would abort startup.
        bool suppressThreadLocalExeAtexitCallback = false;

        // Custom URL scheme deep link. When set, the launcher extracts a <urlProtocolScheme>://
        // argument from its command line and passes it to Instance::OnProtocolLaunch. Registering the
        // scheme with the OS is the mod's responsibility.
        std::wstring urlProtocolScheme; // e.g. L"mafiamp" (no "://")
    };

    class Project final {
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
        bool EnsureGameExecutableIsCompatible(uint32_t);
        uint32_t GetGameVersion() const;

        bool RunInnerSteamChecks();
        bool RunInnerEpicChecks();
        bool RunInnerClassicChecks();

        // Extracts a launch URL from the command line into the environment for the client.
        void HandleUrlProtocolLaunch();

        bool LoadJSONConfig();
        void SaveJSONConfig() const;

        static void InvokeEntryPoint(void (*entryPoint)());

        void AllocateDeveloperConsole() const;

        bool RunWithPELoading();
        bool RunWithDLLInjection();

        const char *InjectLibraryResultToString(const DLLInjectionResult result) {
            switch (result) {
            case DLLInjectionResult::INJECT_LIBRARY_RESULT_OK: return "Ok";
            case DLLInjectionResult::INJECT_LIBRARY_RESULT_WRITE_FAILED: return "Failed to write memory into process";
            case DLLInjectionResult::INJECT_LIBRARY_GET_RETURN_CODE_FAILED: return "Failed to get return code of the load call";
            case DLLInjectionResult::INJECT_LIBRARY_LOAD_LIBRARY_FAILED: return "Failed to load library";
            case DLLInjectionResult::INJECT_LIBRARY_THREAD_CREATION_FAILED: return "Failed to create remote thread";
            case DLLInjectionResult::INJECT_LIBRARY_OPEN_PROCESS_FAIL: return "Open of the process failed";
            default: return "Unknown error";
            }
        }
    };
} // namespace Framework::Launcher
