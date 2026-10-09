/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "platforms.h"

#include "external/epic/auth.h"
#include "external/epic/manifest.h"
#include "external/microsoft_store/package.h"
#include "external/rockstar/library.h"
#include "launcher/project.h"
#include "launcher/rgl_bypass.h"
#include "logging/logger.h"
#include "sfd.h"
#include "utils/hashing.h"
#include "utils/string_utils.h"

#include <Shlwapi.h>

#include <algorithm>
#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>
#include <fmt/format.h>
#include <stdexcept>

namespace Framework::Launcher::Platforms {
    namespace {
        std::wstring ToGameRoot(std::string_view path) {
            auto root = Utils::StringUtils::NormalToWide(std::string(path));
            std::ranges::replace(root, L'\\', L'/');
            return root;
        }

        std::wstring ToGameRoot(std::wstring root) {
            std::ranges::replace(root, L'\\', L'/');
            return root;
        }

        enum class EpicSignInWindow {
            Missing,
            SignedIn,
            Failed
        };

        // The Framework's epic_sign_in.exe, shipped beside the launcher. It persists the refresh token
        // itself; Missing also covers a helper that would not start, so the caller can still fall back.
        EpicSignInWindow RunEpicSignInWindow(const std::filesystem::path &projectPath) {
            const std::filesystem::path helper = projectPath / L"epic_sign_in.exe";
            std::error_code ec;
            if (!std::filesystem::exists(helper, ec)) {
                return EpicSignInWindow::Missing;
            }
            std::wstring commandLine = L"\"" + helper.wstring() + L"\"";
            STARTUPINFOW startupInfo {};
            startupInfo.cb = sizeof(startupInfo);
            PROCESS_INFORMATION processInfo {};
            if (!CreateProcessW(helper.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, projectPath.c_str(), &startupInfo, &processInfo)) {
                return EpicSignInWindow::Missing;
            }
            CloseHandle(processInfo.hThread);
            WaitForSingleObject(processInfo.hProcess, INFINITE);
            DWORD exitCode = 1;
            GetExitCodeProcess(processInfo.hProcess, &exitCode);
            CloseHandle(processInfo.hProcess);
            return exitCode == 0 ? EpicSignInWindow::SignedIn : EpicSignInWindow::Failed;
        }
    } // namespace

    PlatformCheckStatus Steam::Resolve(const PlatformHost &host, PlatformResolution &resolution, bool reportErrors) {
        _tried = true;

        // are we a steam child ?
        const auto child_part    = L"-steamchild:";
        const wchar_t *cmd_match = wcsstr(GetCommandLineW(), child_part);

        if (cmd_match) {
            const int master_pid = _wtoi(&cmd_match[wcslen(child_part)]);

            // open a handle to the parent process with SYNCHRONIZE rights
            const auto handle = OpenProcess(SYNCHRONIZE, FALSE, master_pid);

            // if we opened the process...
            if (handle != INVALID_HANDLE_VALUE) {
                // ... wait for it to exit and close the handle afterwards
                WaitForSingleObject(handle, INFINITE);

                CloseHandle(handle);
            }

            return PlatformCheckStatus::ABORT;
        }

        const auto unavailable = [&](const std::string &reason) {
            return host.ReportUnavailable(GetName(), reason, reportErrors);
        };

        // Make sure we have our required files
        std::error_code ec;
        if (!std::filesystem::is_regular_file(host.GetProjectPath() / "fw_steam_api64.dll", ec) && !std::filesystem::is_regular_file(host.GetProjectPath() / "fw_steam_api.dll", ec)) {
            return unavailable("The Steam runtime bridge is missing from the launcher directory");
        }

        PrepareAppIdentity(host);

        // Initialize the steam wrapper
        External::Steam::Wrapper steam;
        const auto initResult = steam.Init();
        if (!initResult) {
            return unavailable(fmt::format("Failed to init the bridge with steam, are you sure the Steam Client is running? {}", initResult.GetError().message));
        }

        // Make sure steam has the game inside the library
        if (!steam.IsAppInstalled(_options.appId)) {
            steam.Shutdown();
            return unavailable("The destination game is not installed in your Steam library");
        }

        // Ask the game path from steam
        const auto installDir = steam.GetAppInstallDir(_options.appId);
        if (installDir.empty()) {
            steam.Shutdown();
            return unavailable("Steam returned an empty install directory for the destination game");
        }

        const auto installPath = ToGameRoot(installDir);
        if (!host.GameExecutableExistsIn(installPath)) {
            steam.Shutdown();
            return unavailable(fmt::format("Steam points at {}, but the game executable is not there", Utils::StringUtils::WideToNormal(installPath)));
        }

        resolution.gameRoot = installPath;

        // Hand the account id to the in-process client (ClientIdentity); the wrapper is gone by then.
        const auto steamId = steam.GetSteamID().ConvertToUint64();
        if (steamId != 0) {
            host.SetProcessVariable(L"MafiaHubSteamId", std::to_wstring(steamId));
        }

        // Now we have everything we want, just say goodbye
        steam.Shutdown();
        return PlatformCheckStatus::OK;
    }

