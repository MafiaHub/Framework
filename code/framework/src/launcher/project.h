#pragma once

#include <external/steam/wrapper.h>
#include <string>
#include <vector>

namespace Framework::Launcher {
    enum class ProjectPlatform { CLASSIC, STEAM };

    enum class ProjectArchitecture { CPU_X64, CPU_X86 };

    struct ProjectConfiguration {
        std::wstring executableName;
        std::wstring destinationDllName;
        std::string name;
        ProjectPlatform platform;
        ProjectArchitecture arch;
        AppId_t steamAppId;
    };

    class Project {
      private:
        ProjectConfiguration _config;
        std::wstring _gamePath;
        External::Steam::Wrapper *_steamWrapper;

      public:
        Project(ProjectConfiguration &);
        ~Project() = default;

        bool Launch();

      private:
        bool EnsureFilesExist(const std::vector<std::string> &);
        bool EnsureAtLeastOneFileExists(const std::vector<std::string> &);

        bool RunInnerSteamChecks();
        bool RunInnerClassicChecks();

        void InvokeEntryPoint(void (*entryPoint)());

        bool Run();
    };
} // namespace Framework::Launcher
