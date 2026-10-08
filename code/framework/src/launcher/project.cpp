/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "project.h"

#include "gpu_preference.h"
#include "loaders/exe_ldr.h"
#include "loaders/process_identity.h"
#include "logging/logger.h"
#include "utils/hashing.h"
#include "utils/string_utils.h"
#include "utils/url_protocol.h"

#include <Psapi.h>
#include <ShellScalingApi.h>
#include <Windows.h>
#include <algorithm>
#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <fstream>
#include <ostream>
#include <stdexcept>
#include <utils/hooking/hooking.h>
#include <utils/minidump.h>

#include <utils/hooking/jitasm.h>

// Only survives DLL injection: PE loading maps the game over this image and takes the
// export directory with it, which is what ForceHighPerformanceGPU() works around.
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

// The space the game image is mapped into. Its bulk, .fwgame$b, is compiled into each launcher
// by game_reserve.cpp so a project can size it to its game; these two markers bound it. The
// linker joins .fwgame$* into one section ordered by the suffix, which is the only ordering it
// documents, so where .fwgame itself lands is checked on every launch in RunWithPELoading().
#pragma bss_seg(".fwgame$a")
char fwgame_begin[1];
#pragma bss_seg(".fwgame$c")
char fwgame_end[1];

// The launcher's own uninitialized globals below have always sat after the reservation
#pragma bss_seg(".fwgame$d")

// mark the end section we merge with .text
#pragma data_seg(".fwend")
uint8_t zdata[200] = {1};

static const wchar_t *gImagePath;
static const wchar_t *gDllName;
HMODULE tlsDll {};
static Framework::Launcher::ProjectConfiguration *gConfig = nullptr;

static wchar_t gProjectDllPath[32768];

// Default entry point for the client DLL
using ClientEntryPoint = void (*)(const wchar_t *projectPath);
using ThreadLocalCallback = void(NTAPI *)(void *, DWORD, void *);

void __cdecl RegisterThreadLocalExeAtexitCallback_Stub(ThreadLocalCallback) {
    // ucrtbase owns one EXE TLS-destructor callback per process. The launcher's CRT
    // already registered it, so registering the mapped game's callback aborts.
}

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

LPWSTR BuildGameCommandLineW() {
    if (!gImagePath || !gConfig) {
        return GetCommandLineW();
    }

    static wchar_t buffer[32768] = {};
    const auto &args             = gConfig->additionalLaunchArguments;
    const wchar_t *separator     = !args.empty() && args.front() != L' ' ? L" " : L"";
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"\"%ls\"%ls%ls", gImagePath, separator, args.c_str());
    return buffer;
}

LPSTR BuildGameCommandLineA() {
    static char buffer[32768] = {};
    const auto commandLine    = Framework::Utils::StringUtils::WideToNormal(BuildGameCommandLineW());
    strcpy_s(buffer, commandLine.c_str());
    return buffer;
}

bool SynchronizeUCRTCommandLine() {
    const auto ucrt = GetModuleHandleW(L"ucrtbase.dll");
    if (!ucrt) {
        return false;
    }

    using NarrowCommandLineAccessor = char **(__cdecl *)();
    using WideCommandLineAccessor   = wchar_t **(__cdecl *)();
    const auto narrowAccessor = reinterpret_cast<NarrowCommandLineAccessor>(GetProcAddress(ucrt, "__p__acmdln"));
    const auto wideAccessor   = reinterpret_cast<WideCommandLineAccessor>(GetProcAddress(ucrt, "__p__wcmdln"));
    if (!narrowAccessor || !wideAccessor) {
        return false;
    }

    *narrowAccessor() = BuildGameCommandLineA();
    *wideAccessor()   = BuildGameCommandLineW();
    return true;
}

bool SetProcessEnvironmentVariable(const wchar_t *name, const std::wstring &value) {
    const bool processUpdated = SetEnvironmentVariableW(name, value.c_str()) != FALSE;
    const bool ucrtUpdated    = _wputenv_s(name, value.c_str()) == 0;
    return processUpdated && ucrtUpdated;
}

