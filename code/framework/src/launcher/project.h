/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "platforms/platforms.h"
#include "utils/config.h"
#include "utils/minidump.h"

#include <Windows.h>

#include <function2/function2.hpp>
#include <string>
#include <utility>
#include <vector>

namespace Framework::Launcher {
    enum class ProjectLaunchType {
        PE_LOADING,
#ifdef FW_DLL_INJECTION
        DLL_INJECTION
#endif
    };

#ifdef FW_DLL_INJECTION
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
#endif

    struct ProjectConfiguration {
        std::wstring executableName;
        std::wstring destinationDllName;
        std::wstring classicGamePath;
        std::string name;

        // Where the game may come from (Platforms::Steam, Epic, Rockstar, MicrosoftStore, Classic),
        // tried in this order until one resolves it. A store that cannot (the game is not in that
        // library, its client is not running) hands over to the next entry; only the last one
        // reports its failure to the player. Put a Classic last to let the player select the game
        // themselves when no store has it: that pick is remembered in the launcher's JSON config and
        // wins over every store on later runs.
        std::vector<std::shared_ptr<Platform>> platforms = {std::make_shared<Platforms::Classic>()};

        ProjectLaunchType launchType = ProjectLaunchType::PE_LOADING;
        uintptr_t loadLimit          = SIZE_MAX;

        // allows us to load client ourselves, otherwise stick to Framework's standard loading routine
        bool loadClientManually = false;

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

        // other layouts the same game ships under, such as another store's build. A game root
        // resolves to the first of alternativeWorkDir and these that holds the executable.
        std::vector<std::wstring> alternativeWorkDirFallbacks;

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

        // Custom URL scheme deep link, so a Join button on a server's website reaches the game. When
        // set, every launch claims the scheme for the current user under
        // HKCU\Software\Classes\<urlProtocolScheme> and points it at this launcher - no elevation,
        // nothing written for any other account - then extracts a <urlProtocolScheme>:// argument
        // from its own command line and hands it to Instance::OnProtocolLaunch.
        //
        // That claim is the only state the framework keeps outside its own folder. A mod that would
        // rather ask the player first leaves this empty and calls Utils::UrlProtocol::Register /
        // Unregister itself; extraction still works for a link passed on the command line.
        std::wstring urlProtocolScheme; // e.g. L"mafiamp" (no "://")
    };

    class Project final: private PlatformHost {
      public:
        using FunctionResolverProc = fu2::function<LPVOID(HMODULE, const char *) const>;
        using LibraryLoaderProc    = fu2::function<HMODULE(const char *) const>;
        using PreLaunchProc        = fu2::function<void() const>;

      private:
        ProjectConfiguration _config;
        std::unique_ptr<Utils::Config> _fileConfig;
        std::wstring _gamePath;
        bool _manualGamePath = false;
        std::shared_ptr<Platform> _platform; // the one that resolved the game
        std::filesystem::path _projectPath;
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

        PlatformCheckStatus RunPlatformChecks();

        // PlatformHost
        const ProjectConfiguration &GetConfig() const override {
            return _config;
        }
        const std::filesystem::path &GetProjectPath() const override {
            return _projectPath;
        }
        std::wstring GetLauncherExecutablePath() const override;
        bool GameExecutableExistsIn(const std::wstring &gameRoot) const override;
        std::wstring GetGameWorkDir(const std::wstring &gameRoot) const override;
        std::vector<std::wstring> GetAlternativeWorkDirCandidates() const override;
        void SetProcessVariable(const wchar_t *name, const std::wstring &value) const override;
        PlatformCheckStatus ReportUnavailable(const char *platform, const std::string &reason, bool reportErrors) const override;
        void ReportError(const std::string &message) const override;

        // Claims urlProtocolScheme for the current user and points it at this launcher.
        void RegisterUrlProtocolScheme() const;

        // Extracts a launch URL from the command line into the environment for the client.
        void HandleUrlProtocolLaunch();

        bool LoadJSONConfig();
        void SaveJSONConfig() const;

        static void InvokeEntryPoint(void (*entryPoint)());

        void AllocateDeveloperConsole() const;

        bool RunWithPELoading();
#ifdef FW_DLL_INJECTION
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
#endif
    };
} // namespace Framework::Launcher
