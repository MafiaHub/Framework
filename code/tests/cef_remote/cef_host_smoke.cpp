/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// Drives the remote CEF host the way a client does, through Manager, without a game: a page served
// from a client-side fw:// root must paint into the frame surface, its callEvent and an
// EvaluateScript round trip must arrive, this process must never load libcef.dll, and a killed
// host must come back with the page reloaded. Exits non-zero on the first failed check.

#include <utils/safe_win32.h>

#include "gui/manager.h"
#include "gui/remote/remote_view.h"
#include "gui/resources/memory_provider.h"

#include <logging/logger.h>

#include <TlHelp32.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {
    using namespace Framework::GUI;

    constexpr int kWidth  = 320;
    constexpr int kHeight = 200;

    int g_failures = 0;

    void Check(bool condition, const char *what) {
        std::printf(" * [%s] %s\n", condition ? "PASS" : "FAIL", what);
        std::fflush(stdout);
        if (!condition) {
            ++g_failures;
        }
    }

    std::filesystem::path ExecutableDirectory() {
        wchar_t path[MAX_PATH] = {};
        const DWORD length     = GetModuleFileNameW(nullptr, path, MAX_PATH);
        return std::filesystem::path(std::wstring(path, length)).parent_path();
    }

    // The cef_host.exe this process started.
    DWORD FindHostProcess() {
        DWORD found           = 0;
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return 0;
        }
        PROCESSENTRY32W entry {};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
            if (entry.th32ParentProcessID == GetCurrentProcessId() && _wcsicmp(entry.szExeFile, L"cef_host.exe") == 0) {
                found = entry.th32ProcessID;
                break;
            }
        }
        CloseHandle(snapshot);
        return found;
    }

    // Center pixel of the newest frame, BGRA.
    bool CenterIsRed(Remote::RemoteView *view) {
        std::vector<uint8_t> pixels;
        int width  = 0;
        int height = 0;
        if (!view->SnapshotFrame(pixels, width, height) || width != kWidth || height != kHeight) {
            return false;
        }
        const std::size_t offset = (static_cast<std::size_t>(height / 2) * width + width / 2) * 4;
        return pixels[offset] == 0 && pixels[offset + 1] == 0 && pixels[offset + 2] == 255 && pixels[offset + 3] == 255;
    }

    template <class Condition>
    bool PumpUntil(Manager &manager, int timeoutMs, Condition condition) {
        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        while (GetTickCount64() < deadline) {
            manager.Update();
            if (condition()) {
                return true;
            }
            Sleep(16);
        }
        return false;
    }
} // namespace