LPWSTR WINAPI GetCommandLineW_Stub() {
    if (!gConfig->loadClientManually) {
        Framework::Launcher::Project::InitialiseClientDLL();
    }
    return BuildGameCommandLineW();
}

LPSTR WINAPI GetCommandLineA_Stub() {
    if (!gConfig->loadClientManually) {
        Framework::Launcher::Project::InitialiseClientDLL();
    }
    return BuildGameCommandLineA();
}

namespace {
    // The real API truncates into the caller's buffer and says so in its return value;
    // the CRT's _s copies abort the process instead, which is not a contract the game
    // can be handed. Mirror Win32: fill what fits, terminate, report the truncation.
    template <typename CharT>
    DWORD CopyMappedImagePath(CharT *destination, DWORD size, const CharT *path, size_t length) {
        if (!destination || size == 0) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }

        if (length >= size) {
            std::copy_n(path, size - 1, destination);
            destination[size - 1] = CharT {};
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return size;
        }

        std::copy_n(path, length, destination);
        destination[length] = CharT {};
        return static_cast<DWORD>(length);
    }

    // Set only once Launch() has resolved the game, but these hooks are reachable before
    // that; without the guard the conversions below read through a null pointer.
    bool ShouldReportMappedImage(HMODULE module) {
        return gImagePath && (!module || module == GetModuleHandle(nullptr));
    }
} // namespace

DWORD WINAPI GetModuleFileNameA_Hook(HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    if (ShouldReportMappedImage(hModule)) {
        const auto gamePath = Framework::Utils::StringUtils::WideToNormal(gImagePath);
        return CopyMappedImagePath(lpFilename, nSize, gamePath.c_str(), gamePath.size());
    }

    return GetModuleFileNameA(hModule, lpFilename, nSize);
}

DWORD WINAPI GetModuleFileNameExA_Hook(HANDLE hProcess, HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    if (ShouldReportMappedImage(hModule)) {
        const auto gamePath = Framework::Utils::StringUtils::WideToNormal(gImagePath);
        return CopyMappedImagePath(lpFilename, nSize, gamePath.c_str(), gamePath.size());
    }

    return GetModuleFileNameExA(hProcess, hModule, lpFilename, nSize);
}

DWORD WINAPI GetModuleFileNameW_Hook(HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    if (ShouldReportMappedImage(hModule)) {
        return CopyMappedImagePath(lpFilename, nSize, gImagePath, wcslen(gImagePath));
    }

    return GetModuleFileNameW(hModule, lpFilename, nSize);
}

DWORD WINAPI GetModuleFileNameExW_Hook(HANDLE hProcess, HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    if (ShouldReportMappedImage(hModule)) {
        return CopyMappedImagePath(lpFilename, nSize, gImagePath, wcslen(gImagePath));
    }

    return GetModuleFileNameExW(hProcess, hModule, lpFilename, nSize);
}

HMODULE WINAPI GetModuleHandleW_Hook(LPWSTR lpModuleName) {
    if (lpModuleName == nullptr) {
        return GetModuleHandle(nullptr);
    }

    return GetModuleHandleW(lpModuleName);
}

HMODULE WINAPI GetModuleHandleA_Hook(LPSTR lpModuleName) {
    if (lpModuleName == nullptr) {
        return GetModuleHandle(nullptr);
    }

    return GetModuleHandleA(lpModuleName);
}

BOOL WINAPI GetModuleHandleExW_Hook(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE *phModule) {
    if (lpModuleName == nullptr) {
        *phModule = GetModuleHandle(nullptr);
        return TRUE;
    }

    return GetModuleHandleExW(dwFlags, lpModuleName, phModule);
}

BOOL WINAPI GetModuleHandleExA_Hook(DWORD dwFlags, LPSTR lpModuleName, HMODULE *phModule) {
    if (lpModuleName == nullptr) {
        *phModule = GetModuleHandle(nullptr);
        return TRUE;
    }

    return GetModuleHandleExA(dwFlags, lpModuleName, phModule);
}

