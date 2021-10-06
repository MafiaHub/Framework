/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "project.h"

#include "loaders/exe_ldr.h"
#include "utils/hashing.h"
#include "utils/string_utils.h"

#include <ShellScalingApi.h>
#include <Windows.h>
#include <cppfs/FileHandle.h>
#include <cppfs/FilePath.h>
#include <cppfs/fs.h>
#include <fmt/core.h>
#include <fstream>
#include <functional>
#include <ostream>
#include <psapi.h>
#include <utils/hooking/hooking.h>

// Fix for gpu-enabled games
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement        = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// allocate space for game
#ifdef _M_AMD64
#pragma bss_seg(".fwgame")
char fwgame_seg[0x13000000];
#else
#pragma bss_seg(".fwgame")
char fwgame_seg[0x1400000];
#endif

static const wchar_t *gImagePath;
static const wchar_t *gDllName;
HMODULE tlsDll {};

static LONG NTAPI HandleVariant(PEXCEPTION_POINTERS exceptionInfo) {
    return (exceptionInfo->ExceptionRecord->ExceptionCode == STATUS_INVALID_HANDLE) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

void WINAPI GetStartupInfoW_Stub(LPSTARTUPINFOW lpStartupInfo) {
    static bool init = false;

    if (!init) {
        auto mod = LoadLibraryW(gDllName);

        if (mod) {
            auto init = reinterpret_cast<void *(*)()>(GetProcAddress(mod, "InitClient"));
            if (init) {
                init();
            }
        }
        init = true;
    }

    return GetStartupInfoW(lpStartupInfo);
}

DWORD WINAPI GetModuleFileNameA_Hook(HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    if (!hModule || hModule == GetModuleHandle(nullptr)) {
        auto gamePath = Framework::Utils::StringUtils::WideToNormalDirect(gImagePath);
        strcpy_s(lpFilename, nSize, gamePath.c_str());

        return (DWORD)gamePath.size();
    }

    return GetModuleFileNameA(hModule, lpFilename, nSize);
}

DWORD WINAPI GetModuleFileNameExA_Hook(HANDLE hProcess, HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    if (!hModule || hModule == GetModuleHandle(nullptr)) {
        auto gamePath = Framework::Utils::StringUtils::WideToNormalDirect(gImagePath);
        strcpy_s(lpFilename, nSize, gamePath.c_str());

        return (DWORD)gamePath.size();
    }

    return GetModuleFileNameExA(hProcess, hModule, lpFilename, nSize);
}

DWORD WINAPI GetModuleFileNameW_Hook(HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    if (!hModule || hModule == GetModuleHandle(nullptr)) {
        wcscpy_s(lpFilename, nSize, gImagePath);

        return (DWORD)wcslen(gImagePath);
    }

    return GetModuleFileNameW(hModule, lpFilename, nSize);
}

DWORD WINAPI GetModuleFileNameExW_Hook(HANDLE hProcess, HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    if (!hModule || hModule == GetModuleHandle(nullptr)) {
        wcscpy_s(lpFilename, nSize, gImagePath);

        return (DWORD)wcslen(gImagePath);
    }

    return GetModuleFileNameExW(hProcess, hModule, lpFilename, nSize);
}

namespace Framework::Launcher {
    Project::Project(ProjectConfiguration &cfg): _config(cfg) {
        _steamWrapper = new External::Steam::Wrapper;
        gDllName      = _config.destinationDllName.c_str();
    }

    bool Project::Launch() {
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

        // Load the destination DLL
        if (!LoadLibraryW(_config.destinationDllName.c_str())) {
            MessageBox(nullptr, "Failed to load core runtime", _config.name.c_str(), MB_ICONERROR);
            return 0;
        }

        // Fetch the current working directory
        static wchar_t projectDllPath[32768];
        GetCurrentDirectoryW(32768, projectDllPath);

        // Add the required DLL directories to the current process
        auto addDllDirectory          = (decltype(&AddDllDirectory))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "AddDllDirectory");
        auto setDefaultDllDirectories = (decltype(&SetDefaultDllDirectories))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetDefaultDllDirectories");
        if (addDllDirectory && setDefaultDllDirectories) {
            setDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

            AddDllDirectory(_gamePath.c_str());
            AddDllDirectory(projectDllPath);
            AddDllDirectory((std::wstring(projectDllPath) + L"\\bin").c_str());

            // add any custom search paths from the mod
            for (auto &path : _config.additionalSearchPaths) { AddDllDirectory(path.c_str()); }

            SetCurrentDirectoryW(_gamePath.c_str());
        }

        // Load TLS dummy so the game can use thread-local storage
        if (!(tlsDll = LoadLibraryW(L"FrameworkLoaderData.dll"))) {
            MessageBox(nullptr, "Failed to load a vital framework component", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        // Load the steam runtime only if required
        if (_config.platform == ProjectPlatform::STEAM) {
            HMODULE steamDll {};

            if (_config.arch == ProjectArchitecture::CPU_X64) {
                steamDll = LoadLibraryW(L"steam_api64.dll");
            }
            else {
                steamDll = LoadLibraryW(L"steam_api.dll");
            }

            if (!steamDll) {
                MessageBox(nullptr, "Failed to inject the steam runtime DLL in the running process", _config.name.c_str(), MB_ICONERROR);
                return false;
            }
        }

        // Use real scaling
        auto shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) {
            auto SetProcessDpiAwareness = (decltype(&::SetProcessDpiAwareness))GetProcAddress(shcore, "SetProcessDpiAwareness");

            if (SetProcessDpiAwareness) {
                SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
            }
        }

        // handle path variable
        {
            static wchar_t pathBuf[32768];
            GetEnvironmentVariableW(L"PATH", pathBuf, sizeof(pathBuf));

            // append bin & game directories
            std::wstring newPath = _gamePath + L";" + std::wstring(projectDllPath) + L";" + std::wstring(pathBuf);
            SetEnvironmentVariableW(L"PATH", newPath.c_str());
        }

        // Update the game path to include the executable name;
        _gamePath += std::wstring(L"/") + _config.executableName;

        // verify game integrity if enabled
        if (_config.verifyGameIntegrity) {
            if (!EnsureGameExecutableIsCompatible()) {
                MessageBox(nullptr, "Unsupported game version", _config.name.c_str(), MB_ICONERROR);
                return false;
            }
        }

        wprintf(L"[Project] Loading game (%s)\n", _gamePath.c_str());

        // Inner run the project
        if (!Run()) {
            return false;
        }

        return true;
    }

    bool Project::RunInnerSteamChecks() {
        // are we a steam child ?
        const wchar_t *child_part = L"-steamchild:";
        wchar_t *cmd_match        = wcsstr(GetCommandLineW(), child_part);

        if (cmd_match) {
            int master_pid = _wtoi(&cmd_match[wcslen(child_part)]);

            // open a handle to the parent process with SYNCHRONIZE rights
            auto handle = OpenProcess(SYNCHRONIZE, FALSE, master_pid);

            // if we opened the process...
            if (handle != INVALID_HANDLE_VALUE) {
                // ... wait for it to exit and close the handle afterwards
                WaitForSingleObject(handle, INFINITE);

                CloseHandle(handle);
            }

            return 0;
        }

        // Make sure we have our required files
        const std::vector<std::string> requiredFiles = {"steam_api64.dll", "steam_api.dll"};
        if (!EnsureAtLeastOneFileExists(requiredFiles)) {
            return false;
        }

        // If we don't have the app id file, create it
        cppfs::FileHandle appIdFile = cppfs::fs::open("steam_appid.txt");
        if (!appIdFile.exists()) {
            auto outStream = appIdFile.createOutputStream();
            (*outStream) << std::to_string(_config.steamAppId) << std::endl;
        }

        // Initialize the steam wrapper
        const auto initResult = _steamWrapper->Init();
        if (initResult != External::Steam::SteamError::STEAM_NONE) {
            MessageBox(nullptr, fmt::format("Failed to init the bridge with steam, are you sure the Steam Client is running? Error Code #{}", initResult).c_str(),
                _config.name.c_str(), MB_ICONERROR);
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

        _gamePath = Utils::StringUtils::NormalToWide(gamePath);
        return true;
    }

    bool Project::RunInnerClassicChecks() {
        cppfs::FileHandle handle = cppfs::fs::open(Utils::StringUtils::WideToNormal(_config.classicGamePath));
        if (!handle.isDirectory()) {
            MessageBoxA(nullptr, "Please specify game path", _config.name.c_str(), MB_ICONERROR);
            return 0;
        }
        _gamePath = _config.classicGamePath;
        return true;
    }

    bool Project::Run() {
        // Method cannot be called directly
        if (_gamePath.empty()) {
            MessageBoxA(nullptr, "Failed to extract game path from project", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        HANDLE hFile = CreateFileW(_gamePath.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            MessageBox(nullptr, "Failed to find executable image", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        gImagePath = _gamePath.c_str();

        // determine file length
        DWORD dwFileLength = SetFilePointer(hFile, 0, NULL, FILE_END);
        if (dwFileLength == INVALID_SET_FILE_POINTER) {
            CloseHandle(hFile);
            MessageBox(nullptr, "Could not inquire executable image size", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        HANDLE hMapping = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (hMapping == INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            MessageBox(nullptr, "Could not map executable image", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        uint8_t *data = (uint8_t *)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
        if (!data) {
            CloseHandle(hMapping);
            CloseHandle(hFile);
            MessageBox(nullptr, "Could not map view of executable image", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        printf("[Project] Loaded Game (%ld MB)\n", dwFileLength / 1024 / 1024);

        auto base = GetModuleHandle(nullptr);

        // Create the loader instance
        Loaders::ExecutableLoader loader(data);
        loader.SetLoadLimit(_config.loadLimit);
        loader.SetLibraryLoader([this](const char *library) -> HMODULE {
            if (_libraryLoader) {
                auto mod = _libraryLoader(library);
                if (mod) {
                    return mod;
                }
            }
            auto mod = LoadLibraryA(library);
            if (mod == nullptr) {
                mod = (HMODULE)INVALID_HANDLE_VALUE;
            }
            return mod;
        });
        loader.SetFunctionResolver([this](HMODULE hmod, const char *exportFn) -> LPVOID {
            if (_functionResolver) {
                auto ret = _functionResolver(hmod, exportFn);
                if (ret) {
                    return ret;
                }
            }
            if (!_config.loadClientManually && !_strcmpi(exportFn, "GetStartupInfoW")) {
                return static_cast<LPVOID>(GetStartupInfoW_Stub);
            }
            if (!_strcmpi(exportFn, "GetModuleFileNameA")) {
                return static_cast<LPVOID>(GetModuleFileNameA_Hook);
            }
            if (!_strcmpi(exportFn, "GetModuleFileNameExA")) {
                return static_cast<LPVOID>(GetModuleFileNameExA_Hook);
            }
            if (!_strcmpi(exportFn, "GetModuleFileNameW")) {
                return static_cast<LPVOID>(GetModuleFileNameW_Hook);
            }
            if (!_strcmpi(exportFn, "GetModuleFileNameExW")) {
                return static_cast<LPVOID>(GetModuleFileNameExW_Hook);
            }
            return static_cast<LPVOID>(GetProcAddress(hmod, exportFn));
        });

        loader.SetTLSInitializer([&](void **base, uint32_t *index) {
            auto tlsExport = (void (*)(void **, uint32_t *))GetProcAddress(tlsDll, "GetThreadLocalStorage");
            tlsExport(base, index);
        });

        loader.LoadIntoModule(base);

        // Once loaded, we can close handles
        UnmapViewOfFile(data);
        CloseHandle(hMapping);
        CloseHandle(hFile);

        // Acquire the entry point reference
        auto entry_point = static_cast<void (*)()>(loader.GetEntryPoint());

        hook::set_base(reinterpret_cast<uintptr_t>(base));

        if (_preLaunchFunctor)
            _preLaunchFunctor();

        InvokeEntryPoint(entry_point);
        return true;
    }

    void Project::InvokeEntryPoint(void (*entryPoint)()) {
        // SEH call to prevent STATUS_INVALID_HANDLE
        __try {
            // and call the entry point
            entryPoint();
        }
        __except (HandleVariant(GetExceptionInformation())) {
        }
    }

    bool Project::EnsureFilesExist(const std::vector<std::string> &files) {
        for (const auto &file : files) {
            cppfs::FileHandle fh = cppfs::fs::open(file);
            if (!fh.exists() || !fh.isFile()) {
                MessageBox(nullptr, std::string("The file " + file + "is not present in the current directory").c_str(), "Framework", MB_ICONERROR);
                return false;
            }
        }
        return true;
    }

    bool Project::EnsureAtLeastOneFileExists(const std::vector<std::string> &files) {
        for (const auto &file : files) {
            cppfs::FileHandle fh = cppfs::fs::open(file);
            if (fh.exists() && fh.isFile()) {
                return true;
            }
        }
        return false;
    }

    bool Project::EnsureGameExecutableIsCompatible() {
        if (_gamePath.empty()) {
            MessageBoxA(nullptr, "Failed to extract game path from project", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        auto gameExeHandle = std::ifstream(Utils::StringUtils::WideToNormal(_gamePath), std::ios::binary | std::ios::ate);
        if (!gameExeHandle.good()) {
            MessageBoxA(nullptr, "Failed to find the game executable", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        auto gameExeSize = gameExeHandle.tellg();
        gameExeHandle.seekg(0, std::ios::beg);
        std::vector<char> data(gameExeSize);
        gameExeHandle.read(data.data(), gameExeSize);
        auto checksum = Utils::Hashing::CalculateCRC32(data.data(), gameExeSize);

        for (auto &version : _config.supportedGameVersions) {
            if (checksum == version)
                return true;
        }

        return false;
    }
} // namespace Framework::Launcher
