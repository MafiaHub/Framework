#pragma once

#include <external/steam/wrapper.h>
#include <string>
#include <vector>

namespace Framework::Launcher {
    enum class ProjectPlatform { CLASSIC, STEAM };

    struct ProjectConfiguration {
        AppId_t steamAppId;
        std::string name;
        ProjectPlatform platform;
    };

    class Project {
      private:
        ProjectConfiguration _config;
        std::wstring _gamePath;
        External::Steam::Wrapper *_steamWrapper;

      public:
        Project(ProjectConfiguration &);
        ~Project();

        bool Run();

      private:
        bool EnsureFilesExists(const std::vector<std::string> &);

        bool RunInnerSteamChecks();
        bool RunInnerClassicChecks();

        bool RunInnerPreInit();
        bool RunInnerInit();
        bool RunInnerPostInit();

        bool Launch();
    };
} // namespace Framework::Launcher