namespace Framework::Launcher {
    namespace {
        // Empty when Windows will not name our own image; each caller says what it falls back to.
        std::wstring LauncherExecutablePath() {
            std::wstring path(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0 || length >= path.size()) {
                return {};
            }

            path.resize(length);
            return path;
        }
    } // namespace

    Project::Project(ProjectConfiguration &cfg): _config(cfg) {
        gConfig = &_config;

        // The project root is the launcher's own directory, not the process working directory: the
        // client DLL, the logs, the cache and the DLL search paths all hang off it, and a
        // "<scheme>://" launch arrives with whatever directory the shell happened to be in. Keep the
        // working directory on it too, for the code further down that still reads it.
        const std::wstring executablePath = LauncherExecutablePath();
        if (!executablePath.empty()) {
            wcsncpy_s(gProjectDllPath, std::filesystem::path(executablePath).parent_path().c_str(), _TRUNCATE);
            SetCurrentDirectoryW(gProjectDllPath);
        }
        else {
            GetCurrentDirectoryW(32768, gProjectDllPath);
        }

        _projectPath = gProjectDllPath;

        Logging::GetInstance()->SetLogName(_config.name);

        auto projectPath = Utils::StringUtils::WideToNormal(gProjectDllPath);
        std::replace(projectPath.begin(), projectPath.end(), '/', '\\');
        Logging::GetInstance()->SetLogFolder(projectPath + "/logs");

        _minidump     = std::make_unique<Utils::MiniDump>();
        _fileConfig   = std::make_unique<Utils::Config>();

        _minidump->SetSymbolPath(Utils::StringUtils::WideToNormal(gProjectDllPath));
    }

    bool Project::Launch() {
        ForceHighPerformanceGPU();

        if (!_config.urlProtocolScheme.empty()) {
            RegisterUrlProtocolScheme();
            HandleUrlProtocolLaunch();
        }

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
        switch (RunPlatformChecks()) {
        case PlatformCheckStatus::OK: break;
        case PlatformCheckStatus::HANDED_OFF: return true;
        default: return false;
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
        const auto addDllDirectory          = (decltype(&AddDllDirectory))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "AddDllDirectory");
        const auto setDefaultDllDirectories = (decltype(&SetDefaultDllDirectories))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetDefaultDllDirectories");
        if (addDllDirectory && setDefaultDllDirectories) {
            setDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

            // mod-supplied absolute search dirs: for runtimes outside the game tree
            // whose deps are only reachable via PATH, which we just dropped
            for (const auto &dir : _config.additionalDllDirectories) {
                addDllDirectory(dir.c_str());
            }

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
                _gamePath = GetGameWorkDir(_gamePath);
                addDllDirectory(_gamePath.c_str());
            }

            SetCurrentDirectoryW(_gamePath.c_str());
        }

