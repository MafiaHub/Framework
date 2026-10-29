/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "runtime.h"

#include "life_span_handler.h"

#include <logging/logger.h>

#include "gui/resources/scheme.h"

#include "include/cef_app.h"

#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>

namespace Framework::GUI::CEF {
    namespace {
        constexpr std::size_t kMaxCefCacheProfiles = 16;

        // Bounds the wait for OnContextInitialized in StartRuntime
        constexpr ULONGLONG kContextInitializedTimeoutMs = 5000;

        // CefShutdown is as unreliable as the pump on these exit paths, and it
        // faults inside Chromium's own teardown where nothing we own is left to
        // save. Guard it the same way: the process is leaving either way.
        bool CefShutdownGuarded() {
            __try {
                CefShutdown();
                return true;
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                return false;
            }
        }

        const char *CefResultCodeName(int code) {
            switch (code) {
            case CEF_RESULT_CODE_NORMAL_EXIT: return "NORMAL_EXIT";
            case CEF_RESULT_CODE_KILLED: return "KILLED";
            case CEF_RESULT_CODE_HUNG: return "HUNG";
            case CEF_RESULT_CODE_KILLED_BAD_MESSAGE: return "KILLED_BAD_MESSAGE";
            case CEF_RESULT_CODE_GPU_DEAD_ON_ARRIVAL: return "GPU_DEAD_ON_ARRIVAL";
            case CEF_RESULT_CODE_BAD_PROCESS_TYPE: return "BAD_PROCESS_TYPE";
            case CEF_RESULT_CODE_MISSING_DATA: return "MISSING_DATA";
            case CEF_RESULT_CODE_UNSUPPORTED_PARAM: return "UNSUPPORTED_PARAM";
            case CEF_RESULT_CODE_PROFILE_IN_USE: return "PROFILE_IN_USE";
            case CEF_RESULT_CODE_NORMAL_EXIT_PROCESS_NOTIFIED: return "NORMAL_EXIT_PROCESS_NOTIFIED";
            case CEF_RESULT_CODE_INVALID_SANDBOX_STATE: return "INVALID_SANDBOX_STATE";
            case CEF_RESULT_CODE_SYSTEM_RESOURCE_EXHAUSTED: return "SYSTEM_RESOURCE_EXHAUSTED";
            default: return "unmapped";
            }
        }

        const char *CefResultCodeHint(int code) {
            switch (code) {
            case CEF_RESULT_CODE_MISSING_DATA: return "a CEF runtime file is missing or unreadable";
            case CEF_RESULT_CODE_PROFILE_IN_USE: return "another process still holds this cache profile; delete cache/profiles or end leftover cef_subprocess.exe";
            case CEF_RESULT_CODE_NORMAL_EXIT_PROCESS_NOTIFIED: return "the profile was handed to an already-running browser process";
            case CEF_RESULT_CODE_SYSTEM_RESOURCE_EXHAUSTED: return "the system is out of a resource CEF needs (handles, memory, desktop heap)";
            default: return "";
            }
        }

