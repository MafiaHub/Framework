#include "project.h"

#include <Windows.h>
#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>
#include <utils/hooking/hooking.h>

namespace Framework::Launcher {
    using PreInitFunction  = void (*)(LPVOID);
    using InitFunction     = void (*)(LPVOID);
    using PostInitFunction = void (*)(LPVOID);

    Project::Project(ProjectConfiguration &cfg): _config(cfg) {
        _steamWrapper = new External::Steam::Wrapper;
    }

    bool Project::Run() {
        // Run platform-dependant platform checks and init steps
        if (_config.platform == ProjectPlatform::STEAM) {
            if (!RunInnerSteamChecks()) {
                return false;
            }
        }
        else {
            if (!RunInnerClassicChecks()) {
                return false;
            }
        }

        // Add the required DLL directories to the current process
        auto addDllDirectory          = (decltype(&AddDllDirectory))GetProcAddress(GetModuleHandle("kernel32.dll"), "AddDllDirectory");
        auto setDefaultDllDirectories = (decltype(&SetDefaultDllDirectories))GetProcAddress(GetModuleHandle("kernel32.dll"), "SetDefaultDllDirectories");
        if (addDllDirectory && setDefaultDllDirectories) {
            setDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

            AddDllDirectory(_gamePath.c_str());
            AddDllDirectory(L"");
            AddDllDirectory(L"bin");
            SetCurrentDirectoryW(_gamePath.c_str());
        }

        // Load the steam runtime only if required
        if (_config.platform == ProjectPlatform::STEAM) {
            if (!LoadLibraryA("steam_api64.dll")) {
                MessageBox(nullptr, "Failed to inject the steam runtime DLL in the running process", _config.name.c_str(), MB_ICONERROR);
                return false;
            }
        }

        return true;
    }

    bool Project::RunInnerSteamChecks() {
        const std::vector<std::string> requiredFiles = {"steam_api64.dll"};
        // Make sure we have our required files
        if (!EnsureFilesExists(requiredFiles)) {
            return false;
        }

        // Initialize the steam wrapper
        if (!_steamWrapper->Init()) {
            MessageBox(nullptr, "Failed to init the bridge with steam, are you sure the Steam Client is running?", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        // Make sure steam has the game inside the library
        ISteamApps *steamApps = _steamWrapper->GetContext()->SteamApps();
        if (!steamApps->BIsAppInstalled(_config.steamAppId)) {
            MessageBox(nullptr, "The destination game is not installed", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        // Ask the game path from steam
        char gamePath[_MAX_PATH] = {0};
        steamApps->GetAppInstallDir(_config.steamAppId, gamePath, _MAX_PATH);

        std::string gameDirTmp = std::string(gamePath);
        _gamePath              = std::wstring(gameDirTmp.begin(), gameDirTmp.end());
        return true;
    }

    bool Project::RunInnerClassicChecks(){
        return true;
    }

    bool Project::RunInnerPreInit() {
        /*auto preInitFn = (PreInitFunction)(GetProcAddress((HMODULE)_appHandle, "PreInit"));
        if (!preInitFn) {
            return false;
        }*/

        // TODO: call with base address
        return true;
    }

    bool Project::RunInnerInit() {
        /*auto initFn = (InitFunction)(GetProcAddress((HMODULE)_appHandle, "Init"));
        if (!initFn) {
            return false;
        }*/

        // TODO: call with base address
        return true;
    }

    bool Project::RunInnerPostInit() {
        /*auto postInitFn = (PostInitFunction)(GetProcAddress((HMODULE)_appHandle, "PostInit"));
        if (!postInitFn) {
            return false;
        }*/

        // TODO: call with base address
        return true;
    }

    bool Project::EnsureFilesExists(const std::vector<std::string> &files) {
        for (const auto &file : files) {
            cppfs::FileHandle fh = cppfs::fs::open(file);
            if (!fh.exists() || !fh.isFile()) {
                MessageBox(nullptr, std::string("The file " + file + "is not present in the current directory").c_str(), "Framework", MB_ICONERROR);
                return false;
            }
        }
        return true;
    }
} // namespace Framework::Launcher