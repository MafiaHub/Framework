#pragma once
#include <external/steam/wrapper.h>
#include <string>

namespace Framework::Launcher::Entrypoint {
    static bool RunSteamGame(AppId_t appId);
    static bool RunDirectGame(const std::wstring &path);
    static bool Run(int argc, char **argv, std::string &);
}