    bool Steam::AdoptManualCopy(const PlatformHost &host, PlatformResolution &resolution) {
        // a Steam copy the player picked still needs the app identity to start
        PrepareAppIdentity(host);

        // a Steam already asked for the game would only fail the same way again
        if (!_options.adoptManualCopies || _tried) {
            return false;
        }

#ifdef _M_IX86
        const auto steamDllName = L"steam_api.dll";
#else
        const auto steamDllName = L"steam_api64.dll";
#endif
        std::error_code ec;
        if (!std::filesystem::is_regular_file(std::filesystem::path(host.GetGameWorkDir(resolution.gameRoot)) / steamDllName, ec)) {
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("Steam dll found in the game directory, switching to steam platform");
        PlatformResolution steam;
        if (Resolve(host, steam, false) != PlatformCheckStatus::OK) {
            return false;
        }

        resolution = steam;
        return true;
    }

    bool Steam::PrepareLaunch(const PlatformHost &host) {
#ifdef _M_IX86
        const HMODULE steamDll = LoadLibraryW(L"fw_steam_api.dll");
#else
        const HMODULE steamDll = LoadLibraryW(L"fw_steam_api64.dll");
#endif

        if (!steamDll) {
            host.ReportError("Failed to inject the steam runtime DLL in the running process");
            return false;
        }
        return true;
    }

    void Steam::PrepareAppIdentity(const PlatformHost &host) const {
        cppfs::FileHandle appIdFile = cppfs::fs::open("steam_appid.txt");
        appIdFile.writeFile(std::to_string(_options.appId) + "\n");
        host.SetProcessVariable(L"SteamAppId", std::to_wstring(_options.appId));
    }

    PlatformCheckStatus Epic::Resolve(const PlatformHost &host, PlatformResolution &resolution, bool reportErrors) {
        const auto unavailable = [&](const std::string &reason) {
            return host.ReportUnavailable(GetName(), reason, reportErrors);
        };

        // Locate the game via the Epic launcher's plaintext manifests - no SDK or running client
        // needed, just Epic having installed it once. Matched by AppName, else by exe file name.
        const auto exeName = Utils::StringUtils::WideToNormal(host.GetConfig().executableName);
        const auto appName = Utils::StringUtils::WideToNormal(_options.appName);

        const auto app = External::Epic::FindInstalledApp(exeName, appName);
        if (!app.IsValid()) {
            return unavailable("The destination game is not installed through the Epic Games Launcher");
        }

        const auto installPath = ToGameRoot(app.installLocation);
        if (!host.GameExecutableExistsIn(installPath)) {
            return unavailable(fmt::format("Epic points at {}, but the game executable is not there", Utils::StringUtils::WideToNormal(installPath)));
        }

        // Unlike Steam there's no runtime DLL to inject or app-id file to drop; the launch arguments
        // come from PrepareLaunch, once this is the platform the game starts with.
        _app                = app;
        resolution.gameRoot = installPath;
        return PlatformCheckStatus::OK;
    }

    bool Epic::PrepareLaunch(const PlatformHost &host) {
        // A sign-in UI persists a refresh token that we then use like a stored one. Without the
        // sign-in window shipped beside the launcher, fall back to the browser + clipboard flow.
        const auto signIn = [&]() -> std::optional<External::Epic::Tokens> {
            if (auto tokens = External::Epic::TryRefreshStoredAuth()) {
                return tokens;
            }
            if (_options.signIn) {
                return _options.signIn() ? External::Epic::TryRefreshStoredAuth() : std::nullopt;
            }
            switch (RunEpicSignInWindow(host.GetProjectPath())) {
            case EpicSignInWindow::SignedIn: return External::Epic::TryRefreshStoredAuth();
            case EpicSignInWindow::Failed: return std::nullopt;
            case EpicSignInWindow::Missing: break;
            }
            return External::Epic::EnsureAuthenticated(Utils::StringUtils::Utf8ToWide(host.GetConfig().name));
        };
        const auto tokens = signIn();
        if (!tokens) {
            host.ReportError("Epic sign-in is required to play the Epic version of the game");
            return false;
        }

        const auto exchangeCode = External::Epic::GetExchangeCode(*tokens);
        if (!exchangeCode) {
            External::Epic::ClearStoredAuth(); // the next launch signs in afresh
            host.ReportError("Could not obtain an Epic launch code, please try again");
            return false;
        }

        // Hand the account id to the in-process client (ClientIdentity), as Steam does
        if (!tokens->accountId.empty()) {
            host.SetProcessVariable(L"MafiaHubEpicId", Utils::StringUtils::Utf8ToWide(tokens->accountId));
        }
        _launchArguments = External::Epic::BuildLaunchArgs(*tokens, *exchangeCode, _app.appName, _app.catalogNamespace, _app.catalogItemId, _app.installLocation);
        return true;
    }

    PlatformCheckStatus Rockstar::Resolve(const PlatformHost &host, PlatformResolution &resolution, bool reportErrors) {
        const auto unavailable = [&](const std::string &reason) {
            return host.ReportUnavailable(GetName(), reason, reportErrors);
        };

        // Read from the registry, so the launcher itself does not need to be running
        const auto exeName  = Utils::StringUtils::WideToNormal(host.GetConfig().executableName);
        const auto titleKey = Utils::StringUtils::WideToNormal(_options.titleKey);

        const auto title = External::Rockstar::FindInstalledTitle(exeName, titleKey);
        if (!title.IsValid()) {
            return unavailable("The destination game is not installed through the Rockstar Games Launcher");
        }

        const auto installPath = ToGameRoot(title.installFolder);
        if (!host.GameExecutableExistsIn(installPath)) {
            return unavailable(fmt::format("The Rockstar Games Launcher points at {}, but the game executable is not there", title.installFolder));
        }

        Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("Rockstar Games Launcher title '{}' (build {}) resolved to {}", title.titleKey, title.version, title.installFolder);

        resolution.gameRoot = installPath;
        return PlatformCheckStatus::OK;
    }

    bool Rockstar::AdoptManualCopy(const PlatformHost &host, PlatformResolution &resolution) {
        // a wrapped executable needs the snapshot wherever the player keeps it
        return _options.useImageSnapshot;
    }

    bool Rockstar::PrepareImage(const PlatformHost &host, const std::wstring &executablePath, std::span<const uint8_t> image) {
        if (!_options.useImageSnapshot) {
            return true;
        }

        // A wrapped title cannot be mapped from the file alone; its code comes from the cache
        const auto &config = host.GetConfig();
        _snapshot.emplace(host.GetProjectPath() / "cache" / fmt::format("{}_image_snapshot.bin", config.name), Utils::Hashing::CalculateCRC32(reinterpret_cast<const char *>(image.data()), image.size()));

        const auto logger = Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER);
        bool ready        = _snapshot->IsAvailable();
        if (!ready && !_options.captureImage) {
            logger->error("No cached image snapshot for this build of the game, and this launcher sets no captureImage to take one");
        }
        else if (!ready) {
            logger->info("No cached image snapshot for this build of the game, running it once so its decrypted code can be captured");
            ready = _options.captureImage(*_snapshot, executablePath, std::filesystem::path(config.executableName).filename().wstring(), std::vector<uint8_t>(image.begin(), image.end()));
        }

        if (!ready) {
            host.ReportError("The game's decrypted code could not be prepared.\n\nMake sure the Rockstar Games Launcher is installed and signed in, then try again.");
        }
        return ready;
    }