int main() {
    const std::filesystem::path rootDir = ExecutableDirectory() / "cef-smoke";
    std::filesystem::create_directories(rootDir / "logs");
    auto *logging = Framework::Logging::GetInstance();
    logging->SetLogName("cef_smoke");
    logging->SetLogFolder((rootDir / "logs").string());

    _putenv_s("FW_CEF_MODE", "remote");

    std::printf("remote CEF host smoke test (root %s)\n", rootDir.string().c_str());
    {
        Manager manager;
        const auto init = manager.Init(rootDir.string(), {kWidth, kHeight}, nullptr);
        Check(static_cast<bool>(init), "the host starts; CEF comes up behind it");
        Check(manager.IsRemote(), "the manager runs in remote mode");
        if (!init || !manager.IsRemote()) {
            return 1;
        }

        auto pages = std::make_shared<Resources::MemoryProvider>("smoke");
        pages->Set("index.html", "<!doctype html><html><body style=\"margin:0;background:#ff0000\"><script>callEvent('smoke:ready', location.origin)</script></body></html>", "text/html");
        manager.RegisterResourceRoot("smoke", pages);

        const std::string url = Manager::ResourceURL("smoke", "index.html");
        const int id          = manager.CreateView(url, kWidth, kHeight);
        auto *view            = static_cast<Remote::RemoteView *>(manager.GetView(id));
        Check(view != nullptr, "a view is created");
        if (!view) {
            return 1;
        }
        view->LockToOrigin(url);
        view->Display(true);

        std::string ready;
        std::string evaluated;
        std::string origin;
        bool documentReady = false;
        view->AddEventListener("smoke:ready", [&ready](const std::string &payload) {
            ready = payload;
        });
        view->AddEventListener("smoke:eval", [&evaluated](const std::string &payload) {
            evaluated = payload;
        });
        view->SetOnViewEventCallback([&](const ViewEventData &data) {
            if (data.event == ViewEvent::DocumentReady && data.isMainFrame) {
                documentReady = true;
            }
            if (data.event == ViewEvent::OriginChange) {
                origin = data.origin;
            }
        });

        Check(PumpUntil(manager, 30000,
                  [&] {
                      return documentReady && !ready.empty();
                  }),
            "the fw:// page loads from the client's root and its callEvent arrives");
        Check(ready == "fw://smoke", "callEvent carries its payload");
        Check(origin == "fw://smoke", "the origin lock is reported");
        Check(PumpUntil(manager, 10000,
                  [&] {
                      return CenterIsRed(view);
                  }),
            "the page's pixels arrive in the frame surface");

        view->EvaluateScript("callEvent('smoke:eval', String(6 * 7))");
        Check(PumpUntil(manager, 10000,
                  [&] {
                      return !evaluated.empty();
                  }),
            "EvaluateScript runs in the page");
        Check(evaluated == "42", "and its result comes back");

        view->Resize(kWidth, kHeight / 2);
        Check(PumpUntil(manager, 10000,
                  [&] {
                      std::vector<uint8_t> pixels;
                      int width  = 0;
                      int height = 0;
                      return view->SnapshotFrame(pixels, width, height) && height == kHeight / 2;
                  }),
            "a resize moves the view to a new frame surface");
        view->Resize(kWidth, kHeight);
        PumpUntil(manager, 10000, [&] {
            return CenterIsRed(view);
        });

        Check(GetModuleHandleW(L"libcef.dll") == nullptr, "this process never loaded libcef.dll");

        // The host keeps a destroyed view until CEF has closed its browser; one made right after paints.
        {
            const int other = manager.CreateView(url, kWidth, kHeight);
            auto *second    = static_cast<Remote::RemoteView *>(manager.GetView(other));
            Check(second != nullptr, "a second view is created");
            if (second) {
                second->Display(true);
                Check(PumpUntil(manager, 10000,
                          [&] {
                              return CenterIsRed(second);
                          }),
                    "the second view paints");
                const DWORD before = FindHostProcess();
                Check(manager.DestroyView(other), "the second view is destroyed");
                PumpUntil(manager, 2000, [] {
                    return false;
                });
                Check(before != 0 && FindHostProcess() == before && CenterIsRed(view), "the host closes it and carries on with the first");
            }
        }

        // Crash recovery: the page comes back as on a reload.
        const DWORD hostId = FindHostProcess();
        Check(hostId != 0, "the host runs as a child of this process");
        HANDLE host = hostId ? OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, hostId) : nullptr;
        if (host) {
            ready.clear();
            documentReady = false;
            TerminateProcess(host, 9);
            WaitForSingleObject(host, 5000);
            CloseHandle(host);
            Check(PumpUntil(manager, 45000,
                      [&] {
                          return documentReady && !ready.empty() && CenterIsRed(view);
                      }),
                "a killed host is restarted and the view reloads and paints again");
        }

        const DWORD restartedId = FindHostProcess();
        HANDLE restarted        = restartedId ? OpenProcess(SYNCHRONIZE, FALSE, restartedId) : nullptr;
        manager.Shutdown();
        if (restarted) {
            Check(WaitForSingleObject(restarted, 0) == WAIT_OBJECT_0, "shutdown stops the host");
            CloseHandle(restarted);
        }
        Check(GetModuleHandleW(L"libcef.dll") == nullptr, "libcef.dll is still not loaded after shutdown");
    }

    std::printf("%s: %d check(s) failed\n", g_failures ? "FAILED" : "OK", g_failures);
    logging->Shutdown();
    return g_failures ? 1 : 0;
}
