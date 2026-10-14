/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <utils/safe_win32.h>

#include <external/sentry/wrapper.h>
#include <logging/logger.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
    namespace fs = std::filesystem;

    fs::path NativeUnicodeDirectory() {
        // Choose a lossless fixture for the active Windows code page; East Asian
        // code pages cannot necessarily represent the Turkish desktop name.
        for (const auto name : {L"Masa\u00FCst\u00FC", L"\u041F\u0440\u0438\u0432\u0435\u0442", L"\u65E5\u672C", L"\u4E2D\u6587", L"\uD55C\uAE00", L"\u03A9", L"\u05D0", L"\u0627", L"\u0E01"}) {
            try {
                const fs::path candidate(name);
                if (fs::path(candidate.string()) == candidate) {
                    return candidate;
                }
            }
            catch (const std::system_error &) {
            }
        }
        throw std::runtime_error("No non-ASCII fixture is representable in the native code page.");
    }

    int RunCase(std::wstring_view scenario, const fs::path &sandbox, const fs::path &handler) {
        Framework::Logging::GetInstance()->PauseLogging(true);
        const fs::path root = sandbox / (scenario == L"native-unicode" ? NativeUnicodeDirectory() : fs::path(scenario));
        fs::create_directories(root);
        if (scenario != L"missing-handler") {
            fs::copy_file(handler, root / "crashpad_handler.exe");
        }
        if (scenario == L"blocked-cache") {
            std::ofstream(root / "cache") << "A file blocks the cache directory.";
        }
        fs::create_directories(root / "logs");
        const fs::path attachment = root / "logs" / NativeUnicodeDirectory();
        std::ofstream(attachment) << "attachment regression";

        Framework::External::Sentry::InitOptions options;
        // An empty DSN disables uploads while exercising the real Crashpad backend.
        options.dsn         = "";
        options.handlerPath = root.string();
        options.attachments.push_back(attachment.string());
        if (scenario == L"relative") {
            fs::current_path(root);
            options.handlerPath = ".";
            options.attachments = {"logs/log.txt"};
        }

        Framework::External::Sentry::Wrapper reporter;
        const auto result        = reporter.Init(options);
        const bool expectFailure = scenario == L"missing-handler" || scenario == L"blocked-cache";
        if (expectFailure) {
            if (result || reporter.IsInitialized()) {
                std::fprintf(stderr, "Expected initialization to return an error.\n");
                return 1;
            }
            std::printf("Expected error: %s\n", result.GetError().message.c_str());
            return 0;
        }
        if (!result) {
            std::fprintf(stderr, "Initialization failed: %s\n", result.GetError().message.c_str());
            return 1;
        }
        const bool initialized = reporter.IsInitialized() && fs::is_directory(root / "cache" / "sentry");
        const auto attached    = reporter.AddAttachment(attachment.string());
        reporter.Shutdown();
        return initialized && attached && !reporter.IsInitialized() ? 0 : 1;
    }

    bool RunChild(const fs::path &executable, std::wstring_view scenario, const fs::path &sandbox) {
        std::wstring command = L"\"" + executable.wstring() + L"\" --case " + std::wstring(scenario) + L" \"" + sandbox.wstring() + L"\"";
        STARTUPINFOW startup {sizeof(startup)};
        PROCESS_INFORMATION process {};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
            std::fprintf(stderr, "CreateProcessW failed: %lu\n", GetLastError());
            return false;
        }
        const DWORD wait = WaitForSingleObject(process.hProcess, 30000);
        DWORD exitCode   = 1;
        if (wait == WAIT_OBJECT_0) {
            GetExitCodeProcess(process.hProcess, &exitCode);
        }
        else {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 5000);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        std::printf("%ls: %s (exit 0x%08lX)\n", std::wstring(scenario).c_str(), exitCode == 0 ? "PASS" : "FAIL", exitCode);
        return exitCode == 0;
    }
} // namespace

int wmain(int argc, wchar_t **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    wchar_t executableName[32768] {};
    if (!GetModuleFileNameW(nullptr, executableName, 32768)) {
        return 1;
    }
    const fs::path executable(executableName);
    const fs::path handler = executable.parent_path() / "crashpad_handler.exe";
    try {
        if (argc == 4 && std::wstring_view(argv[1]) == L"--case") {
            return RunCase(argv[2], fs::path(argv[3]), handler);
        }

        // Each SDK instance gets its own process, so an access violation fails a case
        // without preventing the remaining cases from running.
        const fs::path sandbox = fs::current_path() / ("framework_ut_sentry_" + std::to_string(GetCurrentProcessId()));
        if (!fs::create_directory(sandbox)) {
            std::fprintf(stderr, "Test sandbox already exists.\n");
            return 1;
        }
        std::printf("Windows native code page: %u\n", GetACP());
        bool passed = true;
        for (const auto scenario : {L"ascii", L"native-unicode", L"relative", L"missing-handler", L"blocked-cache"}) {
            passed = RunChild(executable, scenario, sandbox) && passed;
        }
        std::error_code cleanupError;
        fs::remove_all(sandbox, cleanupError);
        if (cleanupError) {
            std::fprintf(stderr, "Could not remove test sandbox: %s\n", cleanupError.message().c_str());
            passed = false;
        }
        return passed ? 0 : 1;
    }
    catch (const std::exception &error) {
        std::fprintf(stderr, "Sentry test failed: %s\n", error.what());
        return 1;
    }
}