        // Load TLS dummy so the game can use thread-local storage
        if (!(tlsDll = LoadLibraryW(L"FrameworkLoaderData.dll"))) {
            MessageBox(nullptr, "Failed to load a vital framework component", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        // Load the platform's runtime, such as Steam's
        if (!_platform->PrepareLaunch(*this)) {
            return false;
        }

        // Use real scaling
        const auto shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) {
            const auto SetProcessDpiAwareness = (decltype(&::SetProcessDpiAwareness))GetProcAddress(shcore, "SetProcessDpiAwareness");

            if (SetProcessDpiAwareness) {
                SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
            }
        }

        // handle path variable
        {
            static wchar_t pathBuf[32768];
            GetEnvironmentVariableW(L"PATH", pathBuf, sizeof(pathBuf));

            // append bin & game directories
            const std::wstring newPath = _gamePath + L";" + std::wstring(gProjectDllPath) + L";" + std::wstring(pathBuf);
            SetProcessEnvironmentVariable(L"PATH", newPath);
        }

        // Update the game path to include the executable name;
        _gamePath += std::wstring(L"/") + _config.executableName;

        std::error_code ec;
        if (!std::filesystem::is_regular_file(_gamePath, ec)) {
            MessageBoxA(nullptr, ("The game executable could not be found:\n" + Utils::StringUtils::WideToNormal(_gamePath)).c_str(), _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        // Verify game integrity if enabled. GetGameVersion() reads the whole executable into
        // memory and CRC32s it — for Hogwarts Legacy that is 430 MB of I/O plus the hash, about
        // a second and a half of boot, and the mapper below then reads the same file again. The
        // checksum has no other consumer, so when verification is off there is nothing to compute.
        if (_config.verifyGameIntegrity) {
            if (!EnsureGameExecutableIsCompatible(GetGameVersion())) {
                MessageBox(nullptr, "Unsupported game version", _config.name.c_str(), MB_ICONERROR);
                return false;
            }
        }

        Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("Loading game {}", Utils::StringUtils::WideToNormal(_gamePath));

        // Run with type depending
        if (_config.launchType == ProjectLaunchType::PE_LOADING) {
            return RunWithPELoading();
        }
#ifdef FW_DLL_INJECTION
        else if (_config.launchType == ProjectLaunchType::DLL_INJECTION) {
            return RunWithDLLInjection();
        }
#endif
        else {
            return false;
        }
    }

    PlatformCheckStatus Project::RunPlatformChecks() {
        const auto logger = Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER);

        auto resolve = [&](const std::shared_ptr<Platform> &platform, bool reportErrors) {
            PlatformResolution resolution;
            const auto status = platform->Resolve(*this, resolution, reportErrors);
            if (status != PlatformCheckStatus::OK) {
                return status;
            }

            _platform = platform;

            // a copy the player picked may still be one of the stores' own
            if (resolution.manual || platform->IsManualSelection()) {
                for (const auto &other : _config.platforms) {
                    if (other != platform && other->AdoptManualCopy(*this, resolution)) {
                        logger->info("The selected game folder is started as the {} copy", other->GetName());
                        _platform = other;
                        break;
                    }
                }
            }

            _gamePath               = resolution.gameRoot;
            _manualGamePath         = resolution.manual || (_manualGamePath && platform->IsManualSelection());
            _config.classicGamePath = _gamePath; // stashed so it lands in the persisted JSON config
            return status;
        };

        // a remembered manual pick wins over every store
        if (_manualGamePath && GameExecutableExistsIn(_config.classicGamePath)) {
            for (const auto &platform : _config.platforms) {
                if (platform->IsManualSelection()) {
                    logger->info("Using the manually selected game path from the launcher config");
                    return resolve(platform, true);
                }
            }
        }

        for (size_t i = 0; i < _config.platforms.size(); ++i) {
            const auto &platform = _config.platforms[i];
            const auto status    = resolve(platform, i + 1 == _config.platforms.size());
            if (status != PlatformCheckStatus::UNAVAILABLE) {
                return status;
            }

            logger->info("{} did not resolve the game", platform->GetName());
        }
        return PlatformCheckStatus::ABORT;
    }

    PlatformCheckStatus Project::ReportUnavailable(const char *platform, const std::string &reason, bool reportErrors) const {
        Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->warn("{} lookup failed: {}", platform, reason);
        if (reportErrors) {
            ReportError(reason);
        }
        return PlatformCheckStatus::UNAVAILABLE;
    }

    void Project::ReportError(const std::string &message) const {
        MessageBoxA(nullptr, message.c_str(), _config.name.c_str(), MB_ICONERROR);
    }

    std::wstring Project::GetLauncherExecutablePath() const {
        return LauncherExecutablePath();
    }

    void Project::SetProcessVariable(const wchar_t *name, const std::wstring &value) const {
        SetProcessEnvironmentVariable(name, value);
    }

    std::vector<std::wstring> Project::GetAlternativeWorkDirCandidates() const {
        std::vector<std::wstring> candidates;
        if (!_config.useAlternativeWorkDir) {
            return candidates;
        }

        if (!_config.alternativeWorkDir.empty()) {
            candidates.push_back(_config.alternativeWorkDir);
        }
        for (const auto &fallback : _config.alternativeWorkDirFallbacks) {
            if (!fallback.empty()) {
                candidates.push_back(fallback);
            }
        }
        return candidates;
    }

    std::wstring Project::GetGameWorkDir(const std::wstring &gameRoot) const {
        const auto candidates = GetAlternativeWorkDirCandidates();
        if (candidates.empty()) {
            return gameRoot;
        }

        // the first layout that holds the executable, then the root itself (a store that ships the
        // executable at the top), else the primary one so errors name it
        std::error_code ec;
        for (const auto &candidate : candidates) {
            const auto workDir = std::filesystem::path(gameRoot) / candidate;
            if (std::filesystem::is_regular_file(workDir / _config.executableName, ec)) {
                return workDir.wstring();
            }
        }
        if (std::filesystem::is_regular_file(std::filesystem::path(gameRoot) / _config.executableName, ec)) {
            return gameRoot;
        }
        return (std::filesystem::path(gameRoot) / candidates.front()).wstring();
    }

    bool Project::GameExecutableExistsIn(const std::wstring &gameRoot) const {
        if (gameRoot.empty()) {
            return false;
        }

        std::error_code ec;
        return std::filesystem::is_regular_file(std::filesystem::path(GetGameWorkDir(gameRoot)) / _config.executableName, ec);
    }

    void Project::RegisterUrlProtocolScheme() const {
        const std::wstring executablePath = LauncherExecutablePath();
        if (executablePath.empty()) {
            return;
        }

        // Re-asserted on every start rather than once at install time: this is the only place that
        // knows where the launcher currently lives, and an update or a moved game folder would
        // otherwise leave the scheme pointing at a path that no longer runs.
        const std::wstring description = L"URL:" + Utils::StringUtils::NormalToWide(_config.name) + L" link";
        if (!Utils::UrlProtocol::Register(_config.urlProtocolScheme, description, executablePath)) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->warn("Could not claim the {}:// scheme for this user; links from a browser will not open the game", Utils::StringUtils::WideToNormal(_config.urlProtocolScheme));
        }
    }