    void Rockstar::OnSectionsMapped(HMODULE module) {
        if (_snapshot) {
            _snapshot->Apply(module);
        }
    }

    uintptr_t Rockstar::ResolveEntryPoint(uintptr_t imageBase, uintptr_t entryPoint) {
        if (!_options.useImageSnapshot) {
            return entryPoint;
        }

        // With the code in place the stub has nothing left to do but spin, so enter past it
        const auto logger = Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER);
        const auto stub   = RGL::ResolveEntryStub(imageBase, entryPoint);
        switch (stub.status) {
        case RGL::EntryStubStatus::RESOLVED:
            logger->info("Skipping the Rockstar Games Launcher entry stub at {:#x}, entering the game at {:#x}", entryPoint, stub.entryPoint);
            return stub.entryPoint;
        case RGL::EntryStubStatus::NOT_PRESENT: logger->info("The game executable carries no Rockstar Games Launcher entry stub, entering it at {:#x}", entryPoint); return entryPoint;
        default: throw std::runtime_error("The Rockstar Games Launcher entry stub could not be decoded, this game build is not supported yet");
        }
    }

    PlatformCheckStatus MicrosoftStore::Resolve(const PlatformHost &host, PlatformResolution &resolution, bool reportErrors) {
        const auto unavailable = [&](const std::string &reason) {
            return host.ReportUnavailable(GetName(), reason, reportErrors);
        };

        const auto package = External::MicrosoftStore::FindInstalledPackage(_options.packageFamily);
        if (!package.IsValid()) {
            return unavailable("The destination game is not installed through the Microsoft Store or the Xbox app for this Windows user");
        }

        const auto installPath = ToGameRoot(package.installPath);
        if (!host.GameExecutableExistsIn(installPath)) {
            return unavailable(fmt::format("The Microsoft Store points at {}, but the game executable is not there", Utils::StringUtils::WideToNormal(installPath)));
        }

        const auto logger      = Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER);
        const auto packageName = Utils::StringUtils::WideToNormal(package.fullName);

        // The package's executable is licence-protected (unreadable outside the package) and its
        // runtime is tied to the package identity, so the game can only be mapped from a process
        // that carries it. Start this launcher again with it and let that copy do the launch. The
        // copy is marked, so one Windows started without the identity reports it rather than
        // starting another.
        if (!External::MicrosoftStore::IsRunningInPackage(_options.packageFamily)) {
            constexpr auto relaunchMarker = L"-fw-package-relaunch";
            if (wcsstr(GetCommandLineW(), relaunchMarker)) {
                return unavailable(fmt::format("The launcher was started inside the {} package but does not run with its identity", packageName));
            }

            HRESULT error {};
            const std::wstring arguments = std::wstring(PathGetArgsW(GetCommandLineW())) + L" " + relaunchMarker;
            const HANDLE child           = External::MicrosoftStore::StartInPackage(_options.packageFamily, _options.appId, host.GetLauncherExecutablePath(), arguments, error);
            if (!child) {
                return unavailable(fmt::format("Windows could not start the launcher inside the {} package (0x{:08X})", packageName, static_cast<uint32_t>(error)));
            }

            logger->info("Microsoft Store package {} found, the launch goes on in the launcher started inside it (pid {})", packageName, GetProcessId(child));
            CloseHandle(child);
            return PlatformCheckStatus::HANDED_OFF;
        }

        logger->info("Running inside the Microsoft Store package {}", packageName);
        resolution.gameRoot = installPath;
        return PlatformCheckStatus::OK;
    }

    PlatformCheckStatus Classic::Resolve(const PlatformHost &host, PlatformResolution &resolution, bool reportErrors) {
        const auto &stored = host.GetConfig().classicGamePath;
        if (host.GameExecutableExistsIn(stored)) {
            resolution.gameRoot = stored;
            return PlatformCheckStatus::OK;
        }

        if (!_options.prompt) {
            host.ReportError("Please specify game path");
            return PlatformCheckStatus::ABORT;
        }

        if (!Prompt(host, resolution.gameRoot)) {
            return PlatformCheckStatus::ABORT;
        }

        resolution.manual = true;
        return PlatformCheckStatus::OK;
    }

    bool Classic::Prompt(const PlatformHost &host, std::wstring &gameRoot) const {
        const auto &config   = host.GetConfig();
        const auto startPath = host.GetProjectPath().string();

        sfd_Options sfd = {};
        sfd.path        = startPath.c_str();
        sfd.extension   = _options.extension.c_str();
        sfd.filter_name = _options.filterName.c_str();
        sfd.filter      = _options.filter.c_str();
        sfd.title       = _options.title.c_str();

        const char *picked = sfd_open_dialog(&sfd);

        // the dialog leaves the working directory wherever the player browsed to
        SetCurrentDirectoryW(host.GetProjectPath().c_str());

        if (!picked) {
            return false;
        }

        const std::filesystem::path exePath(Utils::StringUtils::NormalToWide(picked));

        std::error_code ec;
        if (!std::filesystem::is_regular_file(exePath, ec)) {
            host.ReportError("Cannot find a game executable by given path:\n" + std::string(picked) + "\n\n Please check your path and try again!");
            return false;
        }

        const auto expectedName = Utils::StringUtils::WideToNormal(config.executableName);
        if (_wcsicmp(exePath.filename().c_str(), config.executableName.c_str()) != 0) {
            host.ReportError("Please select " + expectedName + ", not " + Utils::StringUtils::WideToNormal(exePath.filename().wstring()) + ".");
            return false;
        }

        // stores hand back the game root, so strip the work dir off the picked executable's folder
        auto root = exePath.parent_path();
        for (const auto &candidate : host.GetAlternativeWorkDirCandidates()) {
            std::vector<std::wstring> parts;
            for (const auto &part : std::filesystem::path(candidate)) {
                if (!part.empty()) {
                    parts.push_back(part.wstring());
                }
            }

            auto stripped = root;
            bool matched  = !parts.empty();
            for (auto it = parts.rbegin(); matched && it != parts.rend(); ++it) {
                if (_wcsicmp(stripped.filename().c_str(), it->c_str()) != 0) {
                    matched = false;
                    break;
                }

                stripped = stripped.parent_path();
            }

            if (matched) {
                root = stripped;
                break;
            }
        }

        auto gamePath = ToGameRoot(root.wstring());
        if (_options.onSelection) {
            gamePath = _options.onSelection(gamePath);
        }

        if (!host.GameExecutableExistsIn(gamePath)) {
            host.ReportError("Cannot find " + expectedName + " inside the selected game directory:\n" + Utils::StringUtils::WideToNormal(gamePath));
            return false;
        }

        gameRoot = gamePath;
        return true;
    }
} // namespace Framework::Launcher::Platforms
