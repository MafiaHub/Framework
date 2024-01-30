/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "project.h"

#include "loaders/exe_ldr.h"
#include "logging/logger.h"
#include "sfd.h"
#include "utils/hashing.h"
#include "utils/string_utils.h"

#include <Psapi.h>
#include <ShellScalingApi.h>
#include <Windows.h>
#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>
#include <fstream>
#include <ostream>
#include <utils/hooking/hooking.h>
#include <utils/minidump.h>

#include <utils/hooking/jitasm.h>

// Enforce discrete GPU on mobile units.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement        = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// linker config for sections
#pragma comment(linker, "/merge:.data=.cld")
#pragma comment(linker, "/merge:.rdata=.clr")
#pragma comment(linker, "/merge:.cl=.zdata")
#pragma comment(linker, "/merge:.text=.zdata")
#pragma comment(linker, "/section:.zdata,re")

// allocate space for game
#ifdef _M_AMD64
#pragma bss_seg(".fwgame")
char fwgame_seg[0x6fffffff];
#else
#pragma bss_seg(".fwgame")
char fwgame_seg[0x2500000];
#endif

// mark the end section we merge with .text
#pragma data_seg(".fwend")
uint8_t zdata[200] = {1};

static const wchar_t *gImagePath;
static const wchar_t *gDllName;
HMODULE tlsDll {};
static Framework::Launcher::ProjectConfiguration *gConfig = nullptr;

static wchar_t gProjectDllPath[32768];

// Default entry point for the client DLL
typedef void (*ClientEntryPoint)(const wchar_t *projectPath);

static LONG NTAPI HandleVariant(PEXCEPTION_POINTERS exceptionInfo) {
    const auto result = Framework::Utils::MiniDump::ExceptionFilter(exceptionInfo);
    if (result == EXCEPTION_CONTINUE_EXECUTION)
        return result;
    else if (result != EXCEPTION_EXECUTE_HANDLER)
        return (exceptionInfo->ExceptionRecord->ExceptionCode == STATUS_INVALID_HANDLE) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
    return result;
}

void WINAPI GetStartupInfoW_Stub(LPSTARTUPINFOW lpStartupInfo) {
    Framework::Launcher::Project::InitialiseClientDLL();

    return GetStartupInfoW(lpStartupInfo);
}

void WINAPI GetStartupInfoA_Stub(LPSTARTUPINFOA lpStartupInfo) {
    Framework::Launcher::Project::InitialiseClientDLL();

    return GetStartupInfoA(lpStartupInfo);
}

LPWSTR WINAPI GetCommandLineW_Stub() {
    if (!gConfig->loadClientManually) {
        Framework::Launcher::Project::InitialiseClientDLL();
    }

    static wchar_t buffer[MAX_PATH] = {0};
    wcscpy_s(buffer, MAX_PATH, GetCommandLineW());
    wcscat_s(buffer, MAX_PATH, gConfig->additionalLaunchArguments.c_str());

    return buffer;
}

LPSTR WINAPI GetCommandLineA_Stub() {
    if (!gConfig->loadClientManually) {
        Framework::Launcher::Project::InitialiseClientDLL();
    }
    auto args = Framework::Utils::StringUtils::WideToNormal(gConfig->additionalLaunchArguments);

    static char buffer[MAX_PATH] = {0};
    strcpy_s(buffer, MAX_PATH, GetCommandLineA());
    strcat_s(buffer, MAX_PATH, args.c_str());

    return buffer;
}

DWORD WINAPI GetModuleFileNameA_Hook(HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    if (!hModule || hModule == GetModuleHandle(nullptr)) {
        auto gamePath = Framework::Utils::StringUtils::WideToNormal(gImagePath);
        strcpy_s(lpFilename, nSize, gamePath.c_str());

        return (DWORD)gamePath.size();
    }

    return GetModuleFileNameA(hModule, lpFilename, nSize);
}