        std::string ResolveUiLocaleName() {
            wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
            if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) == 0) {
                return {};
            }
            return std::filesystem::path(name).string();
        }

        void AuditCefRuntimeFiles(const std::filesystem::path &moduleDir) {
            const auto logger = Framework::Logging::GetLogger("Web");

            static constexpr const char *kRequiredFiles[] = {"libcef.dll", "cef_subprocess.exe", "chrome_elf.dll", "icudtl.dat", "resources.pak", "chrome_100_percent.pak", "chrome_200_percent.pak", "v8_context_snapshot.bin"};

            std::error_code error;
            bool complete = true;
            for (const char *file : kRequiredFiles) {
                const std::filesystem::path candidate = moduleDir / file;
                if (!std::filesystem::is_regular_file(candidate, error)) {
                    logger->error("CEF runtime file is missing: {}", candidate.string());
                    complete = false;
                }
            }

            const std::filesystem::path localesDir = moduleDir / "locales";
            if (!std::filesystem::is_directory(localesDir, error)) {
                logger->error("CEF locales directory is missing: {}", localesDir.string());
                return;
            }

            std::size_t pakCount = 0;
            for (const auto &entry : std::filesystem::directory_iterator(localesDir, error)) {
                if (entry.path().extension() == ".pak") {
                    ++pakCount;
                }
            }

            const std::string uiLocale = ResolveUiLocaleName();
            if (uiLocale.empty()) {
                logger->error("CEF locales: {} pak(s) in '{}', OS UI locale could not be resolved", pakCount, localesDir.string());
            }
            else {
                // Chromium falls back from the full name to the bare language.
                const std::size_t separator          = uiLocale.find('-');
                const std::filesystem::path exact    = localesDir / (uiLocale + ".pak");
                const std::filesystem::path language = separator == std::string::npos ? exact : localesDir / (uiLocale.substr(0, separator) + ".pak");
                const bool localeAvailable           = std::filesystem::is_regular_file(exact, error) || std::filesystem::is_regular_file(language, error);

                logger->error("CEF locales: {} pak(s) in '{}', OS UI locale '{}', matching pak {}", pakCount, localesDir.string(), uiLocale, localeAvailable ? "present" : "MISSING");
                complete = complete && localeAvailable;
            }

            if (complete) {
                logger->error("CEF runtime files are all present; the failure is not a missing file");
            }
        }

        std::optional<std::pair<std::filesystem::path, HANDLE>> ClaimCefCacheProfile(const std::string &rootDir) {
            std::error_code error;
            const std::filesystem::path profilesRoot = std::filesystem::absolute(std::filesystem::path(rootDir) / "cache" / "profiles", error);
            if (error) {
                return std::nullopt;
            }

            // Separate Wine prefixes run separate wineservers, which do not
            // enforce the profile lock against each other.
            std::size_t first = 0;
            if (const char *base = std::getenv("FW_CEF_PROFILE_BASE")) {
                first = static_cast<std::size_t>(std::strtoul(base, nullptr, 10));
            }
            for (std::size_t index = first; index < first + kMaxCefCacheProfiles; ++index) {
                const std::filesystem::path profileRoot = profilesRoot / std::to_string(index);
                std::filesystem::create_directories(profileRoot, error);
                if (error) {
                    return std::nullopt;
                }

                const std::filesystem::path lockPath = profileRoot / ".framework-profile.lock";
                HANDLE profileLock                   = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
                if (profileLock != INVALID_HANDLE_VALUE) {
                    return std::make_pair(profileRoot, profileLock);
                }
            }

            return std::nullopt;
        }
    } // namespace

    bool PumpMessageLoopGuarded() {
        __try {
            CefDoMessageLoopWork();
            return true;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            return false;
        }
    }

    bool StartRuntime(const std::string &rootDir, const std::filesystem::path &moduleDir, CefRefPtr<App> app, CefRefPtr<CefSchemeHandlerFactory> resourceFactory, HANDLE &profileLock) {
        // Configure CEF settings
        CefSettings settings;
        settings.windowless_rendering_enabled = true;
        settings.multi_threaded_message_loop  = false;
        settings.external_message_pump        = app->UsesExternalMessagePump();
        settings.no_sandbox                   = true;
        // ERROR by default. On a machine running VBS, Chromium also logs a failed
        // Hyper-V CPU counter every 15 seconds (CpuHealthTracker, which no switch
        // or feature disables), but FATAL alone hides a renderer that fails to
        // launch or dies, and a page then stalls with an empty log.
        // MafiaHubCefErrorLog=verbose goes all the way down, for bring-up work.
        const char *cefErrorLog = std::getenv("MafiaHubCefErrorLog");
        const bool verboseLog   = cefErrorLog && std::string_view(cefErrorLog) == "verbose";
        settings.log_severity   = verboseLog ? LOGSEVERITY_VERBOSE : LOGSEVERITY_ERROR;

        std::error_code logDirError;
        const std::filesystem::path logDir = std::filesystem::path(rootDir) / "logs";
        std::filesystem::create_directories(logDir, logDirError);
        const std::filesystem::path cefLogPath = logDir / "cef.log";
        CefString(&settings.log_file)          = cefLogPath.wstring();

        // CEF requires an absolute path for the subprocess executable
        const std::filesystem::path subprocessPath   = moduleDir / "cef_subprocess.exe";
        CefString(&settings.browser_subprocess_path) = subprocessPath.wstring();

        // CEF holds a process-singleton lock on root_cache_path. Stable numbered
        // profiles keep browser storage across launches while the extra lock
        // gives concurrent clients different roots. The OS releases it after a
        // crash, so the lowest available profile is reusable on the next run.
        const auto cacheProfile = ClaimCefCacheProfile(rootDir);
        if (!cacheProfile) {
            Framework::Logging::GetLogger("Web")->error("Failed to claim a CEF cache profile under '{}' ({} profiles tried, last error {}); is the directory writable?", (std::filesystem::path(rootDir) / "cache" / "profiles").string(), kMaxCefCacheProfiles, GetLastError());
            return false;
        }
        const std::filesystem::path &cacheRoot = cacheProfile->first;
        profileLock                            = cacheProfile->second;
        CefString(&settings.root_cache_path)   = cacheRoot.wstring();
        CefString(&settings.cache_path)        = cacheRoot.wstring();

        Framework::Logging::GetLogger("Web")->debug("Initializing CEF: module '{}', cache profile '{}', log '{}'", moduleDir.string(), cacheRoot.string(), cefLogPath.string());

        // Explorer runs a downloaded exe inside its own job, and other hosts
        // bring theirs; the helpers inherit it, so a log should say so
        BOOL inJob = FALSE;
        if (IsProcessInJob(GetCurrentProcess(), nullptr, &inJob) && inJob) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
            DWORD written = 0;
            if (QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits), &written)) {
                Framework::Logging::GetLogger("Web")->debug("Process runs inside a job (limit flags 0x{:X}); CEF helpers inherit it", limits.BasicLimitInformation.LimitFlags);
            }
            else {
                Framework::Logging::GetLogger("Web")->debug("Process runs inside a job; CEF helpers inherit it");
            }
        }

        // Initialize CEF
        CefMainArgs mainArgs(GetModuleHandle(nullptr));
        if (!CefInitialize(mainArgs, settings, app, nullptr)) {
            // CefGetExitCode is the only CEF call allowed after a failed init.
            const int exitCode = CefGetExitCode();
            const auto logger  = Framework::Logging::GetLogger("Web");
            logger->error("CefInitialize failed: exit code {} ({})", exitCode, CefResultCodeName(exitCode));
            if (const char *hint = CefResultCodeHint(exitCode); *hint) {
                logger->error("{}", hint);
            }
            logger->error("CefInitialize inputs: module '{}', cache profile '{}'", moduleDir.string(), cacheRoot.string());
            AuditCefRuntimeFiles(moduleDir);
            if (std::filesystem::exists(cefLogPath, logDirError)) {
                logger->error("Chromium logged the underlying error to '{}'", cefLogPath.string());
            }
            else {
                logger->error("No '{}' was written; CEF bailed before its own logging came up", cefLogPath.string());
            }

            CloseHandle(profileLock);
            profileLock = INVALID_HANDLE_VALUE;
            return false;
        }

        // One factory for the whole scheme; an empty domain matches every host
        // and the registry routes them, so CEF never learns about a root.
        CefRegisterSchemeHandlerFactory(Resources::kResourceScheme, "", resourceFactory);

        // OnContextInitialized only arrives through the pump, and a browser
        // created before it races the global request context and profile
        // setup, slowest on a new or migrated cache profile. Init returns
        // ready, so callers can create views straight away.
        {
            const auto deadline = GetTickCount64() + kContextInitializedTimeoutMs;
            while (!app->IsContextInitialized() && GetTickCount64() < deadline) {
                if (!PumpMessageLoopGuarded()) {
                    break;
                }
                Sleep(1);
            }
            if (!app->IsContextInitialized()) {
                Framework::Logging::GetLogger("Web")->warn("CEF context not initialized after {} ms; views may start late", kContextInitializedTimeoutMs);
            }
        }

        Framework::Logging::GetLogger("Web")->debug("Using persistent CEF cache profile '{}'", cacheRoot.string());
        return true;
    }

    bool StopRuntime(bool &pumpFailed) {
        CefClearSchemeHandlerFactories();

        // CloseBrowser is async: pump (guarded) until every browser hits
        // OnBeforeClose, 3s cap, then a settle pass.
        bool pumpOk   = true;
        int drainedMs = 0;
        while (pumpOk && LifeSpanHandler::GetLiveBrowserCount() > 0 && drainedMs < 3000) {
            pumpOk = PumpMessageLoopGuarded();
            Sleep(10);
            drainedMs += 10;
        }
        for (int i = 0; pumpOk && i < 50; i++) {
            pumpOk = PumpMessageLoopGuarded();
            Sleep(10);
        }

        // CEF requires every browser closed before CefShutdown; calling it
        // with one still live faults inside Chromium's teardown. The drain
        // above is capped, so the cap can expire with browsers left.
        const int liveBrowsers = LifeSpanHandler::GetLiveBrowserCount();
        if (!pumpOk) {
            pumpFailed = true;
            Framework::Logging::GetLogger("Web")->warn("CEF pump faulted during shutdown drain, skipping CefShutdown");
        }
        else if (liveBrowsers > 0) {
            Framework::Logging::GetLogger("Web")->warn("{} browser(s) still live after the shutdown drain, skipping CefShutdown", liveBrowsers);
        }
        else if (CefShutdownGuarded()) {
            return true;
        }
        else {
            Framework::Logging::GetLogger("Web")->warn("CefShutdown faulted; leaving CEF to the process exit");
        }
        return false;
    }
} // namespace Framework::GUI::CEF