    void Project::HandleUrlProtocolLaunch() {
        if (const auto url = Utils::UrlProtocol::ExtractLaunchUrl(_config.urlProtocolScheme, GetCommandLineW())) {
            SetProcessEnvironmentVariable(L"MafiaHubLaunchURL", *url);
        }
    }

#ifdef FW_DLL_INJECTION
    DLLInjectionResult InjectLibraryIntoProcess(HANDLE hProcess, const wchar_t *szLibraryPath) {
        DLLInjectionResult result = DLLInjectionResult::INJECT_LIBRARY_RESULT_OK;

        // Get the length of the library path
        const size_t sLibraryPathLen = (wcslen(szLibraryPath) + 1) * sizeof(WCHAR);

        // Allocate the a block of memory in our target process for the library path
        void *pRemoteLibraryPath = VirtualAllocEx(hProcess, NULL, sLibraryPathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        // Write our library path to the allocated block of memory
        SIZE_T sBytesWritten     = 0;
        const BOOL bWriteSuccess = WriteProcessMemory(hProcess, pRemoteLibraryPath, szLibraryPath, sLibraryPathLen, &sBytesWritten);

        if (!bWriteSuccess || sBytesWritten != sLibraryPathLen) {
            result = DLLInjectionResult::INJECT_LIBRARY_RESULT_WRITE_FAILED;
        }
        else {
            // Get the handle of Kernel32.dll
            const HMODULE hKernel32 = GetModuleHandle("kernel32.dll");
            if (hKernel32 == NULL) {
                result = DLLInjectionResult::INJECT_LIBRARY_GET_MODULE_HANDLE_FAILED;
            }
            else {
                // Get the address of the LoadLibraryA function from Kernel32.dll
                const FARPROC pfnLoadLibraryW = GetProcAddress(hKernel32, "LoadLibraryW");
                if (pfnLoadLibraryW == NULL) {
                    result = DLLInjectionResult::INJECT_LIBRARY_GET_PROC_ADDRESS_FAILED;
                }
                else {
                    // Create a thread inside the target process to load our library
                    const HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pfnLoadLibraryW, pRemoteLibraryPath, 0, NULL);

                    if (hThread) {
                        // Wait for the created thread to end
                        WaitForSingleObject(hThread, INFINITE);

                        DWORD dwExitCode = 0;
                        if (GetExitCodeThread(hThread, &dwExitCode)) {
                            // Should never happen as we wait for the thread to be finished.
                            assert(dwExitCode != STILL_ACTIVE);
                        }
                        else {
                            result = DLLInjectionResult::INJECT_LIBRARY_GET_RETURN_CODE_FAILED;
                        }

                        // In case LoadLibrary returns handle equal to zero there was some problem.
                        if (dwExitCode == 0) {
                            result = DLLInjectionResult::INJECT_LIBRARY_LOAD_LIBRARY_FAILED;
                        }

                        // Close our thread handle
                        CloseHandle(hThread);
                    }
                    else {
                        // Thread creation failed
                        result = DLLInjectionResult::INJECT_LIBRARY_THREAD_CREATION_FAILED;
                    }
                }
            }
        }

        // Free the allocated block of memory inside the target process
        VirtualFreeEx(hProcess, pRemoteLibraryPath, 0, MEM_RELEASE);
        return result;
    }