DWORD WINAPI GetModuleFileNameExA_Hook(HANDLE hProcess, HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    if (!hModule || hModule == GetModuleHandle(nullptr)) {
        auto gamePath = Framework::Utils::StringUtils::WideToNormal(gImagePath);
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
    auto len = GetModuleFileNameW(hModule, lpFilename, nSize);
    return len;
}

DWORD WINAPI GetModuleFileNameExW_Hook(HANDLE hProcess, HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    if (!hModule || hModule == GetModuleHandle(nullptr)) {
        wcscpy_s(lpFilename, nSize, gImagePath);

        return (DWORD)wcslen(gImagePath);
    }

    return GetModuleFileNameExW(hProcess, hModule, lpFilename, nSize);
}

HMODULE WINAPI GetModuleHandleW_Hook(LPWSTR lpModuleName) {
    if (lpModuleName == nullptr) {
        return (HMODULE)GetModuleHandle(nullptr);
    }

    return GetModuleHandleW(lpModuleName);
}

HMODULE WINAPI GetModuleHandleA_Hook(LPSTR lpModuleName) {
    if (lpModuleName == nullptr) {
        return (HMODULE)GetModuleHandle(nullptr);
    }

    return GetModuleHandleA(lpModuleName);
}

BOOL WINAPI GetModuleHandleExW_Hook(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE *phModule) {
    if (lpModuleName == nullptr) {
        *phModule = (HMODULE)GetModuleHandle(nullptr);
        return TRUE;
    }

    return GetModuleHandleExW(dwFlags, lpModuleName, phModule);
}

BOOL WINAPI GetModuleHandleExA_Hook(DWORD dwFlags, LPSTR lpModuleName, HMODULE *phModule) {
    if (lpModuleName == nullptr) {
        *phModule = (HMODULE)GetModuleHandle(nullptr);
        return TRUE;
    }

    return GetModuleHandleExA(dwFlags, lpModuleName, phModule);
}

namespace Framework::Launcher {
    Project::Project(ProjectConfiguration &cfg): _config(cfg) {
        gConfig = &_config;
        // Fetch the current working directory
        GetCurrentDirectoryW(32768, gProjectDllPath);

        Logging::GetInstance()->SetLogName(_config.name);

        auto projectPath = Utils::StringUtils::WideToNormal(gProjectDllPath);
        std::replace(projectPath.begin(), projectPath.end(), '/', '\\');
        Logging::GetInstance()->SetLogFolder(projectPath + "/logs");

        _steamWrapper = std::make_unique<External::Steam::Wrapper>();
        _minidump     = std::make_unique<Utils::MiniDump>();
        _fileConfig   = std::make_unique<Utils::Config>();

        _minidump->SetSymbolPath(Utils::StringUtils::WideToNormal(gProjectDllPath));
    }

    bool Project::Launch() {
        if (_config.allocateDeveloperConsole) {
            AllocateDeveloperConsole();
        }

        if (!_config.disablePersistentConfig) {
            if (!LoadJSONConfig()) {
                MessageBox(nullptr, "Failed to load JSON launcher config", _config.name.c_str(), MB_ICONERROR);
                return false;
            }
        }

        // Run platform-dependent platform checks and init steps
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
        if (!_config.loadClientManually && !LoadLibraryW(_config.destinationDllName.c_str())) {
            DWORD dwError = GetLastError();
            MessageBox(nullptr, fmt::format("Failed to load core runtime with error code {}", dwError).c_str(), _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        if (!_config.disablePersistentConfig) {
            SaveJSONConfig();
        }

        // Add the required DLL directories to the current process
        auto addDllDirectory          = (decltype(&AddDllDirectory))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "AddDllDirectory");
        auto setDefaultDllDirectories = (decltype(&SetDefaultDllDirectories))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetDefaultDllDirectories");
        if (addDllDirectory && setDefaultDllDirectories) {
            setDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

            // first search in game root dir
            addDllDirectory(_gamePath.c_str());

            // add any custom search paths from the mod
            for (auto &path : _config.additionalSearchPaths) {
                addDllDirectory((_gamePath + L"\\" + path).c_str());
            }

            // add our own paths now
            addDllDirectory(gProjectDllPath);
            addDllDirectory((std::wstring(gProjectDllPath) + L"\\bin").c_str());

            if (_config.useAlternativeWorkDir) {
                _gamePath += L"/" + _config.alternativeWorkDir;
                addDllDirectory(_gamePath.c_str());
            }

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

#ifdef _M_IX86
            steamDll = LoadLibraryW(L"fw_steam_api.dll");
#else
            steamDll = LoadLibraryW(L"fw_steam_api64.dll");
#endif

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
            std::wstring newPath = _gamePath + L";" + std::wstring(gProjectDllPath) + L";" + std::wstring(pathBuf);
            SetEnvironmentVariableW(L"PATH", newPath.c_str());
        }

        // Update the game path to include the executable name;
        _gamePath += std::wstring(L"/") + _config.executableName;

        // Acquire the game version
        auto checksum = GetGameVersion();

        // verify game integrity if enabled
        if (_config.verifyGameIntegrity) {
            if (!EnsureGameExecutableIsCompatible(checksum)) {
                MessageBox(nullptr, "Unsupported game version", _config.name.c_str(), MB_ICONERROR);
                return false;
            }
        }

        Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("Loading game {}", Utils::StringUtils::WideToNormal(_gamePath));

        // Run with type depending
        if (_config.launchType == ProjectLaunchType::PE_LOADING) {
            return RunWithPELoading();
        }
        else if (_config.launchType == ProjectLaunchType::DLL_INJECTION) {
            return RunWithDLLInjection();
        }
        else {
            return false;
        }
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

            return false;
        }

        // Make sure we have our required files
        const std::vector<std::string> requiredFiles = {"fw_steam_api64.dll", "fw_steam_api.dll"};
        if (!EnsureAtLeastOneFileExists(requiredFiles)) {
            return false;
        }

        // If we don't have the app id file, create it
        cppfs::FileHandle appIdFile = cppfs::fs::open("steam_appid.txt");
        appIdFile.writeFile(std::to_string(_config.steamAppId) + "\n");

        // Initialize the steam wrapper
        const auto initResult = _steamWrapper->Init();
        if (initResult != External::Steam::SteamError::STEAM_NONE) {
            MessageBox(nullptr, fmt::format("Failed to init the bridge with steam, are you sure the Steam Client is running? Error Code #{}", initResult).c_str(), _config.name.c_str(), MB_ICONERROR);
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
        std::replace(_gamePath.begin(), _gamePath.end(), '\\', '/');

        // Set classic game path to the one found by Steam just for sake of having that information stored in the config
        // file.
        _config.classicGamePath = _gamePath;

        // Make sure Steam is aware of himself
        auto appId = std::to_wstring(_config.steamAppId);
        SetEnvironmentVariableW(L"SteamAppId", appId.c_str());

        // Now we have everything we want, just say goodbye
        _steamWrapper->Shutdown();
        return true;
    }

    bool Project::RunInnerClassicChecks() {
        cppfs::FileHandle handle = cppfs::fs::open(Utils::StringUtils::WideToNormal(_config.classicGamePath));
        if (!handle.isDirectory() && !_config.promptForGameExe) {
            MessageBoxA(nullptr, "Please specify game path", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        if (!handle.isDirectory()) {
            const auto exePath = Utils::StringUtils::WideToNormal(gProjectDllPath);

            sfd_Options sfd = {};
            sfd.path        = exePath.c_str();
            sfd.extension   = _config.promptExtension.c_str();
            sfd.filter_name = _config.promptFilterName.c_str();
            sfd.filter      = _config.promptFilter.c_str();
            sfd.title       = _config.promptTitle.c_str();

            char const *path = sfd_open_dialog(&sfd);
            if (path) {
                // Reset working directory
                SetCurrentDirectoryW(gProjectDllPath);

                handle = cppfs::fs::open(path);

                if (!handle.isFile()) {
                    MessageBoxA(nullptr, ("Cannot find a game executable by given path:\n" + std::string(path) + "\n\n Please check your path and try again!").c_str(), _config.name.c_str(), MB_ICONERROR);
                    return false;
                }

                _config.classicGamePath = Utils::StringUtils::NormalToWide(path);

                std::replace(_config.classicGamePath.begin(), _config.classicGamePath.end(), '\\', '/');

                _config.classicGamePath = _config.classicGamePath.substr(0, _config.classicGamePath.length() - _config.executableName.length());

                if (_config.promptSelectionFunctor)
                    _config.classicGamePath = _config.promptSelectionFunctor(_config.classicGamePath);

                if (_config.preferSteam) {
#ifdef _M_IX86
                    auto steamDllName = "/steam_api.dll";
#else
                    auto steamDllName = "/steam_api64.dll";
#endif
                    auto steamDll = cppfs::fs::open(Utils::StringUtils::WideToNormal(_config.classicGamePath) + steamDllName);

                    if (steamDll.exists()) {
                        Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("Steam dll found in the game directory, switching to steam platform");
                        _config.platform = ProjectPlatform::STEAM;
                        return RunInnerSteamChecks();
                    }
                }
            }
            else {
                ExitProcess(0);
            }
        }

        _gamePath = _config.classicGamePath;
        return true;
    }

    DLLInjectionResults InjectLibraryIntoProcess(HANDLE hProcess, const wchar_t *szLibraryPath) {
        DLLInjectionResults result = INJECT_LIBRARY_RESULT_OK;

        // Get the length of the library path
        size_t sLibraryPathLen = (wcslen(szLibraryPath) + 1) * sizeof(WCHAR);

        // Allocate the a block of memory in our target process for the library path
        void *pRemoteLibraryPath = VirtualAllocEx(hProcess, NULL, sLibraryPathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        // Write our library path to the allocated block of memory
        SIZE_T sBytesWritten = 0;
        BOOL bWriteSuccess   = WriteProcessMemory(hProcess, pRemoteLibraryPath, (void *)szLibraryPath, sLibraryPathLen, &sBytesWritten);

        if (!bWriteSuccess || sBytesWritten != sLibraryPathLen) {
            result = INJECT_LIBRARY_RESULT_WRITE_FAILED;
        }
        else {
            // Get the handle of Kernel32.dll
            HMODULE hKernel32 = GetModuleHandle("kernel32.dll");
            if (hKernel32 == NULL) {
                result = INJECT_LIBRARY_GET_MODULE_HANDLE_FAILED;
            }
            else {
                // Get the address of the LoadLibraryA function from Kernel32.dll
                FARPROC pfnLoadLibraryW = GetProcAddress(hKernel32, "LoadLibraryW");
                if (pfnLoadLibraryW == NULL) {
                    result = INJECT_LIBRARY_GET_PROC_ADDRESS_FAILED;
                }
                else {
                    // Create a thread inside the target process to load our library
                    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pfnLoadLibraryW, pRemoteLibraryPath, 0, NULL);

                    if (hThread) {
                        // Wait for the created thread to end
                        WaitForSingleObject(hThread, INFINITE);

                        DWORD dwExitCode = 0;
                        if (GetExitCodeThread(hThread, &dwExitCode)) {
                            // Should never happen as we wait for the thread to be finished.
                            assert(dwExitCode != STILL_ACTIVE);
                        }
                        else {
                            result = INJECT_LIBRARY_GET_RETURN_CODE_FAILED;
                        }

                        // In case LoadLibrary returns handle equal to zero there was some problem.
                        if (dwExitCode == 0) {
                            result = INJECT_LIBRARY_LOAD_LIBRARY_FAILED;
                        }

                        // Close our thread handle
                        CloseHandle(hThread);
                    }
                    else {
                        // Thread creation failed
                        result = INJECT_LIBRARY_THREAD_CREATION_FAILED;
                    }
                }
            }
        }

        // Free the allocated block of memory inside the target process
        VirtualFreeEx(hProcess, pRemoteLibraryPath, 0, MEM_RELEASE);
        return result;
    }

    DLLInjectionResults InjectLibraryIntoProcess(DWORD dwProcessId, const wchar_t *szLibraryPath) {
        // Open our target process
        HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwProcessId);

        if (!hProcess) {
            // Failed to open the process
            return INJECT_LIBRARY_OPEN_PROCESS_FAIL;
        }

        // Inject the library into the process
        DLLInjectionResults result = InjectLibraryIntoProcess(hProcess, szLibraryPath);

        // Close the process handle
        CloseHandle(hProcess);
        return result;
    }

    bool Project::RunWithDLLInjection() {
        // Method cannot be called directly
        if (_gamePath.empty()) {
            MessageBoxA(nullptr, "Failed to extract game path from project", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        // Fix the path
        std::replace(_gamePath.begin(), _gamePath.end(), '/', '\\');

        // Append the optional additional arguments
        if (_config.additionalLaunchArguments.length() > 0) {
            _gamePath = _gamePath + L" " + _config.additionalLaunchArguments;
        }

        // Compute the global variable
        gImagePath = _gamePath.c_str();
        gDllName   = _config.destinationDllName.c_str();

        // Prepare startup info
        STARTUPINFOW siStartupInfo;
        PROCESS_INFORMATION piProcessInfo;
        memset(&siStartupInfo, 0, sizeof(siStartupInfo));
        memset(&piProcessInfo, 0, sizeof(piProcessInfo));
        siStartupInfo.cb = sizeof(siStartupInfo);

        // Create the game process and suspend it
        if (!CreateProcessW(NULL, (LPWSTR)_gamePath.c_str(), NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, gProjectDllPath, &siStartupInfo, &piProcessInfo)) {
            MessageBoxA(nullptr, "Failed to start game binary, cannot launch", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        // Inject the client dll inside
        std::wstring completeDllPath                 = gProjectDllPath + std::wstring(L"\\") + gDllName;
        const DLLInjectionResults moduleInjectResult = InjectLibraryIntoProcess(piProcessInfo.hProcess, completeDllPath.c_str());

        // Was it successfull?
        if (moduleInjectResult != INJECT_LIBRARY_RESULT_OK) {
            MessageBoxA(nullptr, "Failed to inject module into game process", _config.name.c_str(), MB_ICONERROR);

            TerminateProcess(piProcessInfo.hProcess, 0);
            return false;
        }

        // Resume the game main thread
        ResumeThread(piProcessInfo.hThread);

        return true;
    }

    bool Project::RunWithPELoading() {
        // Method cannot be called directly
        if (_gamePath.empty()) {
            MessageBoxA(nullptr, "Failed to extract game path from project", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        std::replace(_gamePath.begin(), _gamePath.end(), '/', '\\');
        gImagePath = _gamePath.c_str();
        gDllName   = _config.destinationDllName.c_str();

        HANDLE hFile = CreateFileW(_gamePath.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            MessageBox(nullptr, "Failed to find executable image", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        // determine file length
        DWORD dwFileLength = SetFilePointer(hFile, 0, nullptr, FILE_END);
        if (dwFileLength == INVALID_SET_FILE_POINTER) {
            CloseHandle(hFile);
            MessageBox(nullptr, "Could not inquire executable image size", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        HANDLE hMapping = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (hMapping == INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            MessageBox(nullptr, "Could not map executable image", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        auto *data = (uint8_t *)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
        if (!data) {
            CloseHandle(hMapping);
            CloseHandle(hFile);
            MessageBox(nullptr, "Could not map view of executable image", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("Loaded game ({:.02f} MB or {})", (float(dwFileLength) / 1024.0f / 1024.0f), dwFileLength);

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

            const auto exportName = std::string(exportFn);

            if (!_config.loadClientManually && exportName == "GetStartupInfoW") {
                return reinterpret_cast<LPVOID>(GetStartupInfoW_Stub);
            }
            if (!_config.loadClientManually && exportName == "GetStartupInfoA") {
                return reinterpret_cast<LPVOID>(GetStartupInfoA_Stub);
            }
            if (exportName == "GetCommandLineW") {
                return reinterpret_cast<LPVOID>(GetCommandLineW_Stub);
            }
            if (exportName == "GetCommandLineA") {
                return reinterpret_cast<LPVOID>(GetCommandLineA_Stub);
            }
            if (exportName == "GetModuleFileNameA") {
                return reinterpret_cast<LPVOID>(GetModuleFileNameA_Hook);
            }
            if (exportName == "GetModuleFileNameExA") {
                return reinterpret_cast<LPVOID>(GetModuleFileNameExA_Hook);
            }
            if (exportName == "GetModuleFileNameW") {
                return reinterpret_cast<LPVOID>(GetModuleFileNameW_Hook);
            }
            if (exportName == "GetModuleFileNameExW") {
                return reinterpret_cast<LPVOID>(GetModuleFileNameExW_Hook);
            }
            if (exportName == "GetModuleHandleA") {
                return reinterpret_cast<LPVOID>(GetModuleHandleA_Hook);
            }
            if (exportName == "GetModuleHandleExA") {
                return reinterpret_cast<LPVOID>(GetModuleHandleExA_Hook);
            }
            if (exportName == "GetModuleHandleW") {
                return reinterpret_cast<LPVOID>(GetModuleHandleW_Hook);
            }
            if (exportName == "GetModuleHandleExW") {
                return reinterpret_cast<LPVOID>(GetModuleHandleExW_Hook);
            }
            return static_cast<LPVOID>(GetProcAddress(hmod, exportFn));
        });

        loader.SetTLSInitializer([&](void **base, uint32_t *index) {
            auto tlsExport = (void (*)(void **, uint32_t *))GetProcAddress(tlsDll, "GetThreadLocalStorage");
            tlsExport(base, index);
        });

        loader.LoadIntoModule(base);
        loader.Protect();

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

    void Project::AllocateDeveloperConsole() const {
        AllocConsole();
        AttachConsole(GetCurrentProcessId());
        SetConsoleTitleW(_config.developerConsoleTitle.c_str());

        (void)freopen("CON", "w", stdout);
        (void)freopen("CONIN$", "r", stdin);
        (void)freopen("CONIN$", "r", stderr);
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

    bool Project::LoadJSONConfig() {
        if (!_config.overrideConfigFileName) {
            _config.configFileName = fmt::format("{}_launcher.json", _config.name);
        }
        auto configHandle = cppfs::fs::open(_config.configFileName);

        if (!configHandle.exists()) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("JSON config file is not present, generating new instance...");
            _fileConfig->Parse("{}");
            return true;
        }

        auto configData = configHandle.readFile();

        try {
            // Parse our config data first
            _fileConfig->Parse(configData);

            if (!_fileConfig->IsParsed()) {
                Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->critical("JSON config load has failed: {}", _fileConfig->GetLastError());
                return false;
            }

            // Retrieve fields and overwrite ProjectConfiguration defaults
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("Loading launcher settings from JSON config file...");
            _config.classicGamePath    = _fileConfig->GetDefault<std::wstring>("game_path", _config.classicGamePath);
            _config.steamAppId         = _fileConfig->GetDefault<AppId_t>("steam_app_id", _config.steamAppId);
            _config.executableName     = _fileConfig->GetDefault<std::wstring>("game_executable_name", _config.executableName);
            _config.destinationDllName = _fileConfig->GetDefault<std::wstring>("mod_dll_name", _config.destinationDllName);

            std::replace(_config.classicGamePath.begin(), _config.classicGamePath.end(), '\\', '/');
        }
        catch (const std::exception &ex) {
            return false;
        }
        return true;
    }

    void Project::SaveJSONConfig() {
        auto configHandle = cppfs::fs::open(_config.configFileName);

        // Retrieve fields from ProjectConfiguration and store data into a persistent config file
        _fileConfig->Set<std::wstring>("game_path", _config.classicGamePath);
        _fileConfig->Set<AppId_t>("steam_app_id", _config.steamAppId);
        _fileConfig->Set<std::wstring>("game_executable_name", _config.executableName);
        _fileConfig->Set<std::wstring>("mod_dll_name", _config.destinationDllName);

        configHandle.writeFile(_fileConfig->ToString());
    }

    uint32_t Project::GetGameVersion() {
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
        return checksum;
    }

    bool Project::EnsureGameExecutableIsCompatible(uint32_t checksum) {
        for (auto &version : _config.supportedGameVersions) {
            if (checksum == version) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("Game integrity verified. Mod allowed to launch (Checksum {}, found {})", checksum, version);
                return true;
            }
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Game integrity failed to verify. Mod not allowed to launch (Checksum {})", checksum);
        return false;
    }

    void Project::InitialiseClientDLL() {
        static bool init = false;

        if (!init) {
            auto mod = LoadLibraryW(gDllName);

            if (mod) {
                auto initFunc = reinterpret_cast<ClientEntryPoint>(GetProcAddress(mod, "InitClient"));
                if (initFunc) {
                    initFunc(gProjectDllPath);
                }
                else {
                    MessageBoxA(nullptr, "Failed to find InitClient function in client DLL", "Error", MB_ICONERROR);
                    ExitProcess(1);
                }
            }
            init = true;
        }
    }
} // namespace Framework::Launcher
