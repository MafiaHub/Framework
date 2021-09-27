#include "entrypoint.h"
#include "project.h"

namespace Framework::Launcher::Entrypoint {
    bool RunSteamGame(AppId_t appId) {
        return true;
    }

    bool RunDirectGame(const std::wstring &path) {
        return true;
    }

    bool Run(int argc, char **argv, std::string &handleName) {
        // Initialize the project instance
        Framework::Launcher::Project project(handleName);

        // Create the loader instance
        // TODO: implement

        // If loader is successfull, call the init methods of the project
        // TODO: implement
    }
} // namespace Framework::Launcher::Entrypoint