    DLLInjectionResult InjectLibraryIntoProcess(DWORD dwProcessId, const wchar_t *szLibraryPath) {
        // Open our target process
        const HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwProcessId);

        if (!hProcess) {
            // Failed to open the process
            return DLLInjectionResult::INJECT_LIBRARY_OPEN_PROCESS_FAIL;
        }

        // Inject the library into the process
        const DLLInjectionResult result = InjectLibraryIntoProcess(hProcess, szLibraryPath);

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
        const std::wstring completeDllPath           = gProjectDllPath + std::wstring(L"\\") + gDllName;
        const DLLInjectionResult moduleInjectResult = InjectLibraryIntoProcess(piProcessInfo.hProcess, completeDllPath.c_str());

        // Was it successfull?
        if (moduleInjectResult != DLLInjectionResult::INJECT_LIBRARY_RESULT_OK) {
            MessageBoxA(nullptr, "Failed to inject module into game process", _config.name.c_str(), MB_ICONERROR);

            TerminateProcess(piProcessInfo.hProcess, 0);
            return false;
        }

        // Resume the game main thread
        ResumeThread(piProcessInfo.hThread);

        return true;
    }
#endif

    bool Project::RunWithPELoading() {
        // Method cannot be called directly
        if (_gamePath.empty()) {
            MessageBoxA(nullptr, "Failed to extract game path from project", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        std::replace(_gamePath.begin(), _gamePath.end(), '/', '\\');
        gImagePath = _gamePath.c_str();
        gDllName   = _config.destinationDllName.c_str();

        const HANDLE hFile = CreateFileW(_gamePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            MessageBoxA(nullptr, "Failed to find executable image", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        // determine file length
        DWORD dwFileLength = GetFileSize(hFile, nullptr);
        if (dwFileLength == INVALID_FILE_SIZE) {
            CloseHandle(hFile);
            MessageBoxA(nullptr, "Could not inquire executable image size", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        const HANDLE hMapping = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (hMapping == INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            MessageBoxA(nullptr, "Could not map executable image", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        const auto *data = (uint8_t *)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
        if (!data) {
            CloseHandle(hMapping);
            CloseHandle(hFile);
            MessageBoxA(nullptr, "Could not map view of executable image", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("Loaded game ({:.02f} MB or {})", (float(dwFileLength) / 1024.0f / 1024.0f), dwFileLength);

        auto base = GetModuleHandle(nullptr);

        // The game lands at our image base with its sections from RVA 0x1000 on, so the
        // reservation must start right after our headers and reach past the game's last page
        const auto imageBase     = reinterpret_cast<uintptr_t>(base);
        const auto gameImageSize = reinterpret_cast<const IMAGE_NT_HEADERS *>(data + reinterpret_cast<const IMAGE_DOS_HEADER *>(data)->e_lfanew)->OptionalHeader.SizeOfImage;
        if (reinterpret_cast<uintptr_t>(fwgame_begin) != imageBase + 0x1000 || reinterpret_cast<uintptr_t>(fwgame_end) < imageBase + gameImageSize) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Game image of {:#x} bytes does not fit the reservation at {:#x}-{:#x} (image base {:#x})", gameImageSize, reinterpret_cast<uintptr_t>(fwgame_begin), reinterpret_cast<uintptr_t>(fwgame_end), imageBase);
            UnmapViewOfFile(data);
            CloseHandle(hMapping);
            CloseHandle(hFile);
            MessageBoxA(nullptr, "The game executable does not fit the space this launcher reserves for it. The game may have been updated; please update the mod.", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        // Get file size
        DWORD fileSize = GetFileSize(hFile, NULL);
        if (fileSize == INVALID_FILE_SIZE) {
            CloseHandle(hMapping);
            CloseHandle(hFile);
            MessageBoxA(nullptr, "Failed to get file size of the game executable", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        // A wrapped title cannot be mapped from the file alone; its platform supplies the rest
        if (!_platform->PrepareImage(*this, _gamePath, std::span<const uint8_t>(data, fileSize))) {
            UnmapViewOfFile(data);
            CloseHandle(hMapping);
            CloseHandle(hFile);
            return false;
        }

        // Create the loader instance
        Loaders::ExecutableLoader loader(data, fileSize);
        loader.SetLoadLimit(_config.loadLimit);
        loader.SetUseDirectTlsSlot0(_config.useDirectTlsSlot0);
        loader.SetLibraryLoader([this](const char *library) -> HMODULE {
            if (_libraryLoader) {
                const auto mod = _libraryLoader(library);
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
                const auto ret = _functionResolver(hmod, exportFn);
                if (ret) {
                    return ret;
                }
            }

            const auto exportName = std::string(exportFn);

            if (_config.suppressThreadLocalExeAtexitCallback &&
                exportName == "_register_thread_local_exe_atexit_callback") {
                Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info(
                    "Suppressing duplicate mapped-EXE TLS atexit registration");
                return reinterpret_cast<LPVOID>(RegisterThreadLocalExeAtexitCallback_Stub);
            }
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

        loader.SetSectionsMappedCallback([&](HMODULE module) {
            _platform->OnSectionsMapped(module);
        });

        loader.SetTLSInitializer([&](void **base, uint32_t *index) {
            const auto tlsExport = (void (*)(void **, uint32_t *))GetProcAddress(tlsDll, "GetThreadLocalStorage");
            tlsExport(base, index);
        });

        // Map and prepare the image. The loader throws on fatal mapping errors (unresolvable
        // imports, protection failures); catch them here, before the game runs, so they report as
        // a startup failure rather than a crash inside the running game.
        void (*entry_point)() = nullptr;
        try {
            loader.LoadIntoModule(base);
            loader.Protect();

            // The code was written in place and only now made executable
            FlushInstructionCache(GetCurrentProcess(), nullptr, 0);

            // Once loaded, we can close handles
            UnmapViewOfFile(data);
            CloseHandle(hMapping);
            CloseHandle(hFile);

            // Acquire the entry point reference
            entry_point = static_cast<void (*)()>(loader.GetEntryPoint());

            // A wrapped title may not start at its own entry point
            entry_point = reinterpret_cast<void (*)()>(_platform->ResolveEntryPoint(reinterpret_cast<uintptr_t>(base), reinterpret_cast<uintptr_t>(entry_point)));

            hook::set_base(reinterpret_cast<uintptr_t>(base));

            // Must run before the game does: modules it loads later resolve their own
            // install directory through the process identity.
            Loaders::ApplyMappedImageIdentity(_gamePath);

            if (SynchronizeUCRTCommandLine()) {
                Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info(
                    "Mapped game command line: {}", BuildGameCommandLineA());
            }

            // The OS loader normally dispatches executable TLS callbacks before the entry
            // point. This image was mapped manually, so complete that loader step here.
            loader.RunTLSCallbacks();

            if (_preLaunchFunctor) {
                _preLaunchFunctor();
            }
        }
        catch (const std::exception &ex) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Failed to load and start the game: {}", ex.what());

            MessageBoxA(nullptr, fmt::format("The game could not be started:\n\n{}\n\nSee Launcher.log for the full stack trace.", ex.what()).c_str(), _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        // The game runs here; a C++ exception surfacing means it crashed while running.
        try {
            InvokeEntryPoint(entry_point);
            return true;
        }
        catch (const std::exception &ex) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Unhandled C++ exception escaped the game session: {}", ex.what());

            MessageBoxA(nullptr, fmt::format("The game stopped due to an unhandled error:\n\n{}\n\nSee Launcher.log for the full stack trace.", ex.what()).c_str(), _config.name.c_str(), MB_ICONERROR);
            return false;
        }
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

        // Disable QuickEdit: a stray click puts the console in select mode, which blocks
        // every write to it — freezing whatever game thread logs next until a key clears it.
        const HANDLE conIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD conMode      = 0;
        if (conIn != INVALID_HANDLE_VALUE && GetConsoleMode(conIn, &conMode)) {
            SetConsoleMode(conIn, (conMode & ~ENABLE_QUICK_EDIT_MODE) | ENABLE_EXTENDED_FLAGS);
        }

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
        const auto configHandle = cppfs::fs::open(_config.configFileName);

        if (!configHandle.exists()) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("JSON config file is not present, generating new instance...");
            _fileConfig->Parse("{}");
            return true;
        }

        const auto configData = configHandle.readFile();

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
            _manualGamePath            = _fileConfig->GetDefault<bool>("game_path_manual", _manualGamePath);
            _config.executableName     = _fileConfig->GetDefault<std::wstring>("game_executable_name", _config.executableName);
            _config.destinationDllName = _fileConfig->GetDefault<std::wstring>("mod_dll_name", _config.destinationDllName);

            std::replace(_config.classicGamePath.begin(), _config.classicGamePath.end(), '\\', '/');
        }
        catch (const std::exception &ex) {
            return false;
        }
        return true;
    }

    void Project::SaveJSONConfig() const {
        auto configHandle = cppfs::fs::open(_config.configFileName);

        // Retrieve fields from ProjectConfiguration and store data into a persistent config file
        _fileConfig->Set<std::wstring>("game_path", _config.classicGamePath);
        _fileConfig->Set<bool>("game_path_manual", _manualGamePath);
        _fileConfig->Set<std::wstring>("game_executable_name", _config.executableName);
        _fileConfig->Set<std::wstring>("mod_dll_name", _config.destinationDllName);

        configHandle.writeFile(_fileConfig->ToString());
    }

    uint32_t Project::GetGameVersion() const {
        if (_gamePath.empty()) {
            MessageBoxA(nullptr, "Failed to extract game path from project", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        auto gameExeHandle = std::ifstream(Utils::StringUtils::WideToNormal(_gamePath), std::ios::binary | std::ios::ate);
        if (!gameExeHandle.good()) {
            MessageBoxA(nullptr, "Failed to find the game executable", _config.name.c_str(), MB_ICONERROR);
            return false;
        }

        const auto gameExeSize = gameExeHandle.tellg();
        gameExeHandle.seekg(0, std::ios::beg);
        std::vector<char> data(gameExeSize);
        gameExeHandle.read(data.data(), gameExeSize);
        const auto checksum = Utils::Hashing::CalculateCRC32(data.data(), gameExeSize);
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
            const auto mod = LoadLibraryW(gDllName);

            if (mod) {
                const auto initFunc = reinterpret_cast<ClientEntryPoint>(GetProcAddress(mod, "InitClient"));
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
