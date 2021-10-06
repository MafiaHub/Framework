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
    };

    class Project {
      public:
        using FunctionResolverProc = std::function<LPVOID(HMODULE, const char *)>;
        using LibraryLoaderProc    = std::function<HMODULE(const char *)>;

      private:
        ProjectConfiguration _config;
        std::wstring _gamePath;
        External::Steam::Wrapper *_steamWrapper;
        uintptr_t _loadLimit;

        LibraryLoaderProc _libraryLoader;
        FunctionResolverProc _functionResolver;

      public:
        Project(ProjectConfiguration &);
        ~Project() = default;

        bool Launch();

        inline void SetLoadLimit(uintptr_t loadLimit) {
            _loadLimit = loadLimit;
        }

        inline void SetLibraryLoader(LibraryLoaderProc loader) {
            _libraryLoader = loader;
        }

        inline void SetFunctionResolver(FunctionResolverProc functionResolver) {
            _functionResolver = functionResolver;
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
