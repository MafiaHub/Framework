#include "project.h"

#include "loaders/exe_ldr.h"

#include <ShellScalingApi.h>
#include <Windows.h>
#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>
#include <utils/hooking/hooking.h>

// Fix for gpu-enabled games
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
}
extern "C" {
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

static const wchar_t *gImagePath;
static const wchar_t *gDllName;

static LONG NTAPI HandleVariant(PEXCEPTION_POINTERS exceptionInfo) {
    return (exceptionInfo->ExceptionRecord->ExceptionCode == STATUS_INVALID_HANDLE) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

void WINAPI GetStartupInfoW_Stub(LPSTARTUPINFOW lpStartupInfo) {
    static bool init = false;

    if (!init) {
        auto mod = LoadLibraryW(gDllName);

        if (mod) {
            auto init = reinterpret_cast<void *(*)()>(GetProcAddress(mod, "Init"));
            if (init) {
                init();
            }
        }
        init = true;
    }

    return GetStartupInfoW(lpStartupInfo);
}

DWORD WINAPI GetModuleFileNameA_Hook(HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    if (!hModule) {
        static char buf[MAX_PATH];

        // to ansi
        wcstombs(buf, gImagePath, MAX_PATH);

        strcpy_s(lpFilename, nSize, buf);

        return (DWORD)strlen(buf);
    }

    return GetModuleFileNameA(hModule, lpFilename, nSize);
}

DWORD WINAPI GetModuleFileNameExA(HANDLE hProcess, HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    if (!hModule) {
        static char buf[MAX_PATH];

        // to ansi
        wcstombs(buf, gImagePath, MAX_PATH);

        strcpy_s(lpFilename, nSize, buf);

        return (DWORD)strlen(buf);
    }

    return GetModuleFileNameExA(hProcess, hModule, lpFilename, nSize);
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

        // Add the required DLL directories to the current process
        auto addDllDirectory          = (decltype(&AddDllDirectory))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "AddDllDirectory");
        auto setDefaultDllDirectories = (decltype(&SetDefaultDllDirectories))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetDefaultDllDirectories");
        if (addDllDirectory && setDefaultDllDirectories) {
            setDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

            AddDllDirectory(_gamePath.c_str());
            AddDllDirectory(L"");
            AddDllDirectory(L"bin");
            SetCurrentDirectoryW(_gamePath.c_str());
        }

        // Load the steam runtime only if required
        if (_config.platform == ProjectPlatform::STEAM) {
            if (!LoadLibraryW(L"steam_api64.dll")) {
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

        // Update the game path to include the executable name;
        _gamePath += std::wstring(L"//") + _config.executableName;

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

        const std::vector<std::string> requiredFiles = {"steam_api64.dll"};
        // Make sure we have our required files
        if (!EnsureFilesExists(requiredFiles)) {
            return false;
        }

        // Initialize the steam wrapper
        if (_steamWrapper->Init() != External::Steam::SteamError::STEAM_NONE) {
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

    bool Project::RunInnerClassicChecks() {
        LPWSTR *szArglist;
        int nArgs;

        szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
        if (NULL == szArglist) {
            wprintf(L"CommandLineToArgvW failed\n");
            return 0;
        }

        if (nArgs < 2) {
            MessageBoxW(nullptr, L"Please specify game path", L"MafiaMP", MB_ICONERROR);
            return 0;
        }
        else {
            _gamePath = std::wstring(szArglist[1]);
        }

        // Free memory allocated for CommandLineToArgvW arguments.
        LocalFree(szArglist);
        return true;
    }

    bool Project::Run() {
        // Method cannot be called directly
        if (_gamePath.empty()) {
            return false;
        }

        HANDLE hFile = CreateFileW(_gamePath.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            MessageBoxA(NULL, "Failed to find executable image", "MafiaMP", MB_ICONERROR);
            return false;
        }

        gImagePath = _gamePath.c_str();

        // determine file length
        DWORD dwFileLength = SetFilePointer(hFile, 0, NULL, FILE_END);
        if (dwFileLength == INVALID_SET_FILE_POINTER) {
            CloseHandle(hFile);
            MessageBoxA(NULL, "Could not inquire executable image size", "MafiaMP", MB_ICONERROR);
            return false;
        }

        HANDLE hMapping = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (hMapping == INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            MessageBoxA(NULL, "Could not map executable image", "MafiaMP", MB_ICONERROR);
            return false;
        }

        uint8_t *data = (uint8_t *)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
        if (!data) {
            CloseHandle(hMapping);
            CloseHandle(hFile);
            MessageBoxA(NULL, "Could not map view of executable image", "MafiaMP", MB_ICONERROR);
            return false;
        }

        printf("[Project] Loaded Game (%ld MB)\n", dwFileLength / 1024 / 1024);

        auto base = GetModuleHandle(nullptr);

        // Create the loader instance
        Loaders::ExecutableLoader loader(data);
        loader.SetLoadLimit(0x140000000 + 0x130000000);
        loader.SetLibraryLoader([](const char *library) -> HMODULE {
            auto hMod = LoadLibraryA(library);
            if (hMod == nullptr) {
                hMod = (HMODULE)INVALID_HANDLE_VALUE;
            }
            return hMod;
        });
        loader.SetFunctionResolver([](HMODULE hmod, const char *exportFn) -> LPVOID {
            if (!_strcmpi(exportFn, "GetStartupInfoW")) {
                return static_cast<LPVOID>(GetStartupInfoW_Stub);
            }
            if (!_strcmpi(exportFn, "GetModuleFileNameA")) {
                return static_cast<LPVOID>(GetModuleFileNameA_Hook);
            }
            return static_cast<LPVOID>(GetProcAddress(hmod, exportFn));
        });

        loader.LoadIntoModule(base);

        // Once loaded, we can close handles
        UnmapViewOfFile(data);
        CloseHandle(hMapping);
        CloseHandle(hFile);

        // Acquire the entry point reference
        auto entry_point = static_cast<void (*)()>(loader.GetEP());

        hook::set_base(reinterpret_cast<uintptr_t>(base));
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
