#pragma once

#include <external/steam/wrapper.h>
#include <functional>
#include <string>
#include <vector>
#include <windows.h>

namespace Framework::Launcher {
    enum class ProjectPlatform { CLASSIC, STEAM };

    enum class ProjectArchitecture { CPU_X64, CPU_X86 };

    struct ProjectConfiguration {
        std::wstring executableName;
        std::wstring destinationDllName;
        std::wstring classicGamePath;
        std::string name;
        ProjectPlatform platform;
        ProjectArchitecture arch;
        AppId_t steamAppId;
        uintptr_t loadLimit = 0x140000000 + 0x130000000;
    };

    class Project {
      public:
        using FunctionResolverProc = std::function<LPVOID(HMODULE, const char *)>;
        using LibraryLoaderProc    = std::function<HMODULE(const char *)>;
        using PreLaunchProc        = std::function<void()>;

      private:
        ProjectConfiguration _config;
        std::wstring _gamePath;
        External::Steam::Wrapper *_steamWrapper;

        LibraryLoaderProc _libraryLoader;
        FunctionResolverProc _functionResolver;
        PreLaunchProc _preLaunchFunctor;

      public:
        Project(ProjectConfiguration &);
        ~Project() = default;

        bool Launch();

        inline void SetLibraryLoader(LibraryLoaderProc loader) {
            _libraryLoader = loader;
        }

        inline void SetFunctionResolver(FunctionResolverProc functionResolver) {
            _functionResolver = functionResolver;
        }

        inline void SetPreLaunchFunctor(PreLaunchProc preLaunchFunctor) {
            _preLaunchFunctor = preLaunchFunctor;
        }

      private:
        bool EnsureFilesExist(const std::vector<std::string> &);
        bool EnsureAtLeastOneFileExists(const std::vector<std::string> &);

        bool RunInnerSteamChecks();
        bool RunInnerClassicChecks();

        void InvokeEntryPoint(void (*entryPoint)());

        bool Run();
    };
} // namespace Framework::Launcher
